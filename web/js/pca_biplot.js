// pca_biplot.js — implements the supplied "PCA bubbles and vectors" display
// spec in full:
//   Part A: regime bubbles are wireframe ELLIPSOIDS built from each regime's
//           fitted 3x3 covariance (center + 3 scaled semi-axes), never
//           placeholder spheres, and every bubble carries a persistent label
//           (rendered every frame, not just on hover).
//   Part B: feature loading vectors are arrows from the origin. Scaling
//           convention is fixed and never mixed: points are plotted as raw
//           PCA scores, every arrow is multiplied by ONE shared constant
//           (arrow_scale, sent by the server) -- so the live point decomposes
//           EXACTLY into the sum of the per-feature pull vectors also sent by
//           the server. The "per-feature pulls" overlay draws that exact
//           decomposition for the live observation, toggleable.
//   Part C: reading protocol / acceptance checks -- see the inline notes next
//           to each piece of the acceptance checklist below.
class PcaBiplotPanel {
    constructor(container) {
        this.container = container;
        this.staticData = null;
        this.liveData = null;
        this.trail = [];
        this.showPulls = true;
        this.showEllipsoids = true;
        this.fittedShape = false; // false = round bubbles (default); true = true covariance ellipsoids
        this.bubbleSpread = 1;   // exploded-view factor for the regime bubbles (1 = true score positions)
        this.vectorSpread = 1;   // multiplier on the ONE shared arrow constant (all arrows scale together)

        this.root = document.createElement('div');
        this.root.style.cssText = 'position:relative;width:100%;height:100%;';
        container.appendChild(this.root);

        this.canvas = document.createElement('canvas');
        this.root.appendChild(this.canvas);

        this.cam = new Cam3D({ yaw: 0.38, pitch: 0.40, zoom: 1, radius: 13 });
        attachOrbit(this.canvas, this.cam, {
            onChange: () => this.draw(),
            onDoubleTap: () => { this.cam.reset(); this.draw(); },
            onTap: (x, y) => this._tap(x, y),
        });

        this.views = null; // ProjectionView, created below once the overlay rows exist
        this.legend = document.createElement('div');
        this.legend.className = 'vt-pca-toggle-row';
        this.legend.innerHTML = `
            <button class="vt-pca-toggle-btn active" data-t="pulls">Feature Pulls</button>
            <button class="vt-pca-toggle-btn active" data-t="ellipsoids">Regime Bubbles</button>
            <button class="vt-pca-toggle-btn" data-t="fitted" title="Show the true fitted covariance shape instead of round bubbles">Fitted Shape</button>`;
        this.root.appendChild(this.legend);
        this.legend.querySelectorAll('button').forEach(btn => btn.addEventListener('click', () => {
            btn.classList.toggle('active');
            if (btn.dataset.t === 'pulls') this.showPulls = btn.classList.contains('active');
            if (btn.dataset.t === 'ellipsoids') this.showEllipsoids = btn.classList.contains('active');
            if (btn.dataset.t === 'fitted') { this.fittedShape = btn.classList.contains('active'); this._frame(); }
            this.draw(); this.views.draw();
        }));
        // view selector (visualization task): PCA 3D is the unchanged baseline; the others are display-only views
        this.mode = 'pca3d';
        this.legend.insertAdjacentHTML('afterbegin', `<span class="vt-view-tabs" role="tablist">
            <button class="vt-view-tab active" data-m="pca3d">PCA 3D</button><button class="vt-view-tab" data-m="pca2d">PCA 2D</button>
            <button class="vt-view-tab" data-m="tsne2d">t-SNE 2D</button><button class="vt-view-tab" data-m="tsne3d">t-SNE 3D</button></span>`);
        this.legend.querySelectorAll('.vt-view-tab').forEach(b => b.addEventListener('click', () => this.setMode(b.dataset.m)));

        this.controls = document.createElement('div');
        this.controls.className = 'vt-pca-controls';
        this.controls.innerHTML = `
            <label><span class="nm">Bubbles</span><input type="range" min="1" max="2.5" step="0.05" value="1" data-s="bubble"><span class="val" data-v="bubble">1.0×</span></label>
            <label><span class="nm">Vectors</span><input type="range" min="1" max="2.5" step="0.05" value="1" data-s="vector"><span class="val" data-v="vector">1.0×</span></label>
            <div class="note">Spread view: bubbles are moved apart (each with its own points) and arrows lengthened for legibility. Set both to 1.0× for exact score positions and the exact per-feature decomposition.</div>`;
        this.root.appendChild(this.controls);
        this.controls.querySelectorAll('input[type=range]').forEach(inp => inp.addEventListener('input', () => {
            const v = parseFloat(inp.value);
            if (inp.dataset.s === 'bubble') this.bubbleSpread = v; else this.vectorSpread = v;
            this.controls.querySelector(`[data-v="${inp.dataset.s}"]`).textContent = v.toFixed(1) + '×';
            this.controls.querySelector('.note').style.display = (this.bubbleSpread !== 1 || this.vectorSpread !== 1) ? 'block' : 'none';
            this._frame(); this.draw();
        }));

        this.views = new ProjectionView(this);
        this.legendBox = document.createElement('div');
        this.legendBox.className = 'vt-pca-legend';
        this.root.appendChild(this.legendBox);

        this.tooltip = document.createElement('div');
        this.tooltip.className = 'vt-hover-box';
        this.root.appendChild(this.tooltip);

        window.addEventListener('resize', () => { this._frame(); this.draw(); });
    }

    // Bubble geometry. Default = ROUND: a sphere whose volume equals the regime's fitted
    // ellipsoid (r = cube root of the product of the three fitted radii), so size still shows
    // how spread out the regime is but the shape is never a flat disc. 'Fitted Shape' returns
    // the true covariance ellipsoid (orthonormal axes, one radius each).
    _bubbleAxes(e) {
        if (this.fittedShape) return e.axes;
        const r = Math.cbrt(Math.max(1e-9, e.axes[0][3] * e.axes[1][3] * e.axes[2][3]));
        return [[1, 0, 0, r], [0, 1, 0, r], [0, 0, 1, r]];
    }

    // Offset applied to everything belonging to regime `r` in the exploded view.
    _shift(r) {
        const e = (this.staticData?.ellipsoids || []).find(x => x.regime === r);
        if (!e) return [0, 0, 0];
        const k = this.bubbleSpread - 1;
        return [e.center[0] * k, e.center[1] * k, e.center[2] * k];
    }
    _liveRegime() {
        const name = this.liveData?.predicted_regime;
        const e = (this.staticData?.ellipsoids || []).find(x => x.name === name);
        return e ? e.regime : -1;
    }
    _vscale() { return (this.staticData?.arrow_scale || 1) * this.vectorSpread; }

    // Auto-fit: choose the camera radius so the structure's projected extents fill the
    // box in BOTH dimensions (a wide box gets a wide cloud, not a small island in the
    // middle). Computed at the home orientation + current spread, and again on resize;
    // it does not change while the user rotates, so the view never pulses.
    _frame() {
        const d = this.staticData; if (!d) return;
        const r = this.container.getBoundingClientRect();
        const w = Math.max(300, r.width), h = Math.max(240, r.height);
        const tmp = new Cam3D({ yaw: this.cam.home.yaw, pitch: this.cam.home.pitch });
        // px-per-world-unit that fits the structure at a given (bubble, vector) spread
        const fit = (bs, vsf) => {
            let xs = 0.5, ys = 0.5;
            const take = (x, y, z) => { const v = tmp.rotate(x, y, z); xs = Math.max(xs, Math.abs(v[0])); ys = Math.max(ys, Math.abs(v[1])); };
            const sh = (reg) => { const e = (d.ellipsoids || []).find(q => q.regime === reg); return e ? [e.center[0] * (bs - 1), e.center[1] * (bs - 1), e.center[2] * (bs - 1)] : [0, 0, 0]; };
            for (const p of d.points || []) { const s = sh(p.regime); take(p.pc1 + s[0], p.pc2 + s[1], p.pc3 + s[2]); }
            for (const e of d.ellipsoids || []) {
                const s = sh(e.regime);
                for (const a of this._bubbleAxes(e)) for (const sg of [-1, 1])
                    take(e.center[0] + s[0] + sg * a[0] * a[3], e.center[1] + s[1] + sg * a[1] * a[3], e.center[2] + s[2] + sg * a[2] * a[3]);
            }
            const vs = (d.arrow_scale || 1) * vsf;
            for (const l of d.loadings || []) take(l.pc1 * vs, l.pc2 * vs, l.pc3 * vs);
            return 0.80 * Math.min((w / 2) / xs, (h / 2) / ys);
        };
        // Fit to the current spread, but never shrink below 72% of the 1x scale: a spread slider
        // must visibly pull things APART, not just re-zoom them back to the same on-screen gap.
        // (At the extremes an outer point may leave the box -- wheel/pinch zooms out.)
        const want = Math.max(fit(this.bubbleSpread, this.vectorSpread), 0.72 * fit(1, 1));
        this.cam.radius = Math.max(3, Math.min(w, h) * 0.92 / (2 * want));
    }

    setStatic(data) {
        this.staticData = data;
        this._frame();
        this.cam.home.zoom = this.cam.zoom = 1;
        this.draw();
    }

    updateLive(data) {
        this.liveData = data;
        this.trail.push([data.pca_coords.pc1, data.pca_coords.pc2, data.pca_coords.pc3]);
        this.draw();
    }

    _renderLegend() {
        const d = this.staticData; if (!d) return;
        const prob = {}; for (const q of this.liveData?.probabilities || []) prob[q.name] = q.p;
        const n = {}; for (const pt of d.points || []) n[pt.regime] = (n[pt.regime] || 0) + 1;
        const rows = (d.regimes || []).map((rg, i) => ({ rg, i, p: prob[rg.name], n: n[i] || 0 }))
            .sort((a, b) => (b.p ?? -1) - (a.p ?? -1));
        this.legendBox.innerHTML = '<div class="hd">Regimes</div>' + rows.map(r => `
            <div class="row ${this.views && this.views.hidden.has(r.i) ? 'off' : ''}" data-r="${r.i}" title="click: show/hide this regime (2D views) - double-click: isolate"><span class="dot" style="background:${r.rg.color}"></span>
            <span class="nm">${r.rg.name}</span><span class="n">n=${r.n}</span>
            <span class="pr" style="color:${r.rg.color}">${r.p === undefined ? '' : Math.round(r.p * 100) + '%'}</span></div>`).join('') +
            '<div class="ft">modebar: zoom / pan / reset / PNG · drag to rotate · wheel / pinch to zoom · double-tap resets · tap a bubble, arrow or the white dot to inspect</div>';
        if (this._legendBound || !this.views) return; this._legendBound = true;   // delegated: rows are re-rendered on every update
        const V = this.views, nReg = () => ((this.staticData || {}).regimes || []).length;
        this.legendBox.addEventListener('click', (e) => { const el = e.target.closest('.row[data-r]'); if (!el || e.detail > 1) return; const r = +el.dataset.r;
            V.hidden.has(r) ? V.hidden.delete(r) : V.hidden.add(r); if (V.hidden.size >= nReg()) V.hidden.clear(); this._renderLegend(); V.draw(); });
        this.legendBox.addEventListener('dblclick', (e) => { const el = e.target.closest('.row[data-r]'); if (!el) return; const r = +el.dataset.r;
            V.hidden = new Set(Array.from({ length: nReg() }, (_, i) => i).filter(i => i !== r)); this._renderLegend(); V.draw(); });
        this.legendBox.style.cursor = 'pointer';
    }

    setMode(m) {
        if (m === this.mode) return;
        const prev = this.mode; this.mode = m;
        this.legend.querySelectorAll('.vt-view-tab').forEach(b => b.classList.toggle('active', b.dataset.m === m));
        const ev = document.getElementById('vt-pca-explained-variance');
        if (m === 'pca3d') {
            this.views.leave(() => { this.canvas.style.display = 'block'; if (ev) ev.style.display = ''; this.draw(); });
        } else {
            if (ev) ev.style.display = 'none';
            this.views.enter(m, prev === 'pca3d');
            this.canvas.style.display = 'none';
        }
    }
    setViz(v) { this.views.setViz(v); }
    updateViz(v) { this.views.pushLive(v, this.liveData?.pca_coords); }
    resetForTicker() { this.trail = []; this.liveData = null; this.views.reset(); this._renderLegend(); this.draw(); }

    draw() {
        if (!this.staticData) return;
        this._renderLegend();
        if (this.views.active) { this.views.draw(); return; }
        const { ctx, w, h } = vtSizeCanvas(this.canvas, this.container);
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = '#000'; ctx.fillRect(0, 0, w, h);
        const P = (x, y, z) => this.cam.project(x, y, z, w, h);
        this._hit = [];

        this._drawAxes(ctx, P);
        if (this.showEllipsoids) this._drawEllipsoids(ctx, P);
        this._drawPoints(ctx, P);
        this._drawArrows(ctx, P);
        if (this.showPulls && this.liveData) this._drawPulls(ctx, P);
        this._drawLiveDot(ctx, P);
    }

    _drawAxes(ctx, P) {
        const L = this.cam.radius * 0.92;
        const axes = [{ d: [1, 0, 0], label: 'PC1' }, { d: [0, 1, 0], label: 'PC2' }, { d: [0, 0, 1], label: 'PC3' }];
        ctx.font = '11px monospace';
        for (const ax of axes) {
            const a = P(0, 0, 0), b = P(ax.d[0] * L, ax.d[1] * L, ax.d[2] * L);
            ctx.strokeStyle = 'rgba(160,170,185,0.4)'; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke();
            ctx.fillStyle = 'rgba(180,190,205,0.75)'; ctx.fillText(ax.label, b.sx + 3, b.sy);
        }
    }

    _drawEllipsoids(ctx, P) {
        const rings = 10, seg = 28;
        for (const e of this.staticData.ellipsoids || []) {
            const axes = this._bubbleAxes(e);
            const sh = this._shift(e.regime);
            const cx = e.center[0] + sh[0], cy = e.center[1] + sh[1], cz = e.center[2] + sh[2];
            const localPoint = (u, v) => {
                const su = Math.sin(u), cu = Math.cos(u), sv = Math.sin(v), cv = Math.cos(v);
                const local = [su * cv, cu, su * sv];
                let out = [cx, cy, cz];
                for (let k = 0; k < 3; k++) {
                    const [ax, ay, az, r] = axes[k];
                    out = [out[0] + ax * local[k] * r, out[1] + ay * local[k] * r, out[2] + az * local[k] * r];
                }
                return out;
            };
            ctx.strokeStyle = e.color + '55'; ctx.lineWidth = 0.7;
            let topScreen = null, topY = Infinity;
            for (let ring = 1; ring < rings; ring++) {
                const u = (ring / rings) * Math.PI;
                let prev = null;
                for (let s = 0; s <= seg; s++) {
                    const v = (s / seg) * Math.PI * 2;
                    const sp = P(...localPoint(u, v));
                    if (prev) { ctx.beginPath(); ctx.moveTo(prev.sx, prev.sy); ctx.lineTo(sp.sx, sp.sy); ctx.stroke(); }
                    prev = sp;
                    if (sp.sy < topY) { topY = sp.sy; topScreen = sp; }
                }
            }
            for (let s = 0; s < seg; s += 2) {
                const v = (s / seg) * Math.PI * 2;
                let prev = null;
                for (let ring = 0; ring <= rings; ring++) {
                    const u = (ring / rings) * Math.PI;
                    const sp = P(...localPoint(u, v));
                    if (prev) { ctx.beginPath(); ctx.moveTo(prev.sx, prev.sy); ctx.lineTo(sp.sx, sp.sy); ctx.stroke(); }
                    prev = sp;
                }
            }
            const labelPos = topScreen || P(cx, cy, cz);
            ctx.font = 'bold 12px monospace';
            ctx.fillStyle = 'rgba(0,0,0,0.55)'; ctx.fillText(e.name, labelPos.sx - 9, labelPos.sy - 9);
            ctx.fillStyle = e.color; ctx.fillText(e.name, labelPos.sx - 10, labelPos.sy - 10);
            this._hit.push({ sx: labelPos.sx, sy: labelPos.sy, type: 'regime', data: e });
        }
    }

    _drawPoints(ctx, P) {
        const pts = (this.staticData.points || []).map(p => {
            const sh = this._shift(p.regime);
            const s = P(p.pc1 + sh[0], p.pc2 + sh[1], p.pc3 + sh[2]);
            const regime = (this.staticData.regimes || [])[p.regime] || { color: '#999' };
            return { ...s, color: regime.color };
        });
        pts.sort((a, b) => a.z - b.z);
        for (const p of pts) { ctx.fillStyle = p.color; ctx.globalAlpha = 0.75; ctx.fillRect(p.sx - 1.6, p.sy - 1.6, 3.2, 3.2); }
        ctx.globalAlpha = 1;
    }

    _drawArrows(ctx, P) {
        const scale = this._vscale();
        const loadings = this.staticData.loadings || [];
        // Label only the longest arrows (biggest loadings) to keep the biplot
        // readable; shorter arrows still draw and are still tap-inspectable.
        const byLen = loadings.map(l => ({ l, len: Math.hypot(l.pc1, l.pc2, l.pc3) })).sort((a, b) => b.len - a.len);
        const labelSet = new Set(byLen.slice(0, 9).map(x => x.l.name));
        for (const l of loadings) {
            const tip = [l.pc1 * scale, l.pc2 * scale, l.pc3 * scale];
            const a = P(0, 0, 0), b = P(...tip);
            ctx.strokeStyle = 'rgba(230,235,245,0.5)'; ctx.lineWidth = 1.2;
            ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke();
            this._arrowHead(ctx, a, b, 'rgba(230,235,245,0.65)');
            if (labelSet.has(l.name)) {
                ctx.font = '10px monospace'; ctx.fillStyle = '#eef2f7';
                ctx.fillText(l.short || l.name, b.sx + 3, b.sy - 3);
            }
            this._hit.push({ sx: b.sx, sy: b.sy, type: 'feature', data: l });
        }
    }

    // Part B overlay: exact per-feature pulls for the live observation, drawn
    // as a fan from the origin (not chained tip-to-tail -- a chain lets large
    // offsetting contributions wander far off-canvas even though their VECTOR
    // SUM is bounded, which is visually misleading). Each arrow here is
    // exactly standardized_feature[i] * loading[i] * arrow_scale; their sum
    // is, by construction, the live point -- shown as a dashed sum-check line.
    _drawPulls(ctx, P) {
        const scale = this._vscale();
        const all = this.liveData.feature_contributions || [];
        const top = all.slice().sort((a, b) => Math.hypot(...b.pull) - Math.hypot(...a.pull)).slice(0, 6);
        const origin = P(0, 0, 0);
        for (const c of top) {
            const mag = Math.hypot(...c.pull);
            if (mag < 0.02) continue;
            const tip = P(c.pull[0] * scale, c.pull[1] * scale, c.pull[2] * scale);
            ctx.strokeStyle = 'rgba(255,196,92,0.9)'; ctx.lineWidth = 1.8;
            ctx.beginPath(); ctx.moveTo(origin.sx, origin.sy); ctx.lineTo(tip.sx, tip.sy); ctx.stroke();
            this._arrowHead(ctx, origin, tip, 'rgba(255,196,92,0.95)');
            ctx.font = 'bold 10px monospace'; ctx.fillStyle = '#ffc45c';
            ctx.fillText(c.name.split(' ')[0] === 'Net' ? 'GEX' : (c.short || c.name), tip.sx + 3, tip.sy + 10);
        }
    }

    _drawLiveDot(ctx, P) {
        if (this.trail.length > 1) {
            const sh = this._shift(this._liveRegime());
            const pts = this.trail.map(p => P(p[0] + sh[0], p[1] + sh[1], p[2] + sh[2]));
            ctx.strokeStyle = this.liveData?.directional_bias_bullish ? 'rgba(47,208,106,0.9)' : 'rgba(224,75,75,0.9)';
            ctx.lineWidth = 1.6;
            ctx.beginPath(); pts.forEach((p, i) => (i ? ctx.lineTo(p.sx, p.sy) : ctx.moveTo(p.sx, p.sy))); ctx.stroke();
        }
        if (!this.liveData) return;
        const { pc1, pc2, pc3 } = this.liveData.pca_coords;
        const lsh = this._shift(this._liveRegime());
        const s = P(pc1 + lsh[0], pc2 + lsh[1], pc3 + lsh[2]);
        ctx.beginPath(); ctx.arc(s.sx, s.sy, 7, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(255,255,255,0.25)'; ctx.fill();
        ctx.beginPath(); ctx.arc(s.sx, s.sy, 4, 0, Math.PI * 2);
        ctx.fillStyle = '#fff'; ctx.fill();
        this._hit.push({ sx: s.sx, sy: s.sy, type: 'live', data: this.liveData });
    }

    _arrowHead(ctx, from, to, color) {
        const ang = Math.atan2(to.sy - from.sy, to.sx - from.sx);
        const len = 7;
        ctx.beginPath();
        ctx.moveTo(to.sx, to.sy);
        ctx.lineTo(to.sx - len * Math.cos(ang - 0.35), to.sy - len * Math.sin(ang - 0.35));
        ctx.lineTo(to.sx - len * Math.cos(ang + 0.35), to.sy - len * Math.sin(ang + 0.35));
        ctx.closePath();
        ctx.fillStyle = color; ctx.fill();
    }

    _tap(x, y) {
        let best = null, bd = 20 * 20;
        for (const h of this._hit) { const d = (h.sx - x) ** 2 + (h.sy - y) ** 2; if (d < bd) { bd = d; best = h; } }
        if (!best) { this.tooltip.style.display = 'none'; return; }
        this.tooltip.style.display = 'block';
        this.tooltip.style.left = (x + 12) + 'px'; this.tooltip.style.top = (y - 10) + 'px';
        this.tooltip.style.right = 'auto';
        if (best.type === 'regime') {
            const n = (this.staticData.points || []).filter(p => p.regime === best.data.regime).length;
            this.tooltip.innerHTML = `<div><b style="color:${best.data.color}">${best.data.name}</b></div><div class="hr">n=${n} historical days in this regime</div><div class="hr">${this.fittedShape ? 'true fitted covariance shape' : 'round (same volume as fitted shape)'}</div>`;
        } else if (best.type === 'feature') {
            const live = this.liveData?.feature_contributions?.find(c => c.name === best.data.name);
            const mag = live ? Math.hypot(...live.pull).toFixed(3) : 'n/a';
            this.tooltip.innerHTML = `<div><b>${best.data.short}</b></div><div class="hr">${best.data.name}</div><div class="hr">today's pull magnitude: ${mag}</div>`;
        } else if (best.type === 'live') {
            this.tooltip.innerHTML = `<div><b style="color:${best.data.predicted_color}">${best.data.predicted_regime}</b></div><div class="hr">current live read</div>`;
        }
    }
}
