// pca_views.js -- the visualization layer for the PCA regime model (see the visualization task spec).
//
// DISPLAY ONLY. Every number drawn here is read from payloads the backend already produced:
//   regime_static / regime_live  (unchanged, drive the existing PCA 3D view)
//   regime_viz / regime_viz_live (display-only: per-day PCA scores, posteriors, z-scores, t-SNE 2D/3D
//                                 coordinates, estimated feature directions, fidelity metrics)
// Nothing here classifies, clusters, re-labels or scores. Regime membership always comes from the model.
//
// One scene engine renders three more views of the same points:
//   pca2d   linear PCA, flat (orthographic) biplot with real loadings; axis-pair selectable
//   tsne2d  non-linear neighbourhood view; arrows are ESTIMATED directions (regression, with R2)
//   tsne3d  same, with a third embedding axis
// Switching modes morphs the points between coordinate sets instead of redrawing.

const VT_VIEW_MODES = ['pca2d', 'tsne2d', 'tsne3d'];
const VT_PAIRS = [[0, 1], [0, 2], [1, 2]];

function vtHexRgb(h) { const n = parseInt(h.replace('#', ''), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; }
function vtEase(t) { return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2; }
// diverging map for z-scores: blue (low) - neutral - orange (high)
function vtDiverge(z) {
    const t = Math.max(-1, Math.min(1, z / 3));
    const lo = [64, 140, 255], mid = [120, 128, 140], hi = [255, 168, 56];
    const a = t < 0 ? lo : hi, k = Math.abs(t);
    return [mid[0] + (a[0] - mid[0]) * k, mid[1] + (a[1] - mid[1]) * k, mid[2] + (a[2] - mid[2]) * k];
}

// sequential map (purple -> orange -> yellow), as in the reference scatter; t in [0,1]
function vtPlasma(t) {
    t = Math.max(0, Math.min(1, t)); const st = [[84, 14, 150], [176, 42, 148], [237, 121, 83], [250, 200, 40]], x = t * (st.length - 1), i = Math.min(st.length - 2, Math.floor(x)), f = x - i;
    return st[i].map((c, k) => c + (st[i + 1][k] - c) * f);
}
function vtNiceTicks(lo, hi, n) {
    const span = hi - lo || 1, raw = span / n, mag = Math.pow(10, Math.floor(Math.log10(raw))), r = raw / mag, step = (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag, out = [];
    for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9; v += step) out.push(+v.toFixed(10)); return { out, step };
}

class ProjectionView {
    constructor(panel) {
        this.panel = panel;
        this.active = false;
        this.mode = 'pca2d';
        this.pair = [0, 1];
        this.viz = null;          // regime_viz payload
        this.live = [];           // live timeline entries {pca, tsne2, tsne3, z, posterior, ood, conf}
        this.sel = null;          // {type:'pt', i} | {type:'live', i}
        this.hiddenF = new Set(); this.rect = null; this.off = [0, 0];
        this.pan = [0, 0]; this.panMode = false; this.hidden = new Set();
        this.spot = -1;           // feature index being spotlighted (-1 = none)
        this.showAll = false;
        this.tpos = null;         // timeline scrubber position (null = follow latest)
        this.playing = null;
        this.cam = new Cam3D({ yaw: 0, pitch: 0, zoom: 1, radius: 13 });
        this.cam.persp = 0;
        this.unlocked = false;    // flat views only tilt after a deliberate drag
        this.pos = [];            // current displayed world position of every item
        this.morph = null;
        this.decomp = 0;          // 0..1 progress of the animated contribution decomposition
        this._labels = [];

        this.canvas = document.createElement('canvas');
        this.canvas.style.cssText = 'position:absolute;left:0;top:0;display:none;';
        panel.root.insertBefore(this.canvas, panel.legend);
        this._bindPointer();
        this._buildChrome();
    }

    // ------------------------------------------------------------ chrome (controls overlaid on the canvas)
    _buildChrome() {
        const r = this.panel.root;
        this.bar = document.createElement('div');
        this.bar.className = 'vt-view-bar'; this.bar.style.display = 'none';
        this.bar.innerHTML = `
            <span class="grp" data-grp="pair">
              <button class="vt-pca-toggle-btn active" data-pair="0">PC1 / PC2</button>
              <button class="vt-pca-toggle-btn" data-pair="1">PC1 / PC3</button>
              <button class="vt-pca-toggle-btn" data-pair="2">PC2 / PC3</button>
            </span>
            <button class="vt-pca-toggle-btn" data-act="flatten">Flatten</button>
            <button class="vt-pca-toggle-btn" data-act="showall">Show all labels</button>
            <label class="vt-view-select">Color by
              <select data-act="colorby"><option value="-1">Regime</option></select></label>`;
        r.appendChild(this.bar);
        // Plotly-style modebar (zoom in/out, pan, reset, PNG) and click-to-toggle legend
        this.modebar = document.createElement('div'); this.modebar.className = 'vt-view-modebar'; this.modebar.style.display = 'none';
        this.modebar.innerHTML = `<button data-act="zin" title="Zoom in" aria-label="Zoom in">+</button><button data-act="zout" title="Zoom out" aria-label="Zoom out">&minus;</button>
            <button data-act="pan" title="Pan (drag moves the view)" aria-label="Pan">&#10021;</button><button data-act="home" title="Reset camera" aria-label="Reset camera">&#8962;</button><button data-act="png" title="Download PNG" aria-label="Download PNG">&#8681;</button>`;
        r.appendChild(this.modebar);
        this.modebar.querySelector('[data-act=zin]').addEventListener('click', () => { this.cam.zoom = Math.min(6, this.cam.zoom * 1.25); this.draw(); });
        this.modebar.querySelector('[data-act=zout]').addEventListener('click', () => { this.cam.zoom = Math.max(0.35, this.cam.zoom / 1.25); this.draw(); });
        this.modebar.querySelector('[data-act=pan]').addEventListener('click', (e) => { this.panMode = !this.panMode; e.currentTarget.classList.toggle('on', this.panMode); });
        this.modebar.querySelector('[data-act=home]').addEventListener('click', () => { this.pan = [0, 0]; this.cam.zoom = 1; this.cam.roll = 0; this.hidden.clear(); this._renderLegend(); this.flatten(); this.draw(); });
        this.modebar.querySelector('[data-act=png]').addEventListener('click', () => { try { const a = document.createElement('a'); a.download = 'regime_' + this.mode + '.png'; a.href = this.canvas.toDataURL('image/png'); a.click(); } catch (e) {} });

        this.bar.querySelectorAll('[data-pair]').forEach(b => b.addEventListener('click', () => {
            this.bar.querySelectorAll('[data-pair]').forEach(x => x.classList.toggle('active', x === b));
            this.setPair(VT_PAIRS[+b.dataset.pair]);
        }));
        this.bar.querySelector('[data-act=flatten]').addEventListener('click', () => this.flatten());
        this.bar.querySelector('[data-act=showall]').addEventListener('click', (e) => { this.showAll = !this.showAll; e.target.classList.toggle('active', this.showAll); this.draw(); });
        this.bar.querySelector('[data-act=colorby]').addEventListener('change', (e) => { this.spot = +e.target.value; this.draw(); });

        this.vars = document.createElement('div'); this.vars.className = 'vt-view-vars'; this.vars.style.display = 'none'; r.appendChild(this.vars);
        this.vars.addEventListener('click', (e) => {
            const act = e.target.closest('[data-vact]'); if (act) { const a = act.dataset.vact, n = (this.viz.feature_short || []).length;
                if (a === 'all') this.hiddenF.clear(); else if (a === 'none') this.hiddenF = new Set(Array.from({ length: n }, (_, i) => i)); else if (a === 'top') this._showTop(8);
                this._renderVars(); this.draw(); return; }
            const eye = e.target.closest('.eye'), row = e.target.closest('.vr'); if (!row) return; const f = +row.dataset.f;
            if (eye) { this.hiddenF.has(f) ? this.hiddenF.delete(f) : this.hiddenF.add(f); }
            else { this.spot = this.spot === f ? -1 : f; this.bar.querySelector('[data-act=colorby]').value = String(this.spot); if (this.spot === f) this.hiddenF.delete(f); }
            this._renderVars(); this.draw(); });
        this.badge = document.createElement('div'); this.badge.className = 'vt-view-badge'; this.badge.style.display = 'none';
        r.appendChild(this.badge);

        this.card = document.createElement('div'); this.card.className = 'vt-view-card'; this.card.style.display = 'none';
        r.appendChild(this.card);

        this.scrub = document.createElement('div'); this.scrub.className = 'vt-view-scrub'; this.scrub.style.display = 'none';
        this.scrub.innerHTML = `<button class="vt-pca-toggle-btn" data-act="play">Replay</button>
            <input type="range" min="0" max="0" step="1" value="0"><span class="lbl"></span>`;
        r.appendChild(this.scrub);
        this.scrub.querySelector('[data-act=play]').addEventListener('click', () => this.togglePlay());
        this.scrub.querySelector('input').addEventListener('input', (e) => { this.stopPlay(); this.tpos = +e.target.value; this._syncScrub(); this.draw(); });

        this.ribbon = document.createElement('div'); this.ribbon.className = 'vt-view-ribbon'; this.ribbon.style.display = 'none';
        r.appendChild(this.ribbon);

        this.foot = document.createElement('div'); this.foot.className = 'vt-view-foot'; this.foot.style.display = 'none';
        r.appendChild(this.foot);

        this.hint = document.createElement('div'); this.hint.className = 'vt-view-hint'; this.hint.style.display = 'none';
        r.appendChild(this.hint);
        this.note = document.createElement('div'); this.note.className = 'vt-view-note'; r.appendChild(this.note);
    }

    // ------------------------------------------------------------ data in
    setViz(viz) {
        this.viz = viz; this.live = []; this.sel = null; this.tpos = null;
        const sel = this.bar.querySelector('[data-act=colorby]');
        sel.innerHTML = '<option value="-1">Regime</option><option value="-2">Confidence (max posterior)</option><option value="-3">Time (day order)</option>' + (viz.feature_short || []).map((n, i) => `<option value="${i}">${n}</option>`).join('');
        sel.value = String(this.spot);
        this._kdeCache = null; this.hidden.clear(); this.hiddenF.clear(); this._renderLegend();
        this._relayout(); this._syncScrub(); this._updateChromeState(); this._renderFoot(); this._renderCard();
        if (this.active) { this._snapPositions(); this.draw(); }
    }
    _renderLegend() { if (this.panel._renderLegend) this.panel._renderLegend(); }   // the REGIMES panel is the single (clickable) legend
    _clearChrome() { [this.bar, this.card, this.vars, this.scrub, this.ribbon, this.foot, this.badge, this.note, this.panel.legendBox].forEach(e => ['position', 'left', 'right', 'top', 'bottom', 'width', 'maxHeight'].forEach(k => e.style[k] = '')); this.note.style.display = ''; (this.panel.container.closest('.vt-pca-biplot-wrap') || this.panel.container).style.height = ''; }
    _showTop(n) { const a = this._arrowSet().slice().sort((x, y) => (y.r2 ?? y.mag) - (x.r2 ?? x.mag)).slice(0, n).map(x => x.f); this.hiddenF = new Set(Array.from({ length: (this.viz.feature_short || []).length }, (_, i) => i).filter(i => !a.includes(i))); }
    _curItem() { const tl = this._timeline(), it = this._items()[tl[this.tpos]]; return it || null; }
    _renderVars() {
        if (!this.viz) { this.vars.innerHTML = ''; return; }
        const names = this.viz.feature_short || [], it = this._curItem(), prev = this._items()[(this._timeline()[this.tpos] || 1) - 1];
        this.vars.innerHTML = '<div class="vh"><b>VARIABLES</b><span><button data-vact="all">all</button><button data-vact="top">top 8</button><button data-vact="none">none</button></span></div>' + (this.viz.feature_group ? '<div class="vgrp"><i class="a"></i>anchor book <i class="x"></i>cross-book block (multibook_v1)</div>' : '') + '<div class="vl">' +
            names.map((n, i) => { const z = it ? it.z[i] : 0, dz = it && prev && prev.z ? z - prev.z[i] : 0, hid = this.hiddenF.has(i);
                return `<div class="vr${hid ? ' off' : ''}${this.spot === i ? ' sp' : ''}${this.viz.feature_group && this.viz.feature_group[i] === 'cross_book' ? ' xb' : ''}" data-f="${i}" title="click name: highlight this variable and colour points by it - eye: show/hide its arrow"><button class="eye" aria-label="toggle ${n}">${hid ? '&#9675;' : '&#9679;'}</button><span class="nm">${n}</span>` +
                    `<span class="zb"><i style="width:${Math.min(100, Math.abs(z) / 3 * 100).toFixed(0)}%;${z < 0 ? 'right:50%;background:#4a8cff' : 'left:50%;background:#ffa838'}"></i></span><span class="z">${z >= 0 ? '+' : ''}${z.toFixed(1)}</span><span class="dz ${dz > 0.05 ? 'up' : dz < -0.05 ? 'dn' : ''}">${dz > 0.05 ? '&#9650;' : dz < -0.05 ? '&#9660;' : ''}</span></div>`; }).join('') + '</div>';
    }
    // ---- layout: reserve a plot rectangle so every point stays visible; boxes sit beside/below it, never over it
    _layoutChrome(w, h) {
        const mobile = w < 700, cont = this.panel.container.closest('.vt-pca-biplot-wrap') || this.panel.container, els = [this.bar, this.card, this.vars, this.scrub, this.ribbon, this.foot, this.badge, this.note, this.panel.legendBox];
        const clear = () => els.forEach(e => { ['position', 'left', 'right', 'top', 'bottom', 'width', 'maxHeight', 'display'].forEach(k => { if (k !== 'display') e.style[k] = ''; }); });
        this.hint.style.display = 'none';
        if (!mobile) {
            clear(); cont.style.height = '740px'; h = 740; const x0 = 285, x1 = w - 325, y0 = 78, y1 = h - 190;
            this.rect = { x0, x1, y0, y1 };
            Object.assign(this.vars.style, { position: 'absolute', left: '10px', top: '112px', width: '262px', maxHeight: Math.max(140, y1 - 112) + 'px' });
            const lg = this.panel.legendBox, top = lg.offsetTop + lg.offsetHeight + 8;
            Object.assign(this.card.style, { position: 'absolute', right: '10px', left: 'auto', top: top + 'px', width: '290px', maxHeight: Math.max(90, y1 - top) + 'px' });
        } else {
            cont.style.height = '1120px'; const H = 1120, x0 = 0, x1 = w, y0 = 112, y1 = 112 + Math.min(w, 400); this.rect = { x0, x1, y0, y1 }; let Y = y1 + 8;
            const place = (e, extra) => { e.style.position = 'absolute'; e.style.left = '8px'; e.style.right = '8px'; e.style.width = 'auto'; e.style.bottom = 'auto'; e.style.top = Y + 'px'; if (extra) Object.assign(e.style, extra); Y += e.offsetHeight + 8; };
            place(this.bar); place(this.panel.legendBox, { maxHeight: 'none' }); place(this.card, { maxHeight: 'none', display: this.card.style.display }); place(this.vars, { maxHeight: '300px' });
            place(this.scrub); place(this.ribbon); place(this.foot); if (this.badge.style.display !== 'none') { place(this.badge); } this.note.style.display = 'none';
            cont.style.height = Math.max(Y + 8, 760) + 'px';
        }
        this.off = [(this.rect.x0 + this.rect.x1) / 2 - w / 2, (this.rect.y0 + this.rect.y1) / 2 - h / 2];
    }
    pushLive(v, pcaLive) {
        if (!this.viz) return;
        this.live.push({ pca: pcaLive ? [pcaLive.pc1, pcaLive.pc2, pcaLive.pc3] : [0, 0, 0], tsne2: v.tsne2, tsne3: v.tsne3,
                         z: v.z, posterior: v.posterior, ood: v.out_of_distribution, conf: v.placement_confidence });
        if (this.live.length > 400) this.live.shift();
        this.pos.length = this._items().length;
        const k = this.pos.length - 1; if (!this.pos[k]) this.pos[k] = this._target(k);
        this._syncScrub();
        if (this.active) this.draw();
    }
    refresh() { if (!this.active || !this.viz) return; this._relayout(); this._startMorph(this.pos.slice(), null); }
    reset() { this.viz = null; this.live = []; this.pos = []; this.sel = null; this.tpos = null; this.stopPlay(); if (this.active) this.draw(); }

    // ------------------------------------------------------------ items & coordinate sets
    _items() { return this.viz ? this.viz.points.concat(this.live.map(l => ({ live: true, ...l }))) : []; }
    _nHist() { return this.viz ? this.viz.points.length : 0; }
    _raw(it, mode = this.mode, pair = this.pair) {
        if (mode === 'pca2d') { const c = [0, 1, 2].find(k => k !== pair[0] && k !== pair[1]); return [it.pca[pair[0]], it.pca[pair[1]], it.pca[c]]; }
        if (mode === 'pca3d') return [it.pca[0], it.pca[1], it.pca[2]];
        if (mode === 'tsne2d') return [it.tsne2[0], it.tsne2[1], 0];
        return [it.tsne3[0], it.tsne3[1], it.tsne3[2]];
    }
    _is3d(mode = this.mode) { return mode === 'tsne3d' || mode === 'pca3d'; }
    // px-per-world-unit at zoom 1
    _base(w, h) { return (Math.min(w, h) / (2 * this.cam.radius)) * 0.92; }
    // scale that fits every item of `mode` into the canvas; uniform on all axes so geometry is never distorted
    _fit(mode, pair, w, h) {
        let mx = 1e-9, my = 1e-9, mz = 1e-9;
        const items = this._items();
        for (const it of items) { const r = this._raw(it, mode, pair); mx = Math.max(mx, Math.abs(r[0])); my = Math.max(my, Math.abs(r[1])); mz = Math.max(mz, Math.abs(r[2])); }
        if (mode === 'pca2d' || mode === 'pca3d') {   // arrows must fit too
            const sc = (this.panel.staticData?.arrow_scale || 6) * this.panel.vectorSpread;
            for (const l of this.viz?.pca_meta?.loadings || []) {
                const a = mode === 'pca3d' ? [0, 1, 2] : [pair[0], pair[1]];
                a.forEach((k, j) => { const v = Math.abs(l[k] * sc); if (j === 0) mx = Math.max(mx, v); else if (j === 1) my = Math.max(my, v); else mz = Math.max(mz, v); });
            }
        }
        const k = this.panel.bubbleSpread;                       // headroom for the exploded view
        const spread = 1 + (k - 1) * 0.6;
        const R = this.rect || { x0: 0, x1: w, y0: 0, y1: h }, pw = R.x1 - R.x0, ph = R.y1 - R.y0;
        const pxPer = this._is3d(mode) ? (Math.min(pw, ph) * 0.36) / (Math.max(mx, my, mz) * spread)
                                       : Math.min((pw * 0.44) / (mx * spread), (ph * 0.44) / (my * spread));
        return pxPer / this._base(w, h);          // world units per raw unit
    }
    _relayout() {
        const r = this.panel.container.getBoundingClientRect();
        const w = Math.max(300, r.width), h = Math.max(240, r.height);
        this._layoutChrome(w, h);                               // may change the container height: measure again and settle
        const r2 = this.panel.container.getBoundingClientRect(), w2 = Math.max(300, r2.width), h2 = Math.max(240, r2.height);
        this._layoutChrome(w2, h2); this.size = { w: w2, h: h2 };
        this.k = this._fit(this.mode, this.pair, w2, h2);
        this._shiftCache = null;
    }
    // regime centroid in raw coords of the current mode: used for the exploded (spread) view
    _centroids() {
        if (this._shiftCache && this._shiftCache.key === this.mode + this.pair + this.live.length) return this._shiftCache.c;
        const sum = [0, 1, 2, 3, 4].map(() => [0, 0, 0, 0]);
        for (const p of this.viz?.points || []) { const r = this._raw(p), s = sum[p.dominant]; s[0] += r[0]; s[1] += r[1]; s[2] += r[2]; s[3]++; }
        const c = sum.map(s => s[3] ? [s[0] / s[3], s[1] / s[3], s[2] / s[3]] : [0, 0, 0]);
        this._shiftCache = { key: this.mode + this.pair + this.live.length, c };
        return c;
    }
    _dominantOf(it) { if (it.dominant !== undefined) return it.dominant; let b = 0; it.posterior.forEach((p, i) => { if (p > it.posterior[b]) b = i; }); return b; }
    _target(i, mode = this.mode, pair = this.pair, k = this.k) {
        const it = this._items()[i]; if (!it) return [0, 0, 0];
        const r = this._raw(it, mode, pair), c = this._centroids()[this._dominantOf(it)], sp = this.panel.bubbleSpread - 1;
        const out = [(r[0] + c[0] * sp) * k, (r[1] + c[1] * sp) * k, (r[2] + c[2] * sp) * k];
        return out;
    }
    _snapPositions() { this.pos = this._items().map((_, i) => this._target(i)); }

    // ------------------------------------------------------------ mode switching & morphing
    enter(mode, fromPca3d) {
        const prevMode = this.mode;
        this.mode = mode;
        const wasActive = this.active;
        this.active = true;
        this.canvas.style.display = 'block';
        this.bar.style.display = 'flex'; this.modebar.style.display = 'flex'; this.vars.style.display = 'block'; this.badge.style.display = 'block'; this.foot.style.display = 'block';
        this.scrub.style.display = 'flex'; this.ribbon.style.display = 'block'; this.card.style.display = 'block'; this.hint.style.display = 'block';
        this.bar.querySelector('[data-grp=pair]').style.display = mode === 'pca2d' ? 'inline-flex' : 'none';
        this._relayout();
        const flat = !this._is3d();
        if (!wasActive && fromPca3d) {
            // start from the existing 3D view's coordinates and camera, then morph to this view
            this.cam.yaw = this.panel.cam.yaw; this.cam.pitch = this.panel.cam.pitch; this.cam.persp = 1; this.cam.roll = 0;
            const k3 = this._fit('pca3d', this.pair, this.size.w, this.size.h);
            this._startMorph(this._items().map((_, i) => this._target(i, 'pca3d', this.pair, k3)), flat ? { yaw: 0, pitch: 0 } : { yaw: 0.38, pitch: 0.40 });
        } else if (!wasActive) {
            this._snapPositions(); this._camTo(flat ? { yaw: 0, pitch: 0 } : { yaw: 0.38, pitch: 0.4 }, true);
        } else {
            this._startMorph(this.pos.slice(), flat ? { yaw: 0, pitch: 0 } : (this._is3d(prevMode) ? null : { yaw: 0.38, pitch: 0.40 }));
        }
        this.unlocked = false;
        this._renderFoot(); this._syncScrub(); this._updateChromeState(); this._renderVars(); this._layoutChrome(this.size.w, this.size.h);
        this.draw();
    }
    // morph back to the existing PCA 3D view's coordinates, then hand control over to it
    leave(done) {
        if (!this.active) { done && done(); return; }
        const k3 = this._fit('pca3d', this.pair, this.size.w, this.size.h);
        this.mode = 'pca3d';
        this._startMorph(this.pos.slice(), { yaw: this.panel.cam.yaw, pitch: this.panel.cam.pitch }, () => {
            this.active = false; this.canvas.style.display = 'none';
            [this.bar, this.modebar, this.vars, this.badge, this.foot, this.scrub, this.ribbon, this.card, this.hint].forEach(e => e.style.display = 'none');
            this.mode = 'pca2d'; this.stopPlay(); this.note.textContent = ''; this._clearChrome(); done && done();
        }, k3);
    }
    setPair(pair) {
        this.pair = pair; this._relayout();
        this._startMorph(this.pos.slice(), null);
        this._renderFoot(); this.draw();
    }
    flatten() { this.unlocked = false; this._camTo({ yaw: 0, pitch: 0 }); this.cam.roll = 0; }
    _camTo(t, snap) {
        if (snap) { this.cam.yaw = t.yaw; this.cam.pitch = t.pitch; this._updatePersp(); return; }
        this._startMorph(this.pos.slice(), t);
    }
    _updatePersp() { this.cam.persp = Math.min(1, Math.hypot(this.cam.yaw, this.cam.pitch) / 0.3); }
    _startMorph(from, cam, done, kOverride) {
        const to = this._items().map((_, i) => this._target(i, this.mode, this.pair, kOverride || this.k));
        if (from.length !== to.length) from = to.map((p, i) => from[i] || p);
        this.morph = { t0: performance.now(), dur: 520, from, to, cam0: { yaw: this.cam.yaw, pitch: this.cam.pitch, roll: this.cam.roll, persp: this.cam.persp }, cam1: cam, done };
        this._tick();
    }
    _tick() {
        if (this._raf) return;
        const step = () => {
            this._raf = 0;
            const m = this.morph; if (!m) return;
            const t = Math.min(1, (performance.now() - m.t0) / m.dur), e = vtEase(t);
            this.pos = m.to.map((p, i) => { const f = m.from[i] || p; return [f[0] + (p[0] - f[0]) * e, f[1] + (p[1] - f[1]) * e, f[2] + (p[2] - f[2]) * e]; });
            if (m.cam1) {
                this.cam.yaw = m.cam0.yaw + (m.cam1.yaw - m.cam0.yaw) * e; this.cam.pitch = m.cam0.pitch + (m.cam1.pitch - m.cam0.pitch) * e;
                this.cam.roll = m.cam0.roll * (1 - (m.cam1.yaw === 0 && m.cam1.pitch === 0 ? e : 0));
                this.cam.persp = this.morph.cam1.yaw === 0 && this.morph.cam1.pitch === 0 ? m.cam0.persp * (1 - e) : (this.mode === 'pca3d' ? 1 : this.cam.persp);
            }
            this.morphT = e; this.draw();
            if (t < 1) { this._raf = requestAnimationFrame(step); }
            else { this.morph = null; this.morphT = 1; this._snapPositions(); this._updatePersp(); const d = m.done; this.draw(); d && d(); }
        };
        this._raf = requestAnimationFrame(step);
    }
    _updateChromeState() {
        this.hint.textContent = this.viz ? 'toolbar: zoom / pan / reset / PNG · legend: click to hide a regime, double-click to isolate · drag: tilt out of the plane (Flatten returns) · shift-drag or two-finger twist: spin · wheel / pinch: zoom · double-tap: reset'
                                          : 'Waiting for the embedding: it is computed with each batch refit.';
    }

    // ------------------------------------------------------------ pointer interaction
    _bindPointer() {
        const c = this.canvas, pts = new Map();
        let down = null, moved = 0, pinch = null, lastTap = 0;
        const loc = (e) => { const r = c.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
        c.style.touchAction = 'none';
        c.addEventListener('pointerdown', (e) => {
            c.setPointerCapture?.(e.pointerId); pts.set(e.pointerId, loc(e));
            if (pts.size === 1) { down = { t: performance.now(), p: loc(e) }; moved = 0; }
            if (pts.size === 2) { const [a, b] = [...pts.values()]; pinch = { d: Math.hypot(a[0] - b[0], a[1] - b[1]), ang: Math.atan2(b[1] - a[1], b[0] - a[0]), zoom: this.cam.zoom }; }
        });
        c.addEventListener('pointermove', (e) => {
            if (!pts.has(e.pointerId)) return;
            const prev = pts.get(e.pointerId), cur = loc(e); pts.set(e.pointerId, cur);
            if (pts.size === 1) {
                const dx = cur[0] - prev[0], dy = cur[1] - prev[1]; moved += Math.abs(dx) + Math.abs(dy);
                if (this.panMode) { this.pan[0] += dx; this.pan[1] += dy; this.draw(); return; }
                if (e.shiftKey) { this.cam.roll += dx * 0.01; this.draw(); return; }                 // in-plane spin
                const flatMode = !this._is3d();
                if (flatMode && !this.unlocked) { if (moved > 14) this.unlocked = true; else return; } // deliberate drag to tilt
                this.cam.yaw += dx * 0.008; this.cam.pitch = Math.max(-1.45, Math.min(1.45, this.cam.pitch + dy * 0.008));
                this._updatePersp(); this.draw();
            } else if (pts.size === 2 && pinch) {
                const [a, b] = [...pts.values()];
                const d = Math.hypot(a[0] - b[0], a[1] - b[1]), ang = Math.atan2(b[1] - a[1], b[0] - a[0]);
                this.cam.zoom = Math.max(0.35, Math.min(6, pinch.zoom * d / (pinch.d || 1)));
                this.cam.roll += (ang - pinch.ang); pinch.ang = ang; moved += 10; this.draw();      // two-finger twist spins the plane
            }
        });
        const end = (e) => {
            const single = pts.size === 1; pts.delete(e.pointerId); if (pts.size < 2) pinch = null;
            if (single && down && moved < 6 && performance.now() - down.t < 400) {
                const now = performance.now();
                if (now - lastTap < 350) { lastTap = 0; this.cam.roll = 0; this.flatten(); this.sel = null; this._renderCard(); this.draw(); }
                else { lastTap = now; this._tap(down.p[0], down.p[1]); }
            }
        };
        c.addEventListener('pointerup', end); c.addEventListener('pointercancel', end);
        c.addEventListener('wheel', (e) => { e.preventDefault(); this.cam.zoom = Math.max(0.35, Math.min(6, this.cam.zoom * (e.deltaY < 0 ? 1.1 : 1 / 1.1))); this.draw(); }, { passive: false });
        window.addEventListener('resize', () => { if (this.active) { this._relayout(); this._snapPositions(); this.draw(); } });
    }
    _tap(x, y) {
        let best = null, bd = 18 * 18;
        for (const h of this._hit || []) { const d = (h.sx - x) ** 2 + (h.sy - y) ** 2; if (d < bd) { bd = d; best = h; } }
        this.sel = best && (best.type === 'pt' || best.type === 'live') ? { type: best.type, i: best.i } : null;
        if (best && best.type === 'feature') { this.spot = best.f; this.bar.querySelector('[data-act=colorby]').value = String(best.f); }
        this.decomp = 0; this._animDecomp();
        this._renderCard(); this.draw();
    }
    _animDecomp() {
        const t0 = performance.now();
        const step = () => { this.decomp = Math.min(1, (performance.now() - t0) / 700); this.draw(); if (this.decomp < 1 && this.sel) requestAnimationFrame(step); };
        requestAnimationFrame(step);
    }

    // ------------------------------------------------------------ timeline / replay / ribbon
    _timeline() { // indexes into _items(): last 40 historical days then every live entry
        const n = this._nHist(), start = Math.max(0, n - 40), out = [];
        for (let i = start; i < n; i++) out.push(i);
        for (let j = 0; j < this.live.length; j++) out.push(n + j);
        return out;
    }
    _syncScrub() {
        const tl = this._timeline(), inp = this.scrub.querySelector('input');
        inp.max = String(Math.max(0, tl.length - 1));
        if (this.tpos === null || this.tpos > tl.length - 1) this.tpos = tl.length - 1;
        inp.value = String(Math.max(0, this.tpos));
        const idx = tl[this.tpos], n = this._nHist();
        this.scrub.querySelector('.lbl').textContent = idx === undefined ? '' : (idx >= n ? `live +${idx - n + 1}` : `day ${idx + 1} of ${n}`);
        this._renderRibbon(); this._renderVars();
    }
    togglePlay() { if (this.playing) { this.stopPlay(); return; } const tl = this._timeline(); if (tl.length < 2) return;
        this.tpos = 0; this.scrub.querySelector('[data-act=play]').textContent = 'Pause';
        this.playing = setInterval(() => { this.tpos++; if (this.tpos >= tl.length - 1) { this.tpos = tl.length - 1; this.stopPlay(); } this._syncScrub(); this.draw(); }, 170); }
    stopPlay() { if (this.playing) { clearInterval(this.playing); this.playing = null; } this.scrub.querySelector('[data-act=play]').textContent = 'Replay'; }
    _deltaZ(a, b) { // ranked feature changes between two items
        const A = this._items()[a], B = this._items()[b]; if (!A || !B) return [];
        return (this.viz.feature_short || []).map((n, f) => ({ f, n, d: B.z[f] - A.z[f] })).sort((x, y) => Math.abs(y.d) - Math.abs(x.d));
    }
    _renderRibbon() {
        if (!this.viz) { this.ribbon.innerHTML = ''; return; }
        const tl = this._timeline(), i1 = tl[this.tpos], i0 = tl[this.tpos - 1] ?? i1 - 1;
        const items = this._items(); if (i1 === undefined || !items[i0]) { this.ribbon.innerHTML = '<span class="t">variable shifts</span> needs two days'; return; }
        const top = this._deltaZ(i0, i1).slice(0, 5);
        const rn = this.viz.regime_names, cl = this.viz.regime_colors;
        const p0 = items[i0].posterior, p1 = items[i1].posterior;
        const lead = p1.indexOf(Math.max(...p1)), dp = (p1[lead] - p0[lead]) * 100;
        this.ribbon.innerHTML = `<span class="t">variable shifts</span>` + top.map(x =>
            `<span class="chip" data-f="${x.f}"><b>${x.n}</b> <i class="${x.d >= 0 ? 'up' : 'dn'}">${x.d >= 0 ? '+' : ''}${x.d.toFixed(2)}z</i></span>`).join('') +
            `<span class="chip rg"><b style="color:${cl[lead]}">${rn[lead]}</b> <i class="${dp >= 0 ? 'up' : 'dn'}">${dp >= 0 ? '+' : ''}${dp.toFixed(0)} pts</i></span>`;
        this.ribbon.querySelectorAll('[data-f]').forEach(e => e.addEventListener('click', () => { this.spot = +e.dataset.f; this.bar.querySelector('[data-act=colorby]').value = String(this.spot); this.draw(); }));
    }

    // ------------------------------------------------------------ side card: selection, decomposition, displacement
    _renderCard() {
        const v = this.viz; if (!v) return;
        const items = this._items();
        let html = '';
        if (this.sel) {
            const idx = this.sel.type === 'live' ? this._nHist() + this.sel.i : this.sel.i, it = items[idx];
            if (!it) { this.card.innerHTML = ''; return; }
            const dom = this._dominantOf(it), cl = v.regime_colors[dom];
            const title = it.live ? `Live read +${idx - this._nHist() + 1}` : `Day ${idx + 1} of ${this._nHist()}`;
            html += `<div class="hd">${title}</div><div class="rg" style="color:${cl}">${v.regime_names[dom]}</div>`;
            html += it.posterior.map((p, r) => `<div class="pr"><span style="color:${v.regime_colors[r]}">${v.regime_names[r].split(' ')[0]}</span><i>${Math.round(p * 100)}%</i></div>`).join('');
            if (this.mode === 'pca2d') {
                const L = v.pca_meta.loadings, a = this.pair[0], b = this.pair[1];
                const rows = v.feature_short.map((n, f) => ({ n, f, x: it.z[f] * L[f][a], y: it.z[f] * L[f][b] })).sort((p, q) => Math.hypot(q.x, q.y) - Math.hypot(p.x, p.y)).slice(0, 6);
                html += `<div class="hd sub">What is pushing this point (exact: z x loading)</div>` + rows.map(r => `<div class="ct"><span>${r.n}</span><i>${r.x >= 0 ? '+' : ''}${r.x.toFixed(2)}, ${r.y >= 0 ? '+' : ''}${r.y.toFixed(2)}</i></div>`).join('');
            }
            const prev = idx > 0 ? idx - 1 : null;
            if (prev !== null) {
                const d = this._deltaZ(idx >= this._nHist() ? (idx === this._nHist() ? this._nHist() - 1 : idx - 1) : prev, idx).slice(0, 5);
                html += `<div class="hd sub">Change vs previous day (z)</div>` + d.map(x => `<div class="ct"><span>${x.n}</span><i class="${x.d >= 0 ? 'up' : 'dn'}">${x.d >= 0 ? '+' : ''}${x.d.toFixed(2)}</i></div>`).join('');
            }
            if (this.mode !== 'pca2d') html += `<div class="note">t-SNE proximity is suggestive; axes and arrows have no direct meaning.</div>`;
        } else {
            html = `<div class="hd">Tap a point</div><div class="note">Select a day to see its regime split, what is pushing it, and what changed since the day before. Tap a feature chip to spotlight it.</div>`;
        }
        this.card.innerHTML = html;
    }

    // ------------------------------------------------------------ footer / honesty labels
    _renderFoot() {
        const v = this.viz; if (!v) { this.foot.innerHTML = ''; return; }
        if (this.mode === 'pca2d') {
            const e = v.pca_meta.explained_variance_ratio, cum = v.pca_meta.cumulative;
            this.badge.style.display = 'none';
            this.foot.innerHTML = `explained variance: PC1 <b>${(e[0] * 100).toFixed(0)}%</b> PC2 <b>${(e[1] * 100).toFixed(0)}%</b> PC3 <b>${(e[2] * 100).toFixed(0)}%</b> · cumulative <b class="${cum < 0.5 ? 'warn' : ''}">${(cum * 100).toFixed(0)}%</b>${cum < 0.5 ? ' (low: proximity is suggestive only)' : ''}`;
        } else {
            const m = v.tsne_meta, d3 = this.mode === 'tsne3d';
            const tw = d3 ? m.trustworthiness_3d : m.trustworthiness, ct = d3 ? m.continuity_3d : m.continuity, pu = d3 ? m.knn_regime_purity_3d : m.knn_regime_purity;
            this.badge.style.display = 'block';
            this.badge.textContent = 'Visual aid only. Regime classification is computed in the original 17-feature space.';
            this.foot.innerHTML = `trustworthiness <b class="${tw < 0.85 ? 'warn' : ''}">${tw.toFixed(2)}</b>${tw < 0.85 ? ' <span class="warn">(low: layout unreliable)</span>' : ''} · continuity <b>${ct.toFixed(2)}</b> · kNN regime purity <b>${pu.toFixed(2)}</b> · perplexity ${m.perplexity.toFixed(0)} · seed ${m.seed} · KL ${(d3 ? m.kl_divergence_3d : m.kl_divergence).toFixed(2)} · batch ${v.batch_id}<br><span class="caveat">Cluster size and inter-cluster distance are not meaningful in t-SNE. Proximity is suggestive, not definitive.</span>`;
        }
    }

    // ------------------------------------------------------------ drawing
    _colorOf(it, idx) {
        if (this.spot >= 0) return vtDiverge(it.z[this.spot]);
        if (this.spot === -2) return vtPlasma((Math.max(...it.posterior) - 0.2) / 0.8);
        if (this.spot === -3) return vtPlasma(idx / Math.max(1, this._items().length - 1));
        const cl = this.viz.regime_colors.map(vtHexRgb), p = it.posterior;
        let r = 0, g = 0, b = 0; p.forEach((w, i) => { r += cl[i][0] * w; g += cl[i][1] * w; b += cl[i][2] * w; });
        return [r, g, b];
    }
    draw() {
        if (!this.active || !this.viz) return;
        const { ctx, w, h } = vtSizeCanvas(this.canvas, this.panel.container);
        if (!this.size || Math.abs(this.size.w - w) > 1 || Math.abs(this.size.h - h) > 1) { this._relayout(); if (!this.morph) this._snapPositions(); }
        ctx.fillStyle = '#000'; ctx.fillRect(0, 0, w, h);
        const P = (x, y, z) => { const q = this.cam.project(x, y, z, w, h); q.sx += this.pan[0] + this.off[0]; q.sy += this.pan[1] + this.off[1]; return q; };
        this._hit = []; this._labels = [];
        const flatT2 = this.mode === 'tsne2d' ? Math.min(1, Math.hypot(this.cam.yaw, this.cam.pitch) / 0.5) : 0;
        const items = this._items(), n = this._nHist();
        const layerZ = (i) => flatT2 ? ((i < n ? i / Math.max(1, n - 1) : 1) - 0.5) * (Math.min(w, h) * 0.5 / this._base(w, h)) * flatT2 : 0;
        const W = (i) => { const p = this.pos[i] || [0, 0, 0]; return [p[0], p[1], p[2] + layerZ(i)]; };
        const settle = this.morph ? Math.pow(this.morphT || 0, 2) : 1;     // arrows / shapes fade in once points have landed

        this._drawGrid(ctx, P, w, h, flatT2);
        if (this.panel.showEllipsoids) this._drawShapes(ctx, P, settle);
        // points, far to near
        const order = items.map((_, i) => i).filter(i => !items[i].live).map(i => ({ i, s: P(...W(i)) })).sort((a, b) => a.s.z - b.s.z);
        const tl = this._timeline(), cur = tl[this.tpos], tail = new Set(tl.slice(Math.max(0, this.tpos - 6), this.tpos + 1));
        for (const { i, s } of order) {
            if (this.hidden.has(items[i].dominant)) continue;
            const it = items[i], col = this._colorOf(it, i), conf = Math.max(...it.posterior);
            const inTail = tail.has(i), dim = this.spot >= 0 ? 1 : 1;
            const a = (0.30 + 0.62 * conf) * (this.sel && !(this.sel.type === 'pt' && this.sel.i === i) ? 0.85 : 1);
            const rad = i === cur ? 5.2 : inTail ? 4 : 3;
            ctx.beginPath(); ctx.arc(s.sx, s.sy, rad, 0, Math.PI * 2); ctx.fillStyle = vtRgb(col, a); ctx.fill();
            // uncertainty ring: runner-up regime colour when the posterior is genuinely split
            const sorted = it.posterior.map((p, r) => [p, r]).sort((x, y) => y[0] - x[0]);
            if (this.spot === -1 && sorted[1][0] > 0.25) { ctx.beginPath(); ctx.arc(s.sx, s.sy, rad + 1.2, 0, Math.PI * 2); ctx.strokeStyle = vtRgb(vtHexRgb(this.viz.regime_colors[sorted[1][1]]), 0.8); ctx.lineWidth = 1; ctx.stroke(); }
            if (inTail && i !== cur) { ctx.beginPath(); ctx.arc(s.sx, s.sy, rad + 2.5, 0, Math.PI * 2); ctx.strokeStyle = `rgba(255,255,255,${0.12 + 0.08 * (6 - (this.tpos - tl.indexOf(i)))})`; ctx.stroke(); }
            this._hit.push({ sx: s.sx, sy: s.sy, type: 'pt', i });
        }
        // comet tail line through the replay window
        if (tail.size > 1) { ctx.strokeStyle = 'rgba(255,255,255,0.35)'; ctx.lineWidth = 1.2; ctx.beginPath();
            [...tail].forEach((i, k) => { const s = P(...W(i)); k ? ctx.lineTo(s.sx, s.sy) : ctx.moveTo(s.sx, s.sy); }); ctx.stroke(); }
        if (settle > 0.05) { this._drawArrows(ctx, P, settle); }
        this._drawLive(ctx, P, W, w, h);
        if (this.sel) this._drawSelection(ctx, P, W);
        this._drawColorbar(ctx, w, h);
        this._placeLabels(ctx, w, h);
        this.note.textContent = this._layerNote || (this.mode !== 'pca2d' ? 'Arrows are estimated feature directions (R2 = how well the feature varies smoothly across the map). They are not loadings.' : '');
    }
    _drawColorbar(ctx, w, h) {
        if (this.spot !== -2 && this.spot !== -3) return;
        const x = w - 22, y0 = 96, hh = 120; for (let i = 0; i < hh; i++) { ctx.fillStyle = vtRgb(vtPlasma(1 - i / hh), 1); ctx.fillRect(x, y0 + i, 9, 1.5); }
        ctx.font = '10px monospace'; ctx.fillStyle = 'rgba(190,198,210,.85)'; ctx.textAlign = 'right';
        const top = this.spot === -2 ? '1.0' : 'today', bot = this.spot === -2 ? '0.2' : 'oldest'; ctx.fillText(top, x - 3, y0 + 8); ctx.fillText(bot, x - 3, y0 + hh); ctx.fillText(this.spot === -2 ? 'confidence' : 'time', x - 3, y0 - 6); ctx.textAlign = 'left';
    }
    _drawGrid(ctx, P, w, h, layered) {
        const half = Math.min(w, h) * 0.46 / this._base(w, h) / Math.max(0.35, this.cam.zoom) * Math.max(0.35, this.cam.zoom); // world half-extent of the plane
        const ext = (this._is3d() ? 0.95 : 1.55) * half * (w / Math.min(w, h) > 1.4 ? 1.55 : 1);
        ctx.lineWidth = 1;
        for (let k = -4; k <= 4; k++) {
            const t = k / 4 * ext;
            ctx.strokeStyle = k === 0 ? 'rgba(160,170,185,0.38)' : 'rgba(120,130,145,0.12)';
            let a = P(-ext, t, 0), b = P(ext, t, 0); ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke();
            a = P(t, -ext, 0); b = P(t, ext, 0); ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke();
        }
        // numeric tick labels in data units (world = raw * k)
        if (!this._is3d() && this.k > 0) {
            ctx.font = '9px monospace'; ctx.fillStyle = 'rgba(150,160,175,0.7)'; ctx.textAlign = 'center';
            const tk = vtNiceTicks(-ext / this.k, ext / this.k, 8);
            for (const v of tk.out) { if (Math.abs(v) < tk.step * 0.01) continue; const lab = Math.abs(v) < 10 ? v.toFixed(tk.step < 1 ? 1 : 0) : v.toFixed(0); const a = P(v * this.k, 0, 0), b = P(0, v * this.k, 0);
                ctx.fillText(lab, a.sx, a.sy + 12); ctx.textAlign = 'right'; ctx.fillText(lab, b.sx - 5, b.sy + 3); ctx.textAlign = 'center'; }
            ctx.textAlign = 'left';
        }
        // axis labels (never implied to be loadings in t-SNE)
        const lab = this.mode === 'pca2d' ? this.pair.map(k => `PC${k + 1} (${(this.viz.pca_meta.explained_variance_ratio[k] * 100).toFixed(0)}%)`)
            : [this.mode === 'tsne3d' ? 't-SNE 1' : 't-SNE 1', 't-SNE 2'];
        ctx.font = '11px monospace'; ctx.fillStyle = 'rgba(190,198,210,0.85)';
        const ax = P(ext * 0.98, 0, 0), ay = P(0, ext * 0.98, 0);
        ctx.textAlign = 'right'; ctx.fillText(lab[0], ax.sx - 4, ax.sy - 6);
        ctx.textAlign = 'left'; ctx.fillText(lab[1], ay.sx + 6, ay.sy + 12); ctx.textAlign = 'left';
        if (this.mode === 'tsne3d') { const az = P(0, 0, ext * 0.9); ctx.fillText('t-SNE 3', az.sx + 4, az.sy); }
        this._layerNote = layered > 0.1 ? 'Tilted t-SNE 2D: height = time (oldest at the bottom). It is a layering aid, not a third embedding axis.' : '';
    }
    // ----- regime shapes: projected GMM ellipses (PCA) or KDE 80%-mass regions (t-SNE)
    _drawShapes(ctx, P, settle) {
        const v = this.viz, sp = this.panel.bubbleSpread - 1, cent = this._centroids();
        if (this.mode === 'pca2d') {
            const [a, b] = this.pair, c = [0, 1, 2].find(k => k !== a && k !== b), k = this.k;
            for (const g of v.pca_meta.gmm_proj) {
                const r = g.regime; if (this.hidden.has(r)) continue; const col = vtHexRgb(v.regime_colors[r]);
                const s2 = [[g.cov[a][a], g.cov[a][b]], [g.cov[b][a], g.cov[b][b]]];
                const tr = s2[0][0] + s2[1][1], det = s2[0][0] * s2[1][1] - s2[0][1] * s2[1][0], disc = Math.sqrt(Math.max(0, tr * tr / 4 - det));
                const l1 = tr / 2 + disc, l2 = Math.max(1e-12, tr / 2 - disc), th = 0.5 * Math.atan2(2 * s2[0][1], s2[0][0] - s2[1][1]);
                const rs = this.panel.fittedShape ? [Math.sqrt(l1) * 1.5, Math.sqrt(l2) * 1.5] : (() => { const q = Math.sqrt(Math.sqrt(l1 * l2)) * 1.5; return [q, q]; })();
                const m = [g.mu[a] + cent[r][0] * sp, g.mu[b] + cent[r][1] * sp, g.mu[c] + cent[r][2] * sp];
                const pts = []; for (let t = 0; t <= 48; t++) { const u = t / 48 * Math.PI * 2, x = rs[0] * Math.cos(u), y = rs[1] * Math.sin(u);
                    pts.push(P((m[0] + x * Math.cos(th) - y * Math.sin(th)) * k, (m[1] + x * Math.sin(th) + y * Math.cos(th)) * k, m[2] * k)); }
                ctx.beginPath(); pts.forEach((p, i) => i ? ctx.lineTo(p.sx, p.sy) : ctx.moveTo(p.sx, p.sy)); ctx.closePath();
                ctx.fillStyle = vtRgb(col, 0.10 * settle); ctx.fill(); ctx.strokeStyle = vtRgb(col, 0.55 * settle); ctx.lineWidth = 1; ctx.stroke();
                const top = pts.reduce((m2, p) => p.sy < m2.sy ? p : m2, pts[0]);
                this._labels.push({ x: top.sx, y: top.sy - 4, text: v.regime_names[r], color: v.regime_colors[r], pri: 3, bold: true });
            }
        } else if (this.mode === 'tsne2d') {
            const kde = this._kde(); if (!kde) return;
            for (const r of Object.keys(kde)) {
                if (this.hidden.has(+r)) continue;
                const reg = kde[r], col = vtHexRgb(v.regime_colors[r]), k = this.k;
                const s0 = [cent[r][0] * sp, cent[r][1] * sp];
                ctx.fillStyle = vtRgb(col, 0.11 * settle);
                for (const q of reg.cells) { const A = P((q[0] + s0[0]) * k, (q[1] + s0[1]) * k, 0), B = P((q[2] + s0[0]) * k, (q[1] + s0[1]) * k, 0), C = P((q[2] + s0[0]) * k, (q[3] + s0[1]) * k, 0), D = P((q[0] + s0[0]) * k, (q[3] + s0[1]) * k, 0);
                    ctx.beginPath(); ctx.moveTo(A.sx, A.sy); ctx.lineTo(B.sx, B.sy); ctx.lineTo(C.sx, C.sy); ctx.lineTo(D.sx, D.sy); ctx.closePath(); ctx.fill(); }
                ctx.strokeStyle = vtRgb(col, 0.5 * settle); ctx.lineWidth = 1; ctx.beginPath();
                for (const e of reg.edges) { const A = P((e[0] + s0[0]) * k, (e[1] + s0[1]) * k, 0), B = P((e[2] + s0[0]) * k, (e[3] + s0[1]) * k, 0); ctx.moveTo(A.sx, A.sy); ctx.lineTo(B.sx, B.sy); }
                ctx.stroke();
                const lp = P((reg.cx + s0[0]) * k, (reg.top + s0[1]) * k, 0);
                this._labels.push({ x: lp.sx, y: lp.sy - 4, text: v.regime_names[r], color: v.regime_colors[r], pri: 3, bold: true });
            }
        } else { // tsne3d: no honest 3D region from points alone, so label the regime medians
            const k = this.k; const med = [0, 1, 2, 3, 4].map(r => { const ps = v.points.filter(p => p.dominant === r); if (ps.length < 3) return null;
                const m = [0, 1, 2].map(c => { const s = ps.map(p => p.tsne3[c]).sort((a, b) => a - b); return s[s.length >> 1]; }); return m; });
            med.forEach((m, r) => { if (!m) return; const p = P((m[0] + cent[r][0] * sp) * k, (m[1] + cent[r][1] * sp) * k, (m[2] + cent[r][2] * sp) * k);
                this._labels.push({ x: p.sx, y: p.sy - 4, text: v.regime_names[r], color: v.regime_colors[r], pri: 3, bold: true }); });
        }
    }
    // 80%-mass kernel density region per dominant regime (display geometry computed from the points themselves)
    _kde() {
        if (this._kdeCache) return this._kdeCache;
        const v = this.viz, out = {}, G = 44;
        for (let r = 0; r < 5; r++) {
            const ps = v.points.filter(p => p.dominant === r).map(p => p.tsne2); if (ps.length < 6) continue;
            const xs = ps.map(p => p[0]), ys = ps.map(p => p[1]);
            const sd = (a) => { const m = a.reduce((s, x) => s + x, 0) / a.length; return Math.sqrt(a.reduce((s, x) => s + (x - m) ** 2, 0) / a.length); };
            const hh = Math.max(0.9 * Math.pow(ps.length, -0.2) * (sd(xs) + sd(ys)) / 2, 1e-6);
            const x0 = Math.min(...xs) - 3 * hh, x1 = Math.max(...xs) + 3 * hh, y0 = Math.min(...ys) - 3 * hh, y1 = Math.max(...ys) + 3 * hh;
            const dx = (x1 - x0) / G, dy = (y1 - y0) / G, dens = [];
            let tot = 0;
            for (let i = 0; i < G; i++) for (let j = 0; j < G; j++) {
                const cx = x0 + (i + 0.5) * dx, cy = y0 + (j + 0.5) * dy; let d = 0;
                for (const p of ps) d += Math.exp(-((cx - p[0]) ** 2 + (cy - p[1]) ** 2) / (2 * hh * hh));
                dens.push({ i, j, d }); tot += d;
            }
            const sorted = dens.slice().sort((a, b) => b.d - a.d); let acc = 0; const keep = new Set();
            for (const c of sorted) { keep.add(c.i * G + c.j); acc += c.d; if (acc >= 0.8 * tot) break; }
            const cells = [], edges = []; let top = -1e9, cx = 0, cn = 0;
            for (const key of keep) {
                const i = Math.floor(key / G), j = key % G, ax = x0 + i * dx, ay = y0 + j * dy;
                cells.push([ax, ay, ax + dx, ay + dy]); if (ay + dy > top) top = ay + dy; cx += ax + dx / 2; cn++;
                if (!keep.has((i - 1) * G + j)) edges.push([ax, ay, ax, ay + dy]); if (!keep.has((i + 1) * G + j)) edges.push([ax + dx, ay, ax + dx, ay + dy]);
                if (!keep.has(i * G + j - 1)) edges.push([ax, ay, ax + dx, ay]); if (!keep.has(i * G + j + 1)) edges.push([ax, ay + dy, ax + dx, ay + dy]);
            }
            out[r] = { cells, edges, top, cx: cx / cn };
        }
        return (this._kdeCache = out);
    }
    // ----- feature arrows: real loadings (PCA) or estimated directions with R2 (t-SNE)
    _arrowSet() {
        const v = this.viz, k = this.k, vs = this.panel.vectorSpread, out = [];
        if (this.mode === 'pca2d') {
            const sc = (this.panel.staticData?.arrow_scale || 6) * vs, [a, b] = this.pair, c = [0, 1, 2].find(q => q !== a && q !== b);
            v.pca_meta.loadings.forEach((l, f) => { const tip = [l[a] * sc, l[b] * sc, l[c] * sc]; out.push({ f, tip: tip.map(x => x * k), mag: Math.hypot(tip[0], tip[1]), r2: null }); });
        } else {
            const d3 = this.mode === 'tsne3d';
            let ext = 0; for (const p of v.points) { const r = d3 ? p.tsne3 : p.tsne2; ext = Math.max(ext, Math.hypot(...r)); }
            v.tsne_meta.feature_gradients.forEach((g, f) => { const dir = d3 ? g.dir3 : g.dir2, r2 = d3 ? g.r2_3d : g.r2; const L = ext * 0.5 * r2 * vs;
                out.push({ f, tip: [dir[0] * L * k, dir[1] * L * k, (dir[2] || 0) * L * k], mag: L, r2 }); });
        }
        return out;
    }
    _drawArrows(ctx, P, settle) {
        const arrows = this._arrowSet(), o = P(0, 0, 0);
        const ranked = arrows.slice().sort((a, b) => (b.r2 ?? b.mag) - (a.r2 ?? a.mag));
        const label = new Set((this.showAll ? ranked : ranked.slice(0, 8)).filter(a => a.r2 === null || a.r2 >= (this.showAll ? 0 : 0.15)).map(a => a.f));
        for (const a of arrows) {
            const faded = a.r2 !== null && a.r2 < 0.15 && !this.showAll;        // low R2: the direction is not trustworthy, hide it
            if (faded) continue;
            if (this.hiddenF.has(a.f) && this.spot !== a.f) continue;
            const spotted = this.spot === a.f, dim = this.spot >= 0 && !spotted;
            const alpha = (a.r2 === null ? 0.62 : 0.25 + 0.6 * a.r2) * settle * (dim ? 0.18 : 1);
            const t = P(...a.tip);
            const xb = this.viz.feature_group && this.viz.feature_group[a.f] === 'cross_book', rgb = xb ? '110,226,196' : '230,235,245';   // cross-book block arrows are teal, anchor arrows white
            ctx.strokeStyle = spotted ? `rgba(255,196,92,${alpha + 0.3})` : `rgba(${rgb},${alpha})`; ctx.lineWidth = spotted ? 2.2 : 1.2;
            ctx.beginPath(); ctx.moveTo(o.sx, o.sy); ctx.lineTo(t.sx, t.sy); ctx.stroke();
            this.panel._arrowHead(ctx, o, t, spotted ? 'rgba(255,196,92,0.95)' : `rgba(${rgb},${alpha + 0.05})`);
            if (label.has(a.f) || spotted) {
                const nm = this.viz.feature_short[a.f];
                this._labels.push({ x: t.sx, y: t.sy, text: a.r2 === null ? nm : `${nm} (R2 ${a.r2.toFixed(2)})`, color: spotted ? '#ffc45c' : (xb ? '#8ff0d2' : '#eef2f7'), pri: spotted ? 4 : 1, anchorX: t.sx, anchorY: t.sy, arrow: true });
            }
            this._hit.push({ sx: t.sx, sy: t.sy, type: 'feature', f: a.f });
        }
    }
    _drawLive(ctx, P, W, w, h) {
        const items = this._items(), n = this._nHist(); if (!this.live.length) return;
        // trail: fades with age
        for (let j = 1; j < this.live.length; j++) {
            const a = P(...W(n + j - 1)), b = P(...W(n + j)), age = (this.live.length - j) / this.live.length;
            ctx.strokeStyle = `rgba(255,255,255,${Math.max(0.12, 0.85 - age * 0.7)})`; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke();
        }
        const j = this.live.length - 1, s = P(...W(n + j)), it = items[n + j];
        // donut = live probability split
        let a0 = -Math.PI / 2; ctx.lineWidth = 4;
        it.posterior.forEach((p, r) => { ctx.strokeStyle = this.viz.regime_colors[r]; ctx.beginPath(); ctx.arc(s.sx, s.sy, 9, a0, a0 + p * Math.PI * 2); ctx.stroke(); a0 += p * Math.PI * 2; });
        ctx.beginPath(); ctx.arc(s.sx, s.sy, 4, 0, Math.PI * 2); ctx.fillStyle = '#fff'; ctx.fill();
        this._hit.push({ sx: s.sx, sy: s.sy, type: 'live', i: j });
        if (this.mode === 'pca2d' && this.panel.showPulls && this.panel.liveData && this.viz) {   // exact live pulls, same fan as the 3D tab
            const L = this.viz.pca_meta.loadings, [a, b] = this.pair, c = [0, 1, 2].find(q => q !== a && q !== b), sc = (this.panel.staticData?.arrow_scale || 6) * this.panel.vectorSpread * this.k, o = P(0, 0, 0);
            const top = this.viz.feature_short.map((nm, f) => ({ nm, d: [it.z[f] * L[f][a], it.z[f] * L[f][b], it.z[f] * L[f][c]] })).sort((x, y) => Math.hypot(y.d[0], y.d[1]) - Math.hypot(x.d[0], x.d[1])).slice(0, 4);
            for (const r of top) { const t = P(r.d[0] * sc, r.d[1] * sc, r.d[2] * sc); ctx.strokeStyle = 'rgba(255,196,92,0.55)'; ctx.lineWidth = 1.4; ctx.beginPath(); ctx.moveTo(o.sx, o.sy); ctx.lineTo(t.sx, t.sy); ctx.stroke(); }
        }
        if (this.mode !== 'pca2d' && it.ood) this._labels.push({ x: s.sx + 12, y: s.sy - 12, text: 'out-of-distribution: placement approximate', color: '#ff8a5c', pri: 5 });
        else if (this.mode !== 'pca2d') this._labels.push({ x: s.sx + 12, y: s.sy - 12, text: `live (placement ${it.conf <= 1.2 ? 'good' : 'loose'})`, color: '#fff', pri: 2 });
    }
    // selected point: animated exact decomposition (PCA) / displacement vector (all)
    _drawSelection(ctx, P, W) {
        const items = this._items(), n = this._nHist(), idx = this.sel.type === 'live' ? n + this.sel.i : this.sel.i, it = items[idx]; if (!it) return;
        const s = P(...W(idx)); ctx.beginPath(); ctx.arc(s.sx, s.sy, 9, 0, Math.PI * 2); ctx.strokeStyle = '#ffc45c'; ctx.lineWidth = 1.6; ctx.stroke();
        const prevIdx = idx > 0 ? (idx === n ? n - 1 : idx - 1) : null;
        if (prevIdx !== null && items[prevIdx]) {   // day-over-day displacement
            const p = P(...W(prevIdx)); ctx.setLineDash([4, 3]); ctx.strokeStyle = 'rgba(255,196,92,0.8)'; ctx.beginPath(); ctx.moveTo(p.sx, p.sy); ctx.lineTo(s.sx, s.sy); ctx.stroke(); ctx.setLineDash([]);
            this.panel._arrowHead(ctx, p, s, 'rgba(255,196,92,0.9)');
        }
        if (this.mode === 'pca2d') {   // exact: score_k = sum_j z_j * loading_jk, stacked head to tail from the origin
            const v = this.viz, L = v.pca_meta.loadings, [a, b] = this.pair, c = [0, 1, 2].find(q => q !== a && q !== b), k = this.k;
            const rows = v.feature_short.map((nm, f) => ({ nm, f, d: [it.z[f] * L[f][a], it.z[f] * L[f][b], it.z[f] * L[f][c]] })).sort((x, y) => Math.hypot(y.d[0], y.d[1]) - Math.hypot(x.d[0], x.d[1]));
            const cols = ['#ffc45c', '#7cc4ff', '#ff7ab6', '#8be39a', '#c8a2ff', '#ff9a5c'];
            let acc = [0, 0, 0], prev = P(0, 0, 0); const top = rows.slice(0, 6), rest = rows.slice(6);
            const total = top.length + 1, prog = this.decomp * total;
            top.forEach((r, q) => {
                const f = Math.max(0, Math.min(1, prog - q)); if (f <= 0) return;
                const nxt = [acc[0] + r.d[0] * f, acc[1] + r.d[1] * f, acc[2] + r.d[2] * f];
                const to = P(nxt[0] * k, nxt[1] * k, nxt[2] * k);
                ctx.strokeStyle = cols[q]; ctx.lineWidth = 2.4; ctx.beginPath(); ctx.moveTo(prev.sx, prev.sy); ctx.lineTo(to.sx, to.sy); ctx.stroke();
                if (f >= 1) this._labels.push({ x: (prev.sx + to.sx) / 2, y: (prev.sy + to.sy) / 2, text: r.nm, color: cols[q], pri: 4 });
                acc = nxt; prev = to;
            });
            const f2 = Math.max(0, Math.min(1, prog - top.length));
            if (f2 > 0) { const rs = [0, 0, 0]; rest.forEach(r => { rs[0] += r.d[0]; rs[1] += r.d[1]; rs[2] += r.d[2]; });
                const to = P((acc[0] + rs[0] * f2) * k, (acc[1] + rs[1] * f2) * k, (acc[2] + rs[2] * f2) * k);
                ctx.setLineDash([2, 3]); ctx.strokeStyle = 'rgba(200,205,215,0.7)'; ctx.beginPath(); ctx.moveTo(prev.sx, prev.sy); ctx.lineTo(to.sx, to.sy); ctx.stroke(); ctx.setLineDash([]); }
        }
    }
    // greedy label placement with leader lines: no overlaps at any viewport width
    _placeLabels(ctx, w, h) {
        const boxes = [], placed = [];
        const labels = this._labels.slice().sort((a, b) => (b.pri || 0) - (a.pri || 0));
        ctx.font = 'bold 10px monospace';
        const offs = [[6, -6], [6, 12], [-6, -6], [-6, 12], [0, -16], [0, 20], [14, -22], [14, 26], [-14, -22], [-14, 26], [24, 0], [-24, 0], [30, -34], [30, 38], [-30, -34], [-30, 38], [44, 0], [-44, 0]];
        const hit = (b) => boxes.some(q => b.x < q.x + q.w && b.x + b.w > q.x && b.y < q.y + q.h && b.y + b.h > q.y);
        const R = this.rect || { x0: 0, x1: w, y0: 70, y1: h - 175 }, topBand = R.y0, botBand = h - R.y1;      // keep clear of the control rows, side boxes and the footer
        for (const L of labels) {
            const tw = ctx.measureText(L.text).width + 4, th = 13, ax = L.anchorX ?? L.x, ay = L.anchorY ?? L.y;
            let spot = null;
            for (const [ox, oy] of offs) {
                let x = ax + ox - (ox < 0 ? tw : 0), y = ay + oy - th + 3;
                x = Math.max(R.x0 + 4, Math.min(R.x1 - tw - 4, x)); y = Math.max(topBand, Math.min(h - botBand, y));
                const b = { x, y, w: tw, h: th }; if (!hit(b)) { spot = b; break; }
            }
            if (!spot) continue;      // out of room: drop the label rather than overlap another
            boxes.push(spot); placed.push({ L, b: spot, ax, ay });
        }
        for (const { L, b, ax, ay } of placed) {
            const cx = b.x + b.w / 2, cy = b.y + b.h / 2;
            if (Math.hypot(cx - ax, cy - ay) > 22) { ctx.strokeStyle = 'rgba(200,205,215,0.35)'; ctx.lineWidth = 0.8; ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(cx, cy); ctx.stroke(); }
            ctx.fillStyle = 'rgba(0,0,0,0.6)'; ctx.fillRect(b.x, b.y, b.w, b.h);
            ctx.fillStyle = L.color; ctx.textAlign = 'left'; ctx.fillText(L.text, b.x + 2, b.y + 10);
        }
        this._labelBoxes = boxes;
    }
}
window.ProjectionView = ProjectionView;
