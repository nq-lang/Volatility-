// pricing_analysis.js - Pricing Surfaces analysis blocks (A-1..A-8): Risk Pricing, Distribution, Options-Market View, Fundamental View,
// Intraday / Weekly Structure, Cross-Asset Cascades. All content comes from the C++ core (`an` per book in the strike_lens payload);
// this file only lays it out. The cascade composer concatenates pre-derived stage pieces: it contains no market logic.
(function () {
    const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const S = { id: null, trig: null, fx: { dxy: 'none', bonds: 'none', vol: 'none' } };
    const lens = () => (window.VT_LENS && VT_LENS.lens) || null;
    const disp = id => (window.VT_REGISTRY && VT_REGISTRY.display[id]) || id;
    const scope = () => (window.VT_SCOPE ? VT_SCOPE.scope() : { mode: 'single' });
    const block = (cls, b) => '<section class="vt-pa-blk ' + cls + '"><h4>' + esc(b.title) + '</h4>' + (!b.ok ? '<div class="vt-lens-empty">Unavailable &mdash; ' + esc(b.reason) + '.</div>' :
        '<p class="vt-pa-head">' + esc(b.headline) + '</p><table class="vt-pa-t"><tbody>' + b.rows.map(r => '<tr><th>' + esc(r.k) + '</th><td>' + esc(r.v) + '</td><td class="n">' + esc(r.n) + '</td></tr>').join('') + '</tbody></table>' +
        b.reads.map(t => '<p class="vt-pa-read">' + esc(t) + '</p>').join('') + '<details class="vt-pa-in"><summary>Inputs used</summary><ul>' + b.inputs.map(i => '<li>' + esc(i) + '</li>').join('') + '</ul></details>') + '</section>';
    const hist = h => { if (!h || !h.ok) return ''; const mx = Math.max(...h.p, 1e-9);
        const bars = h.p.map((p, i) => { const x = h.x0 + (i + .5) * h.dx, z = Math.abs(x - h.spot) / h.sigma, cls = z > 2 ? 'tail' : z > 1 ? 'ext' : 'core'; return '<i class="' + cls + '" style="height:' + Math.max(2, p / mx * 100) + '%" title="' + x.toFixed(2) + ' : ' + (p * 100).toFixed(1) + '%"></i>'; }).join('');
        return '<div class="vt-pa-hist" aria-label="risk-neutral distribution">' + bars + '</div><div class="vt-pa-histx"><span>' + (h.x0).toFixed(0) + '</span><span>spot ' + h.spot.toFixed(2) + '</span><span>' + (h.x0 + h.p.length * h.dx).toFixed(0) + '</span></div><div class="vt-pa-leg"><i class="core"></i>within 1σ <i class="ext"></i>1–2σ <i class="tail"></i>beyond 2σ</div>'; };
    function cascade(a) {
        const C = a.casc; if (!C.triggers.length) return '<section class="vt-pa-blk vt-pa-casc"><h4>Cross-Asset Cascades</h4><div class="vt-lens-empty">Unavailable &mdash; no structural strike to break.</div></section>';
        if (!C.triggers.some(t => t.id === S.trig)) S.trig = C.triggers[0].id;
        const T = C.triggers.find(t => t.id === S.trig), dir = T.dir;
        const sel = '<div class="vt-pa-sel"><label>Trigger <select id="vt-pa-trig">' + C.triggers.map(t => '<option value="' + esc(t.id) + '"' + (t.id === S.trig ? ' selected' : '') + '>' + esc(t.label) + '</option>').join('') + '</select></label>' +
            C.factors.map(f => '<label>' + esc(f.label) + ' <select data-fx="' + f.id + '"><option value="none">not in scenario</option>' + f.states.map(s => '<option value="' + s.s + '"' + (S.fx[f.id] === s.s ? ' selected' : '') + '>' + esc(s.label) + '</option>').join('') + '</select></label>').join('') + '</div>';
        const live = C.factors.map(f => '<span class="vt-pa-chip ' + (f.live ? 'on' : 'off') + '" title="' + esc(f.note) + '">' + esc(f.label) + ': ' + (f.live ? esc(f.live_state) : 'no feed') + '</span>').join('');
        const stages = T.stages.slice(0, 3).concat(C.factors.filter(f => S.fx[f.id] !== 'none').map(f => { const st = f.states.find(s => s.s === S.fx[f.id]); return { t: '→ ' + f.label + ' ' + st.label + ' (scenario assumption)', x: st[dir] }; }));
        if (S.fx.dxy !== 'none' && S.fx.bonds !== 'none') { const p = C.pairs.find(q => q.a === S.fx.dxy && q.b === S.fx.bonds); if (p) stages.push({ t: '→ Combined: dollar and bonds interact', x: p.x }); }
        stages.push.apply(stages, T.stages.slice(3));
        return '<section class="vt-pa-blk vt-pa-casc"><h4>Cross-Asset Cascades <small>derived from this book’s strikes; macro states are hypotheses you set, live readings shown beside</small></h4>' + sel + '<div class="vt-pa-live">' + live + '</div>' +
            '<ol class="vt-pa-stages">' + stages.map(s => '<li><b>' + esc(s.t) + '</b><span>' + esc(s.x) + '</span></li>').join('') + '</ol>' +
            '<div class="vt-pa-two"><div><h5>Downstream levels</h5><ul>' + T.downstream.map(d => '<li>' + esc(d) + '</li>').join('') + '</ul></div><div><h5>What stops it</h5><ul>' + T.stops.map(d => '<li>' + esc(d) + '</li>').join('') + '</ul></div></div>' +
            (T.fut != null ? '<div class="vt-pa-fut">Trigger in futures terms: <b>' + T.fut.toLocaleString('en-US') + '</b></div>' : '') +
            '<details class="vt-pa-in"><summary>Inputs used</summary><ul>' + T.inputs.map(i => '<li>' + esc(i) + '</li>').join('') + '</ul></details></section>'; }
    function render() {
        const el = document.getElementById('vt-pa'); if (!el) return; const lz = lens();
        if (!lz || scope().mode === 'single') { el.hidden = true; return; }
        const books = lz.books.filter(b => b.an); if (!books.length) { el.hidden = true; return; }
        if (!books.some(b => b.id === S.id)) S.id = (scope().ticker && books.some(b => b.id === scope().ticker)) ? scope().ticker : books[0].id;
        const b = books.find(x => x.id === S.id), a = b.an; el.hidden = false;
        el.innerHTML = '<div class="vt-pa-h"><b>Pricing Analysis</b><span class="vt-pa-books" role="group">' + books.map(x => '<button class="vt-pa-bk' + (x.id === S.id ? ' on' : '') + '" data-id="' + x.id + '">' + esc(disp(x.id)) + '</button>').join('') + '</span></div>' +
            '<div class="vt-pa-grid">' + block('risk', a.risk) + '<section class="vt-pa-blk dist"><h4>Distribution</h4>' + (a.dist.ok ? hist(a.hist) + '<p class="vt-pa-head">' + esc(a.dist.headline) + '</p><table class="vt-pa-t"><tbody>' + a.dist.rows.map(r => '<tr><th>' + esc(r.k) + '</th><td>' + esc(r.v) + '</td><td class="n">' + esc(r.n) + '</td></tr>').join('') + '</tbody></table>' + a.dist.reads.map(t => '<p class="vt-pa-read">' + esc(t) + '</p>').join('') + '<details class="vt-pa-in"><summary>Inputs used</summary><ul>' + a.dist.inputs.map(i => '<li>' + esc(i) + '</li>').join('') + '</ul></details>' : '<div class="vt-lens-empty">Unavailable &mdash; ' + esc(a.dist.reason) + '.</div>') + '</section>' +
            block('opt', a.options) + block('fund', a.fund) + block('intra', a.intraday) + block('week', a.weekly) + '</div>' + cascade(a);
        el.querySelectorAll('.vt-pa-bk').forEach(x => x.addEventListener('click', () => { S.id = x.dataset.id; S.trig = null; render(); }));
        const tr = el.querySelector('#vt-pa-trig'); if (tr) tr.addEventListener('change', () => { S.trig = tr.value; render(); });
        el.querySelectorAll('select[data-fx]').forEach(x => x.addEventListener('change', () => { S.fx[x.dataset.fx] = x.value; render(); }));
    }
    const prev = () => window.onStrikeLens;
    window.addEventListener('DOMContentLoaded', () => { const p = prev(); window.onStrikeLens = pl => { if (p) p(pl); render(); }; if (window.VT_SCOPE) VT_SCOPE.onChange(() => render()); });
    window.VT_PA = { render, state: S };
})();
