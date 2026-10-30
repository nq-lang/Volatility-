// cf_mock.js - EXAMPLE-ONLY data for layers whose feeds are not connected yet: 1m bars + volume + CVD, L2 liquidity streaks, forensics flags,
// block prints, bid/ask size and session reference levels (SET / PDL / ONL / wOPEN). Deterministic (seeded) and anchored so the last candle
// closes at the real axis price. Every consumer stamps what it draws "EXAMPLE". Replace with the real order-flow API; no model number reads this.
(function () {
    function rng(seed) { let s = seed >>> 0; return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }; }
    // anchors: [{t (minute), spot}] model-epoch prices; the example candle path is bridged through them so candles and levels agree in time
    function make(spot, nowMin, tick, emPts, anchors) {
        const R = rng(20261021), NB = 200, first = Math.max(0, Math.round(nowMin) - NB + 1), n = Math.round(nowMin) - first + 1;
        const vol = Math.max(emPts || spot * 0.01, spot * 0.004) * 0.03; let p = spot; const w = [];
        for (let i = 0; i < n; i++) { p = p + (R() - 0.5) * vol * 1.2 - (p - spot) * 0.012; w.push(p); }
        const an = (anchors || []).filter(a => a.t >= first && a.t <= first + n - 1).sort((x, y) => x.t - y.t); if (!an.length || an[an.length - 1].t < nowMin) an.push({ t: Math.round(nowMin), spot });
        const cl = w.slice(); let prevT = null, prevAdj = null;   // piecewise bridge: walk + linear correction so close(t_anchor) == anchor spot
        an.forEach((a, k) => { const i1 = Math.round(a.t) - first, adj1 = a.spot - w[i1]; if (k === 0) { for (let i = 0; i <= i1; i++) cl[i] = w[i] + adj1; } else { const i0 = Math.round(prevT) - first; for (let i = i0; i <= i1; i++) cl[i] = w[i] + prevAdj + (adj1 - prevAdj) * ((i - i0) / Math.max(1, i1 - i0)); } prevT = a.t; prevAdj = adj1; });
        const lastI = Math.round(prevT) - first; for (let i = lastI + 1; i < n; i++) cl[i] = w[i] + prevAdj;
        const raw = cl.map((c, i) => { const o = i ? cl[i - 1] : c - (R() - 0.5) * vol; return { o, c, h: Math.max(o, c) + R() * vol * 0.45, l: Math.min(o, c) - R() * vol * 0.45 }; });
        const shift = 0; let cvd = 0; const cv = [], bars = [];
        raw.forEach((b, i) => { const o = b.o + shift, c = b.c + shift, v = Math.round(300 + R() * 900 + Math.abs(c - o) / vol * 600), dl = Math.round((c - o) / vol * v * 0.35 + (R() - 0.5) * v * 0.2); cvd += dl; bars.push({ t: first + i, o, h: b.h + shift, l: b.l + shift, c, v, d: dl }); cv.push(cvd); });
        const lo = Math.min(...bars.map(b => b.l)), hi = Math.max(...bars.map(b => b.h));
        const session = { SET: bars[Math.floor(n * 0.45)].o + vol * 4, PDL: lo - vol * 3, ONL: lo - vol * 1.2, wOPEN: bars[0].o - vol * 2 };
        const W = 900, H = 420, c = document.createElement('canvas'); c.width = W; c.height = H; const g = c.getContext('2d'); g.fillStyle = '#020611'; g.fillRect(0, 0, W, H);
        const t0 = first, t1 = nowMin + 60, pLo = spot - spot * 0.03, pHi = spot + spot * 0.03, ty = v => (pHi - v) / (pHi - pLo) * H, tx = m => (m - t0) / (t1 - t0) * W;
        for (let k = 0; k < 46; k++) { const py = spot + (R() - 0.5) * spot * 0.05, a0 = t0 + R() * (n - 20), a1 = a0 + 20 + R() * 100, a = 0.12 + R() * 0.55, y = ty(py);
            const gr = g.createLinearGradient(0, y - 3, 0, y + 3); gr.addColorStop(0, 'rgba(20,70,160,0)'); gr.addColorStop(0.5, `rgba(${a > .55 ? 200 : 40},${a > .55 ? 240 : 150},255,${a})`); gr.addColorStop(1, 'rgba(20,70,160,0)'); g.fillStyle = gr; g.fillRect(tx(a0), y - 3, tx(Math.min(a1, nowMin)) - tx(a0), 6); }
        const kinds = ['sweep', 'absorption', 'iceberg', 'cvd', 'em'], flags = [];
        for (let k = 0; k < 14; k++) { const b = bars[Math.floor(R() * (bars.length - 4)) + 2], up = R() > 0.5; flags.push({ t: b.t, price: up ? b.h : b.l, kind: kinds[k % 5], up }); }
        const blocks = []; for (let k = 0; k < 9; k++) { const b = bars[Math.floor(R() * bars.length)], buy = b.c >= b.o; blocks.push({ t: b.t, price: Math.round((buy ? b.l : b.h) / tick) * tick, size: Math.round(20 + R() * 260), buy }); }
        const bid = Math.round(spot / tick) * tick - tick * 0.5;
        return { bars, cvd: cv, session, heat: { canvas: c, pLo, pHi, t0, t1 }, flags, blocks, book: { bid, ask: bid + tick, bsz: Math.round(8 + R() * 40), asz: Math.round(8 + R() * 40) }, first, nowMin, hi, lo };
    }
    window.CFMock = { make };
})();
