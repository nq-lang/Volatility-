// surface_renderer.js — SurfacePanel now shows 3D and 2D SIMULTANEOUSLY,
// side-by-side (institutional-terminal style), per spec: the 2D/3D toggle is
// gone. Left pane: the true continuous 3D mesh, now with real DATA-DRIVEN
// projection walls (not empty grid panels) -- the shortest-dated expiry's
// strike/delta profile silhouetted on the back wall, and the ATM/center
// term-structure profile silhouetted on the side wall, both updating with
// the data. Right pane: a stacked column of 2D cross-sectional charts, one
// per real expiry in the current bucket (dot markers at every data node,
// connected by an interpolated line), each with the same dashed SPOT/ATM
// marker as the 3D view so the two panes read as one synchronized model.

const SIGNED_GREEN = '#53b680';
const SIGNED_RED = '#d26a6a';
const CALL_BLUE = '#3665c7';
const PUT_PINK = '#d26a6a';
const NEUTRAL_TEAL = '#4cc9c4';
const LABEL_COLOR = '#a0a5b0';
const STRIKE_LINE_COLOR = '#c58d3c';
const BB_ORANGE = '#ffa028';
const SINGLE_ROWS = 7; // grid rows used to draw a single expiry as a proper surface

function colorForMode(mode) {
    switch (mode) {
        case 'signed': return null; // per-point sign decides
        case 'blue': return CALL_BLUE;
        case 'red': return PUT_PINK;
        case 'teal': return NEUTRAL_TEAL;
        default: return null;
    }
}

class SurfacePanel {
    constructor(container, opts = {}) {
        this.container = container;
        this.opts = Object.assign({
            xLabel: 'x', zLabel: 'z', colorMode: 'signed', spot: null,
            surfaceStyle: 'signed', zeroCrossLabel: 'Zero-cross', wallLabels: ['Wall(+)', 'Wall(-)'],
        }, opts);
        if (this.opts.surfaceStyle === 'ridge') this.opts.surfaceStyle = 'signed';
        if (this.opts.surfaceStyle === 'rainbow') this.opts.surfaceStyle = 'bloomberg';
        this.points = null;
        this.lens = null;
        this._grid = null;

        this.root = document.createElement('div');
        this.root.className = 'vt-split-wrap';
        this.container.appendChild(this.root);

        this.leftPane = document.createElement('div');
        this.leftPane.className = 'vt-split-left';
        this.root.appendChild(this.leftPane);

        this.canvas3d = document.createElement('canvas');
        this.leftPane.appendChild(this.canvas3d);
        this.cam = new Cam3D({ yaw: this.opts.surfaceStyle === 'bloomberg' ? 0.62 : 0.32, pitch: 0.5, zoom: 1, radius: 62 });
        this.cam.center = [0, 6, 0];
        attachOrbit(this.canvas3d, this.cam, {
            onChange: () => this._draw3d(),
            onDoubleTap: () => { this.cam.reset(); this._draw3d(); },
            onHover: (x, y) => this._hover3d(x, y),
        });
        this.hoverBox = document.createElement('div');
        this.hoverBox.className = 'vt-hover-box';
        this.leftPane.appendChild(this.hoverBox);
        this._statsEl = document.createElement('div');
        this._statsEl.className = 'vt-panel-stats';
        this.leftPane.appendChild(this._statsEl);

        this.rightPane = document.createElement('div');
        this.rightPane.className = 'vt-split-right';
        this.root.appendChild(this.rightPane);

        window.addEventListener('resize', () => { if (this.points) this.update(this.points); });
    }

    // Strike lens (Task 2): lens = { markers(): [{x,label,color}] in THIS panel's strike space, toFut(k): futures-equivalent|null, anchor }.
    // Only meaningful on strike-axis panels; delta-axis IV surfaces ignore it.
    setLens(lens) { this.lens = (this.opts.xLabel === 'strike') ? lens : null; if (this.points) this.update(this.points); }
    _lensMarkers() { if (!this.lens || !this._grid || this._grid.xs.length < 2) return []; const g = this._grid, lo = g.xs[0], hi = g.xs[g.xs.length - 1];
        return (this.lens.markers() || []).filter(m => m.x >= lo && m.x <= hi); }

    showAwaiting(dteLabel) {
        this.points = null; this._grid = null;
        const { ctx, w, h } = vtSizeCanvas(this.canvas3d, this.leftPane);
        ctx.clearRect(0, 0, w, h); ctx.fillStyle = '#050508'; ctx.fillRect(0, 0, w, h);
        ctx.fillStyle = '#7a828d'; ctx.font = '12px monospace';
        ctx.fillText('awaiting ' + dteLabel + ' data…', 16, 28);
        this._statsEl.style.display = 'none';
        this.rightPane.innerHTML = '<div class="vt-mini-chart-label" style="padding:8px">awaiting ' + dteLabel + ' data…</div>';
    }

    update(points) {
        this.points = points;
        if (!points || !points.length) return;
        this._buildGrid(points);
        this._updateStats();
        this._draw3d();
        this._drawCrossSections();
    }

    // ------------------------------------------------------------ data grid
    _buildGrid(points) {
        const xs = [...new Set(points.map(p => p.x))].sort((a, b) => a - b);
        const ys = [...new Set(points.map(p => p.dte))].sort((a, b) => a - b);
        const single = ys.length === 1;
        const look = new Map();
        for (const p of points) look.set(p.x + '|' + p.dte, (look.get(p.x + '|' + p.dte) || 0) + p.y);
        const rows = single ? Array(SINGLE_ROWS).fill(ys[0]) : ys;
        const Z = rows.map(d => xs.map(x => look.get(x + '|' + d) ?? 0));
        let vmin = Infinity, vmax = -Infinity;
        for (const r of Z) for (const v of r) { vmin = Math.min(vmin, v); vmax = Math.max(vmax, v); }
        this._grid = { xs, ys: rows, Z, single, vmin, vmax, realYs: ys };
    }

    // ------------------------------------------------------------- 3D world
    _world() {
        const g = this._grid, W = 90;
        const D = g.single ? 30 : 56;
        const H = 24;
        const style = this.opts.surfaceStyle;
        const absMax = Math.max(Math.abs(g.vmin), Math.abs(g.vmax)) || 1;
        const range = (g.vmax - g.vmin) || 1;
        const nx = g.xs.length, ny = g.ys.length;
        const X = i => (i / (nx - 1) - 0.5) * W;
        const Zc = j => (j / (ny - 1) - 0.5) * D;
        const Y = v => style === 'signed' ? (v / absMax) * H : ((v - g.vmin) / range) * H;
        return { W, D, H, X, Z: Zc, Y, absMax, range, nx, ny, style };
    }

    _faceColor(style, vAvg, shade) {
        const g = this._grid;
        if (style === 'bloomberg') {
            const t = (vAvg - g.vmin) / ((g.vmax - g.vmin) || 1);
            let c;
            if (t < 0.72) c = vtMix([40, 225, 45], [150, 245, 40], t / 0.72);
            else c = vtMix([150, 245, 40], [255, 125, 25], Math.min(1, (t - 0.72) / 0.28));
            return vtRgb([c[0] * shade, c[1] * shade, c[2] * shade], 0.96);
        }
        const absMax = Math.max(Math.abs(g.vmin), Math.abs(g.vmax)) || 1;
        const m = Math.min(1, Math.abs(vAvg) / absMax);
        let c;
        if (this.opts.colorMode === 'teal') c = vtMix([24, 90, 92], [110, 235, 225], m);
        else if (vAvg >= 0) c = vtMix([30, 105, 78], [125, 240, 185], m);
        else c = vtMix([115, 38, 55], [245, 105, 125], m);
        return vtRgb([c[0] * shade, c[1] * shade, c[2] * shade], 0.9);
    }

    _draw3d() {
        if (!this._grid) return;
        const { ctx, w, h } = vtSizeCanvas(this.canvas3d, this.leftPane);
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = this.opts.surfaceStyle === 'bloomberg' ? '#050508' : '#0a0a0d';
        ctx.fillRect(0, 0, w, h);
        const g = this._grid, Wd = this._world();
        const P = (x, y, z) => this.cam.project(x, y, z, w, h);
        this._proj = { pts: [], w, h };

        // Projection walls FIRST (they're the backdrop), data mesh drawn on top.
        this._drawProjectionWalls(ctx, Wd, P);
        if (Wd.style === 'signed') this._drawSpotPlane(ctx, Wd, P);

        const V = g.Z.map((row, j) => row.map((v, i) => {
            const wx = Wd.X(i), wy = Wd.Y(v), wz = Wd.Z(j);
            const p = P(wx, wy, wz);
            this._proj.pts.push({ sx: p.sx, sy: p.sy, i, j, v });
            return { ...p, wx, wy, wz, v };
        }));

        const quads = [];
        for (let j = 0; j < Wd.ny - 1; j++) for (let i = 0; i < Wd.nx - 1; i++) {
            const a = V[j][i], b = V[j][i + 1], c = V[j + 1][i + 1], d = V[j + 1][i];
            const ux = b.wx - a.wx, uy = b.wy - a.wy, uz = b.wz - a.wz;
            const vx = d.wx - a.wx, vy = d.wy - a.wy, vz = d.wz - a.wz;
            const nx_ = uy * vz - uz * vy, ny_ = uz * vx - ux * vz, nz_ = ux * vy - uy * vx;
            const len = Math.hypot(nx_, ny_, nz_) || 1;
            const rn = this.cam.rotate(nx_ / len + this.cam.center[0], ny_ / len + this.cam.center[1], nz_ / len + this.cam.center[2]);
            const shade = 0.62 + 0.38 * Math.abs(rn[0] * 0.25 + rn[1] * 0.8 + rn[2] * 0.55);
            quads.push({ q: [a, b, c, d], z: (a.z + b.z + c.z + d.z) / 4, v: (a.v + b.v + c.v + d.v) / 4, shade });
        }
        quads.sort((p, q) => p.z - q.z);
        const gridStroke = Wd.style === 'bloomberg' ? 'rgba(0,0,0,0.55)' : 'rgba(6,10,12,0.5)';
        for (const f of quads) {
            const [a, b, c, d] = f.q;
            ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.lineTo(c.sx, c.sy); ctx.lineTo(d.sx, d.sy); ctx.closePath();
            ctx.fillStyle = this._faceColor(Wd.style, f.v, Math.min(1.08, f.shade));
            ctx.fill();
            ctx.strokeStyle = gridStroke; ctx.lineWidth = Wd.style === 'bloomberg' ? 0.8 : 0.5; ctx.stroke();
        }

        if (Wd.style === 'signed') this._drawSignedAnnotations(ctx, Wd, P);
        this._drawAxisTicks(ctx, Wd, P);
    }

    // -------------------------------------------------- projection walls
    // Back (Z) wall: the shortest-dated expiry's x-profile, silhouetted flat
    // against the wall -- "gauge the skew instantly without rotating."
    // Side (X) wall: the term-structure profile (value at the ATM/center x,
    // across every expiry) silhouetted against the other wall.
    _drawProjectionWalls(ctx, Wd, P) {
        const g = this._grid;
        const hx = Wd.W / 2, hz = Wd.D / 2 + 1, top = Wd.H + 4, bot = Wd.style === 'bloomberg' ? -1.5 : -Wd.H - 4;
        const zx = (s) => this.cam.rotate(s * hx, 0, 0)[2];
        const zz = (s) => this.cam.rotate(0, 0, s * hz)[2];
        const xSide = zx(-1) < zx(1) ? -1 : 1;
        const zSide = zz(-1) < zz(1) ? -1 : 1;
        const zw = zSide * hz, xw = xSide * hx;

        // base grids (subtle)
        ctx.strokeStyle = 'rgba(190,190,200,0.22)'; ctx.lineWidth = 0.6;
        for (let k = 0; k <= 8; k++) { const x = -hx + (k / 8) * 2 * hx; const a = P(x, bot, -hz), b = P(x, bot, hz); ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke(); }
        for (let k = 0; k <= 5; k++) { const z = -hz + (k / 5) * 2 * hz; const a = P(-hx, bot, z), b = P(hx, bot, z); ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke(); }

        const wallRect = (corners) => {
            ctx.beginPath(); corners.forEach((p, k) => (k ? ctx.lineTo(p.sx, p.sy) : ctx.moveTo(p.sx, p.sy))); ctx.closePath();
            ctx.fillStyle = 'rgba(150,150,160,0.10)'; ctx.fill();
            ctx.strokeStyle = 'rgba(210,210,220,0.35)'; ctx.lineWidth = 0.8; ctx.stroke();
        };
        wallRect([P(-hx, bot, zw), P(hx, bot, zw), P(hx, top, zw), P(-hx, top, zw)]); // back X-Z wall
        wallRect([P(xw, bot, -hz), P(xw, bot, hz), P(xw, top, hz), P(xw, top, -hz)]); // side Y-Z wall

        // --- skew silhouette on the back (X-Z) wall: shortest-dated row ---
        const shortRowIdx = 0; // ys sorted ascending
        const skewRow = g.Z[shortRowIdx];
        const skewColor = Wd.style === 'bloomberg' ? BB_ORANGE : (this.opts.colorMode === 'teal' ? NEUTRAL_TEAL : '#9fd0ff');
        this._drawWallSilhouette(ctx, P, skewRow.map((v, i) => [Wd.X(i), Wd.Y(v)]), zw, 'z', skewColor);

        // --- term-structure silhouette on the side (Y-Z) wall: ATM/center column ---
        if (!g.single) {
            const centerI = this._centerIndex();
            const col = g.Z.map(row => row[centerI]);
            const termColor = Wd.style === 'bloomberg' ? '#ffd27a' : '#ffc45c';
            this._drawWallSilhouette(ctx, P, col.map((v, j) => [Wd.Z(j), Wd.Y(v)]), xw, 'x', termColor);
        }
    }

    _centerIndex() {
        const g = this._grid;
        if (this.opts.spot != null) {
            let best = 0, bd = Infinity;
            g.xs.forEach((x, i) => { const d = Math.abs(x - this.opts.spot); if (d < bd) { bd = d; best = i; } });
            return best;
        }
        return Math.floor(g.xs.length / 2);
    }

    // pts: array of [axisCoord, y] pairs; wallFixed: the constant coordinate
    // the wall sits at; axis: 'z' means wall is at fixed z (points vary in x),
    // 'x' means wall is at fixed x (points vary in z).
    _drawWallSilhouette(ctx, P, pts, wallFixed, axis, color) {
        if (pts.length < 2) return;
        const proj = pts.map(([a, y]) => axis === 'z' ? P(a, y, wallFixed) : P(wallFixed, y, a));
        ctx.beginPath();
        proj.forEach((p, i) => (i ? ctx.lineTo(p.sx, p.sy) : ctx.moveTo(p.sx, p.sy)));
        ctx.strokeStyle = color; ctx.lineWidth = 1.6; ctx.stroke();
        ctx.fillStyle = color;
        for (const p of proj) { ctx.beginPath(); ctx.arc(p.sx, p.sy, 1.8, 0, Math.PI * 2); ctx.fill(); }
    }

    _drawSpotPlane(ctx, Wd, P) {
        const y = 0, hx = Wd.W / 2, hz = Wd.D / 2 + 2;
        const c = [P(-hx, y, -hz), P(hx, y, -hz), P(hx, y, hz), P(-hx, y, hz)];
        ctx.beginPath(); c.forEach((p, k) => (k ? ctx.lineTo(p.sx, p.sy) : ctx.moveTo(p.sx, p.sy))); ctx.closePath();
        ctx.fillStyle = 'rgba(63,177,255,0.07)'; ctx.fill();
        ctx.strokeStyle = 'rgba(63,177,255,0.35)'; ctx.lineWidth = 1; ctx.stroke();
    }

    _drawSignedAnnotations(ctx, Wd, P) {
        const g = this._grid;
        ctx.font = '10px monospace';
        const spot = this.opts.spot;
        if (spot != null && g.xs.length > 1 && spot >= g.xs[0] && spot <= g.xs[g.xs.length - 1]) {
            const t = (spot - g.xs[0]) / (g.xs[g.xs.length - 1] - g.xs[0]);
            const x = (t - 0.5) * Wd.W, z = Wd.D / 2 + 2;
            const a = P(x, -Wd.H * 0.9, z), b = P(x, Wd.H * 0.9, z);
            ctx.save(); ctx.strokeStyle = STRIKE_LINE_COLOR; ctx.setLineDash([4, 4]); ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke(); ctx.restore();
            ctx.fillStyle = '#9fd0ff'; ctx.fillText('SPOT ' + Math.round(spot), b.sx + 4, b.sy);
        }
        this._lensMarkers().forEach((m, mi) => {
            const t = (m.x - g.xs[0]) / (g.xs[g.xs.length - 1] - g.xs[0]);
            const x = (t - 0.5) * Wd.W, z = Wd.D / 2 + 2, a = P(x, -Wd.H * 0.9, z), b = P(x, Wd.H * 0.9, z);
            ctx.save(); ctx.strokeStyle = m.color; ctx.setLineDash([2, 3]); ctx.lineWidth = 1.5; ctx.beginPath(); ctx.moveTo(a.sx, a.sy); ctx.lineTo(b.sx, b.sy); ctx.stroke(); ctx.restore();
            ctx.fillStyle = m.color; ctx.fillText(m.label, b.sx + 3, b.sy - 2 - (mi % 4) * 11);
        });
        const fmt = v => Math.abs(v) >= 1e6 || (Math.abs(v) < 1e-2 && v !== 0) ? v.toExponential(1) : v.toFixed(2);
        const zf = Wd.D / 2 + 2, xl = -Wd.W / 2 - 4;
        const top = P(xl, Wd.H, zf), mid = P(xl, 0, zf), bot = P(xl, -Wd.H, zf);
        ctx.fillStyle = 'rgba(190,200,210,0.6)';
        ctx.fillText('+' + fmt(Wd.absMax), Math.max(2, top.sx - 34), top.sy);
        ctx.fillText('0', Math.max(2, mid.sx - 10), mid.sy);
        ctx.fillText('-' + fmt(Wd.absMax), Math.max(2, bot.sx - 34), bot.sy);
    }

    _drawAxisTicks(ctx, Wd, P) {
        const g = this._grid, bb = Wd.style === 'bloomberg';
        ctx.font = '10px monospace';
        ctx.fillStyle = bb ? BB_ORANGE : 'rgba(170,180,195,0.75)';
        const zFront = Wd.D / 2 + 3, yFloor = bb ? -1.5 : -Wd.H - 1;
        const every = Math.max(1, Math.floor(g.xs.length / 6));
        g.xs.forEach((x, i) => {
            if (i % every) return;
            const p = P(Wd.X(i), yFloor, zFront);
            ctx.fillText(typeof x === 'number' && x < 5 ? x.toFixed(2) : String(x), p.sx - 10, p.sy + 10);
        });
        if (!g.single) {
            const everyJ = Math.max(1, Math.floor(g.ys.length / 5));
            g.ys.forEach((d, j) => {
                if (j % everyJ) return;
                const p = P(Wd.W / 2 + 4, yFloor, Wd.Z(j));
                ctx.fillText(d + 'd', p.sx + 2, p.sy + 4);
            });
        }
        const t1 = P(0, yFloor, zFront + 9), t2 = P(Wd.W / 2 + 12, yFloor, 0);
        ctx.font = 'bold 11px monospace';
        ctx.fillStyle = bb ? BB_ORANGE : 'rgba(190,200,215,0.85)';
        ctx.fillText(this.opts.xLabel === 'delta' ? 'Delta' : this.opts.xLabel, t1.sx - 14, t1.sy + 12);
        if (!g.single) ctx.fillText(bb ? 'Term' : 'Expiry (DTE)', t2.sx - 6, t2.sy + 16);
        else {
            ctx.fillStyle = bb ? BB_ORANGE : 'rgba(190,200,215,0.9)';
            ctx.fillText(g.realYs[0] + ' DTE  (single expiry)', t2.sx - 24, t2.sy + 30);
        }
    }

    // --------------------------------------------------------- stats overlay
    _updateStats() {
        if (this.opts.surfaceStyle !== 'signed') { this._statsEl.style.display = 'none'; return; }
        this._statsEl.style.display = 'block';
        const g = this._grid;
        const vals = g.xs.map((x, i) => g.single ? g.Z[0][i] : g.Z.reduce((s, r) => s + r[i], 0));
        const net = vals.reduce((s, v) => s + v, 0);
        let zc = null;
        for (let i = 1; i < g.xs.length; i++) {
            const a = vals[i - 1], b = vals[i];
            if ((a <= 0 && b > 0) || (a >= 0 && b < 0)) { zc = g.xs[i - 1] + ((0 - a) / (b - a)) * (g.xs[i] - g.xs[i - 1]); break; }
        }
        let mx = 0, mn = 0;
        vals.forEach((v, i) => { if (v > vals[mx]) mx = i; if (v < vals[mn]) mn = i; });
        const [pl, nl] = this.opts.wallLabels;
        this._statsEl.innerHTML = `Net <b class="${net >= 0 ? 'v-bull' : 'v-bear'}">${net.toExponential(2)}</b>` +
            (zc !== null ? ` &middot; ${this.opts.zeroCrossLabel} <b>${zc.toFixed(0)}</b>` : '') +
            ` &middot; ${pl} <b class="v-bull">${g.xs[mx]}</b> &middot; ${nl} <b class="v-bear">${g.xs[mn]}</b>`;
    }

    _hover3d(x, y) {
        if (!this._proj || !this._grid) return;
        let best = null, bd = 16 * 16;
        for (const p of this._proj.pts) { const d = (p.sx - x) ** 2 + (p.sy - y) ** 2; if (d < bd) { bd = d; best = p; } }
        if (!best) { this.hoverBox.style.display = 'none'; return; }
        const g = this._grid;
        const big = Math.abs(best.v) >= 1e4 || (Math.abs(best.v) < 1e-2 && best.v !== 0);
        this.hoverBox.style.display = 'block';
        this.hoverBox.innerHTML = `<div><span class="hr">${this.opts.xLabel}</span> ${g.xs[best.i]}</div>` +
            (this.lens && this.lens.toFut && this.lens.toFut(g.xs[best.i]) != null ? `<div><span class="hr">${this.lens.anchor}</span> ${this.lens.toFut(g.xs[best.i]).toFixed(2)}</div>` : '') +
            `<div><span class="hr">DTE</span> ${g.ys[best.j]}</div>` +
            `<div><span class="hr">${this.opts.zLabel}</span> ${big ? best.v.toExponential(3) : best.v.toFixed(4)}</div>`;
        this._highlightCrossSection(g.ys[best.j], g.xs[best.i]);
    }

    // ------------------------------------------ 2D cross-sectional stack
    // One dot+line chart per REAL expiry in this bucket (capped so the column
    // stays usable), each with its own dashed SPOT/ATM marker, synchronized
    // with the 3D pane (hovering a 3D vertex highlights its cross-section node).
    _drawCrossSections() {
        const g = this._grid;
        this.rightPane.innerHTML = '';
        this._miniCharts = [];
        const realYs = g.single ? [g.ys[0]] : g.realYs;
        const maxCharts = 6;
        const shown = realYs.length <= maxCharts ? realYs
            : [realYs[0], realYs[Math.floor(realYs.length * 0.25)], realYs[Math.floor(realYs.length * 0.5)],
               realYs[Math.floor(realYs.length * 0.75)], realYs[realYs.length - 1]];

        for (const dte of shown) {
            const rowIdx = g.ys.findIndex(d => d === dte);
            const row = g.Z[rowIdx];
            const wrap = document.createElement('div');
            wrap.className = 'vt-mini-chart';
            const label = document.createElement('div');
            label.className = 'vt-mini-chart-label';
            label.textContent = dte + ' DTE cross-section';
            const canvas = document.createElement('canvas');
            wrap.appendChild(label); wrap.appendChild(canvas);
            this.rightPane.appendChild(wrap);
            this._renderMiniChart(canvas, row, dte);
            this._miniCharts.push({ canvas, dte });
        }
    }

    _renderMiniChart(canvas, row, dte) {
        const g = this._grid;
        const { ctx, w, h } = vtSizeCanvas(canvas, canvas.parentElement);
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = '#0a0b0e'; ctx.fillRect(0, 0, w, h);
        const padL = 46, padB = 16, padT = 6, padR = 8;
        const plotW = w - padL - padR, plotH = h - padT - padB;
        const isSigned = this.opts.colorMode === 'signed';
        const vMin = isSigned ? Math.min(...row, 0) : Math.min(...row);
        const vMax = isSigned ? Math.max(...row, 0) : Math.max(...row);
        const range = (vMax - vMin) || 1;
        const sx = i => padL + (i / (g.xs.length - 1)) * plotW;
        const sy = v => padT + plotH - ((v - vMin) / range) * plotH;

        // zero gridline for signed series
        if (isSigned) {
            const y0 = sy(0);
            ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(padL, y0); ctx.lineTo(w - padR, y0); ctx.stroke();
        }
        // spot/ATM dashed marker
        if (this.opts.spot != null && g.xs.length > 1) {
            let best = 0, bd = Infinity;
            g.xs.forEach((x, i) => { const d = Math.abs(x - this.opts.spot); if (d < bd) { bd = d; best = i; } });
            const mx = sx(best);
            ctx.save(); ctx.strokeStyle = STRIKE_LINE_COLOR; ctx.setLineDash([3, 3]); ctx.lineWidth = 1.2;
            ctx.beginPath(); ctx.moveTo(mx, padT); ctx.lineTo(mx, padT + plotH); ctx.stroke(); ctx.restore();
            ctx.fillStyle = STRIKE_LINE_COLOR; ctx.font = '8px monospace'; ctx.fillText('SPOT', mx + 2, padT + 8);
        }

        this._lensMarkers().forEach((m, mi) => {
            let bi = 0, bdd = Infinity; g.xs.forEach((x, i) => { const d = Math.abs(x - m.x); if (d < bdd) { bdd = d; bi = i; } });
            const mx = sx(bi); ctx.save(); ctx.strokeStyle = m.color; ctx.setLineDash([2, 3]); ctx.lineWidth = 1.2; ctx.beginPath(); ctx.moveTo(mx, padT); ctx.lineTo(mx, padT + plotH); ctx.stroke(); ctx.restore();
            ctx.fillStyle = m.color; ctx.font = '8px monospace'; ctx.fillText(m.label, Math.min(mx + 2, w - padR - 40), padT + 18 + (mi % 4) * 9);
        });

        // Curve Interpolation: continuous spline-ish line through every node.
        const base = colorForMode(this.opts.colorMode) || (isSigned ? null : NEUTRAL_TEAL);
        ctx.lineWidth = 1.6;
        if (!isSigned) {
            ctx.strokeStyle = base; ctx.beginPath();
            row.forEach((v, i) => { const p = [sx(i), sy(v)]; i ? ctx.lineTo(...p) : ctx.moveTo(...p); });
            ctx.stroke();
        } else {
            // signed: color each line segment by local sign (green above zero / red below)
            for (let i = 1; i < row.length; i++) {
                ctx.strokeStyle = (row[i - 1] + row[i]) / 2 >= 0 ? SIGNED_GREEN : SIGNED_RED;
                ctx.beginPath(); ctx.moveTo(sx(i - 1), sy(row[i - 1])); ctx.lineTo(sx(i), sy(row[i])); ctx.stroke();
            }
        }
        // Data Nodes: a solid dot at every discrete coordinate.
        row.forEach((v, i) => {
            ctx.beginPath(); ctx.arc(sx(i), sy(v), 2.1, 0, Math.PI * 2);
            ctx.fillStyle = isSigned ? (v >= 0 ? SIGNED_GREEN : SIGNED_RED) : base;
            ctx.fill();
        });
        // localized peak marker (largest |value|)
        let peakI = 0; row.forEach((v, i) => { if (Math.abs(v) > Math.abs(row[peakI])) peakI = i; });
        ctx.beginPath(); ctx.arc(sx(peakI), sy(row[peakI]), 3.4, 0, Math.PI * 2);
        ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.2; ctx.stroke();

        // axis labels
        ctx.fillStyle = LABEL_COLOR; ctx.font = '8.5px monospace';
        ctx.fillText(String(g.xs[0]), padL, h - 4);
        ctx.fillText(String(g.xs[g.xs.length - 1]), w - padR - 28, h - 4);
        ctx.save(); ctx.translate(10, padT + plotH / 2); ctx.rotate(-Math.PI / 2);
        ctx.fillText(this.opts.zLabel.length > 14 ? this.opts.zLabel.slice(0, 14) : this.opts.zLabel, 0, 0);
        ctx.restore();

        canvas.onmousemove = (e) => {
            const r = canvas.getBoundingClientRect();
            const mx = (e.clientX - r.left);
            const i = Math.round(((mx - padL) / plotW) * (g.xs.length - 1));
            if (i >= 0 && i < g.xs.length) this._hoverFromMini(dte, g.xs[i], row[i]);
        };
    }

    _hoverFromMini(dte, x, v) {
        this.hoverBox.style.display = 'block';
        const big = Math.abs(v) >= 1e4 || (Math.abs(v) < 1e-2 && v !== 0);
        this.hoverBox.innerHTML = `<div><span class="hr">${this.opts.xLabel}</span> ${x}</div>` +
            `<div><span class="hr">DTE</span> ${dte}</div>` +
            `<div><span class="hr">${this.opts.zLabel}</span> ${big ? v.toExponential(3) : v.toFixed(4)}</div>`;
    }

    _highlightCrossSection() { /* reserved: could scroll/flash the matching mini chart */ }
}
