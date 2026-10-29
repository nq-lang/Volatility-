// sidebar_xbook.js - cross-book "What Dealers Are Forced To Do" layer.
//   * RIGHT NOW: severity-ranked alerts (also shown as a slim bar on EVERY page)
//   * KEY CONTROL STRIKES: zero-gamma, call/put walls, expected-move bounds. Weighted across in-scope books, shown "strike / futures" (e.g. QQQ $574.5 / NQ $24,475)
//   * CROSS-BOOK: per-book weight, gamma sign, flip offset vs the consensus, 0DTE share; outliers flagged
//   * SCOPED READ: the validated LLM analysis for the current (scope, snapshot), badged with its scope
// Everything is read from the payloads the C++ core publishes (multibook, strike_lens, regime_live, scoped_insight, multibook_model). Nothing is computed here
// beyond ranking, medians and distances of numbers that were supplied. In Single-Book scope only that one book's own numbers are used.
(function () {
    const X = { scoped: null, regime: null, model: null };
    const disp = id => (window.VT_REGISTRY && VT_REGISTRY.display[id]) || id;
    const sc = () => (window.VT_SCOPE ? VT_SCOPE.scope() : { mode: 'single', ticker: 'SPX' });
    const lensOn = () => sc().mode !== 'single' && window.VT_LENS && VT_LENS.lens && VT_LENS.mb;
    const fmt = (v, d) => v == null ? 'n/a' : (+v).toLocaleString('en-US', { minimumFractionDigits: d ?? 2, maximumFractionDigits: d ?? 2 });
    const money = (v, t) => '$' + fmt(v, t && t < 1 ? Math.min(3, (String(t).split('.')[1] || '').length) : (Math.abs(v) >= 1000 ? 0 : 2));
    const sgn = (v, d) => (v > 0 ? '+' : '') + fmt(v, d ?? 0);
    const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    function wmedian(a) { a = a.filter(x => x.v != null && x.w > 0).sort((p, q) => p.v - q.v); const tw = a.reduce((s, x) => s + x.w, 0); if (!tw) return null; let acc = 0; for (const x of a) { acc += x.w; if (acc >= tw / 2) return x.v; } return a[a.length - 1].v; }
    // ---- data assembled from the payloads (family scope)
    function family() {
        const L = VT_LENS.lens, M = VT_LENS.mb, w = {}; (M.books || []).forEach(b => { if (b.included) w[b.id] = b; });
        const books = L.books.filter(b => w[b.id]).map(b => ({ ...b, w: w[b.id].weight, mb: w[b.id] }));
        const lv = (b, name, set) => { const x = (set === 'pricing' ? b.pricing : b.levels).find(l => l.label === name); return x && x.fut != null ? x.fut : null; };
        const med = (name, set) => wmedian(books.filter(b => b.convertible).map(b => ({ v: lv(b, name, set), w: b.w })));
        return { L, M, books, med, lv, F: L.anchor_px, tick: L.tick, an: L.anchor };
    }
    // "BOOK $strike / ANCHOR $fut": strike first, then futures, per the terminal convention
    function both(f, F, bookId) {
        const s = sc(), id = bookId || (s.mode === 'family' ? (f.books.filter(b => b.convertible).sort((a, b) => b.w - a.w)[0] || {}).id : s.ticker);
        const k = id && id !== f.an && VT_LENS.fromFut ? VT_LENS.fromFut(id, F) : null;
        return (k != null ? disp(id) + ' ' + money(k) + ' / ' : '') + f.an + ' ' + money(F, f.tick); }
    function alerts() {
        const out = [], push = (sev, tag, text) => out.push({ sev, tag, text }), s = sc(), R = X.regime;
        if (lensOn()) {
            const f = family(), M = f.M, flipM = f.med('Gamma Flip'), F = f.F;
            const agree = M.sign_agreement, short = M.raw_net_gex < 0;
            if (Math.abs(agree) >= 0.8) push(short ? 2 : 1, 'GAMMA', 'Books agree: net ' + (short ? 'SHORT' : 'long') + ' gamma (agreement ' + fmt(agree) + ') &mdash; ' + (short ? 'dealer hedging would amplify moves' : 'dealer hedging would dampen moves') + ', if dealers are positioned as assumed');
            else push(3, 'GAMMA', 'Books DISAGREE on the gamma sign (agreement ' + fmt(agree) + '): read each book, not the sum');
            if (flipM != null && F) { const d = (flipM - F) / F * 100; push(Math.abs(d) < 0.3 ? 3 : 1, 'ZERO-GAMMA', (Math.abs(d) < 0.3 ? 'Spot is within ' + fmt(Math.abs(d)) + '% of the zero-gamma level ' : 'Zero-gamma ') + both(f, flipM) + ' &mdash; ' + sgn(d, 2) + '% from spot'); }
            if (M.flip_confluence < 0.6) push(2, 'CONFLUENCE', 'Flip levels differ across books: only ' + Math.round(M.flip_confluence * 100) + '% of weight agrees');
            const h = M.hedge; if (h) { const i1 = h.shock_pct.indexOf(-1), j1 = h.shock_pct.indexOf(1); if (i1 >= 0 && j1 >= 0) { const dn = h.anchor_contracts[i1], up = h.anchor_contracts[j1];
                push(dn < 0 ? 2 : 1, 'HEDGE FLOW', '&minus;1%: dealers ' + (dn < 0 ? 'SELL' : 'buy') + ' ' + fmt(Math.abs(dn), 0) + ' ' + f.an + '-equiv &middot; +1%: ' + (up < 0 ? 'sell' : 'BUY') + ' ' + fmt(Math.abs(up), 0) + (dn < 0 && up > 0 ? ' &mdash; pro-cyclical both ways' : '')); }
                if (h.charm_1d_c) push(1, 'CHARM/VANNA', 'Charm 1d ' + sgn(h.charm_1d_c) + ' &middot; vanna IV+1pt ' + sgn(h.vanna_up_c) + ' ' + f.an + '-equiv contracts'); }
            const back = f.books.filter(b => b.vol && b.vol.backwardated).map(b => disp(b.id)); if (back.length) push(2, 'VOL', 'Term structure backwardated in ' + back.join(', ') + ' &mdash; near-dated vol is bid');
            const z = f.books.map(b => ({ id: b.id, z: ((M.hedge || {}).per_book || []).find(p => p.id === b.id) })).filter(x => x.z && x.z.zero_dte_gamma_share > 0.3); z.forEach(x => push(2, '0DTE', disp(x.id) + ': ' + Math.round(x.z.zero_dte_gamma_share * 100) + '% of gamma expires today'));
            const ex = (M.books || []).filter(b => !b.included); if (M.confidence < 1 || ex.length) push(2, 'DATA', 'Confidence ' + Math.round(M.confidence * 100) + '%' + (ex.length ? '; excluded: ' + ex.map(b => disp(b.id)).join(', ') : ''));
        } else if (typeof flow !== 'undefined') {
            const fl = flow; if (fl.flipRegime) push(fl.flipRegime === 'short_gamma' ? 2 : 1, 'GAMMA', disp(s.ticker) + ' is ' + (fl.flipRegime === 'short_gamma' ? 'SHORT' : 'long') + ' gamma vs its flip' + (fl.flipDistance != null ? ' (' + sgn(fl.flipDistance, 1) + ' pts)' : ''));
            if (fl.flipDistance != null && Math.abs(fl.flipDistance) < 15) push(3, 'ZERO-GAMMA', 'Spot is ' + fmt(Math.abs(fl.flipDistance), 1) + ' pts from the flip level');
            if (fl.rvIvZ != null && Math.abs(fl.rvIvZ) > 1.5) push(2, 'VOL', 'RV vs IV z-score ' + fmt(fl.rvIvZ) + ' (time-of-day)'); }
        if (R) { const top = R.probabilities.slice().sort((a, b) => b.p - a.p), t = R.triggers || {};
            if (t.hard_active || t.soft_active) push(3, 'REGIME', (t.hard_active ? 'HARD' : 'Soft') + ' transition trigger ACTIVE &mdash; ' + top[0].name + ' ' + Math.round(top[0].p * 100) + '%, ' + top[1].name + ' ' + Math.round(top[1].p * 100) + '%');
            else push(1, 'REGIME', top[0].name + ' ' + Math.round(top[0].p * 100) + '% (distribution, not a label)'); }
        if (sc().mode !== 'single' && X.model && X.model.available === false) push(1, 'MODEL', 'multibook_v1 unavailable (' + esc(X.model.reason || '') + ')');
        return out.sort((a, b) => b.sev - a.sev);
    }
    function levels() {
        if (lensOn()) { const f = family(), F = f.F, rows = [], add = (name, v) => { if (v != null) rows.push({ name, v }); };
            add('Zero-gamma', f.med('Gamma Flip')); add('Call wall', f.med('Call Wall')); add('Put wall', f.med('Put Wall')); add('Exp move +1&sigma;', f.med('Exp Move +1s', 'pricing')); add('Exp move &minus;1&sigma;', f.med('Exp Move -1s', 'pricing'));
            return rows.sort((a, b) => b.v - a.v).map(r => ({ name: r.name, text: both(f, r.v), d: F ? (r.v - F) / F * 100 : null, tag: r.v > F ? 'up' : 'dn' })); }
        const fl = typeof flow !== 'undefined' ? flow : null, g = fl && fl.gex && fl.gex.term, P = typeof currentSpot !== 'undefined' ? currentSpot : null; if (!fl) return [];
        const row = (name, v) => v == null ? null : { name, text: disp(sc().ticker) + ' ' + money(+v), d: P ? (v - P) / P * 100 : null, tag: P && v > P ? 'up' : 'dn' };
        return [row('Call wall', g && g.callWall), row('Zero-gamma', fl.flipLevel), row('Put wall', g && g.putWall)].filter(Boolean); }
    function crossBook() {
        if (!lensOn()) return ''; const f = family(), flipM = f.med('Gamma Flip'), F = f.F;
        const rows = f.books.slice().sort((a, b) => b.w - a.w).map(b => { const flip = f.lv(b, 'Gamma Flip'), off = flip != null && flipM != null ? (flip - flipM) / F * 100 : null, short = b.mb.net_gex < 0, hz = (((f.M.hedge || {}).per_book || []).find(p => p.id === b.id) || {}).zero_dte_gamma_share;
            const out = off != null && Math.abs(off) > 0.5;
            return '<div class="vt-xb-row' + (out ? ' out' : '') + '"><span class="n">' + disp(b.id) + '</span><span class="bar"><i style="width:' + Math.round(b.w * 100) + '%"></i></span><span class="w">' + Math.round(b.w * 100) + '%</span><span class="g ' + (short ? 'v-bear' : 'v-bull') + '">' + (short ? 'short' : 'long') + ' &gamma;</span>' +
                '<span class="o">' + (off == null ? 'flip n/a' : 'flip ' + sgn(off, 2) + '%') + '</span><span class="z">' + (hz != null && hz > 0 ? Math.round(hz * 100) + '% 0DTE' : '') + '</span>' + (b.vol && b.vol.backwardated ? '<span class="bk">backwardated</span>' : '') + '</div>'; });
        const off = (f.M.books || []).filter(b => !b.included).map(b => '<div class="vt-xb-row off"><span class="n">' + disp(b.id) + '</span><span class="why">excluded: ' + esc(b.reason) + '</span></div>');
        return '<div class="vt-flow-section vt-xb"><div class="vt-flow-section-title">Cross-book &mdash; who is pushing</div>' + rows.join('') + off.join('') + '</div>'; }
    function scopedCard() {
        const S = X.scoped; if (!S) return ''; const cur = sc(); if (S.scope.mode !== cur.mode || (cur.mode === 'single' && S.scope.ticker !== cur.ticker)) return '';   // never show a read generated under another scope
        const lines = String(S.text).split('\n').filter(l => l.trim()), head = lines.filter(l => /^\s*[1-3][.)]/.test(l) || /^MOCK/.test(l)), rest = lines.filter(l => !head.includes(l));
        return '<div class="vt-flow-section vt-xb-read"><div class="vt-flow-section-title">Scoped read <span class="vt-xb-badge">' + S.scope.mode.replace('_', '-').toUpperCase() + ' &middot; ' + esc(S.scope.family || disp(S.scope.ticker)) + ' &middot; ' + S.scope.books.length + (S.scope.books.length === 1 ? ' book' : ' books') + '</span>' +
            '<span class="vt-xb-badge ' + (S.validated ? 'ok' : 'warn') + '">' + (S.validated ? 'numbers verified' : 'computed summary (model text rejected)') + '</span></div>' + head.map(l => '<div class="vt-xb-line">' + esc(l) + '</div>').join('') +
            (rest.length ? '<details><summary>' + rest.length + ' more sections</summary>' + rest.map(l => '<div class="vt-xb-line">' + esc(l) + '</div>').join('') + '</details>' : '') + '<div class="vt-xb-ts">' + esc(S.scope.model_variant) + ' &middot; ' + esc(S.scope.snapshot_ts) + (S.cached ? ' &middot; cached' : '') + '</div></div>'; }
    // ---- B3: dealer Greeks tracked through the session (open -> one hour before the close), interpreted. All numbers/sentences come from the C++ session tracker.
    function spark(pts, key) {
        const W = 260, H = 46, vals = pts.map(p => p[key]).filter(v => v != null); if (vals.length < 2) return '<div class="vt-xb-sp na">only one snapshot so far: no trend line yet</div>';
        const lo = Math.min(0, ...vals), hi = Math.max(0, ...vals), sy = v => H - 4 - (v - lo) / ((hi - lo) || 1) * (H - 8), sx = m => 4 + Math.max(0, Math.min(390, m)) / 390 * (W - 8), zy = sy(0);
        const d = pts.filter(p => p[key] != null).map((p, i) => (i ? 'L' : 'M') + sx(p.min).toFixed(1) + ' ' + sy(p[key]).toFixed(1)).join(' '), last = pts[pts.length - 1];
        return '<svg class="vt-xb-sp" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none" role="img" aria-label="charm through the session"><line x1="4" x2="' + (W - 4) + '" y1="' + zy.toFixed(1) + '" y2="' + zy.toFixed(1) + '" class="z"/><line x1="' + sx(330).toFixed(1) + '" x2="' + sx(330).toFixed(1) + '" y1="0" y2="' + H + '" class="w"><title>tracking window ends one hour before the close</title></line><path d="' + d + '" class="l"/><circle cx="' + sx(last.min).toFixed(1) + '" cy="' + sy(last[key]).toFixed(1) + '" r="2.6" class="n"/></svg>';
    }
    function sessionCard() {
        const s0 = sc(); const title = '<div class="vt-flow-section-title">Dealer Greeks through the session</div>';
        if (s0.mode === 'single') return '<div class="vt-flow-section vt-xb-sess">' + title + '<div class="vt-xb-na">Session Greek tracking is a Multi-Book / Family read. Single-Book keeps the original single_v1 reading unchanged.</div></div>';
        if (!lensOn()) return '<div class="vt-flow-section vt-xb-sess">' + title + '<div class="vt-xb-na">Unavailable &mdash; waiting for the multi-book payload.</div></div>';
        const S = VT_LENS.mb.session; if (!S) return '<div class="vt-flow-section vt-xb-sess">' + title + '<div class="vt-xb-na">Unavailable &mdash; the session tracker is not connected to this feed.</div></div>';
        if (!S.ok) return '<div class="vt-flow-section vt-xb-sess">' + title + '<div class="vt-xb-na">Unavailable &mdash; ' + esc(S.reason || 'no history') + '. Nothing is back-filled.</div></div>';
        const key = S.pts.some(p => p.charm_close != null) ? 'charm_close' : 'charm_1d', an = VT_LENS.lens.anchor;
        const reads = (S.reads || []).map(g => '<div class="vt-xb-gr ' + g.state + '"><span class="n">' + esc(g.name) + '</span><span class="st">' + esc(g.state) + '</span><span class="t">' + esc(g.text.replace(/^[^:]*:\s*/, '')) + '</span></div>').join('');
        const obs = (S.observed || [])[0];
        return '<div class="vt-flow-section vt-xb-sess">' + title + '<div class="vt-xb-win">as of ' + esc(S.as_of) + ' &middot; tracking ' + esc(S.window) + ' &middot; ' + S.n + ' snapshot' + (S.n > 1 ? 's' : '') + '</div>' +
            '<div class="vt-xb-conc">' + esc(S.charm_headline) + '</div>' + spark(S.pts, key) + '<div class="vt-xb-sub">charm ' + (key === 'charm_close' ? 'to the close' : 'per day') + ', ' + an + '-equivalent contracts, open &rarr; now (line = window end, one hour before the close)</div>' + reads +
            '<div class="vt-xb-line"><b>Vanna &times; charm:</b> ' + esc(S.interaction) + '</div><div class="vt-xb-line"><b>Into the close:</b> ' + esc(S.closing) + '</div>' + (obs ? '<div class="vt-xb-line"><b>Observed (' + esc(obs.window) + '):</b> ' + esc(obs.reading) + '</div>' : '') +
            '<div class="vt-xb-line"><b>Volatility:</b> ' + (window.VT_VOL ? VT_VOL.stripHtml(an) : esc(S.vol)) + '</div><div class="vt-xb-ts">Dealers assumed long calls / short puts (not observed). Window ends one hour before the close; the final hour is the handoff.</div></div>';
    }
    // C-3: volatility regime reading and vanna / vomma interpretation (shared object from the core)
    function volCard() {
        const title = '<div class="vt-flow-section-title">Volatility regime &mdash; vanna &amp; vomma</div>';
        if (sc().mode === 'single') return ''; if (!lensOn()) return '';
        const v = VT_LENS.mb && VT_LENS.mb.volctx; if (!v) return '<div class="vt-flow-section vt-xb-vol">' + title + '<div class="vt-xb-na">Unavailable &mdash; no volatility context in the payload.</div></div>';
        if (!v.ok) return '<div class="vt-flow-section vt-xb-vol">' + title + '<div class="vt-xb-na">Unavailable &mdash; ' + esc(v.reason) + '.</div></div>';
        return '<div class="vt-flow-section vt-xb-vol">' + title + '<div class="vt-xb-conc">' + esc(v.headline) + '</div><div class="vt-xb-line"><b>Hedging:</b> ' + esc(v.hedge) + '</div><div class="vt-xb-line"><b>Vomma:</b> ' + esc(v.vomma) + '</div>' + (v.divergence ? '<div class="vt-xb-line"><b>VXN vs VIX:</b> ' + esc(v.divergence) + '</div>' : '') + '</div>'; }
    // key strikes with context (what each implies, how long it is valid, conviction) from the Strike Lens intraday cards of the primary book
    function strikeContext() {
        if (!lensOn()) return ''; const L = VT_LENS.lens, s0 = sc(), id = s0.mode === 'family' ? (L.books.filter(b => b.convertible && b.insight && b.insight.intraday.ok).sort((a, b) => 0)[0] || {}).id : s0.ticker, b = id && L.books.find(x => x.id === id);
        if (!b || !b.insight) return ''; const H = b.insight.intraday.ok ? b.insight.intraday : b.insight.swinging; if (!H.ok) return '<div class="vt-xb-na">Strike context unavailable &mdash; ' + esc(H.reason) + '</div>';
        return '<div class="vt-flow-section"><div class="vt-flow-section-title">High-exposure strikes &mdash; what they imply <small>' + disp(id) + ' &middot; ' + esc(H.name) + ' &middot; ' + esc(H.as_of) + '</small></div>' + H.cards.slice(0, 4).map(c => '<div class="vt-xb-sc"><b>' + esc(VT_LENS.strikeLabel ? VT_LENS.strikeLabel(id, c.strike) : '$' + c.strike) + '</b> <span class="vd">' + esc(c.verdict) + '</span> <span class="' + (c.gamma > 0 ? 'pos' : 'neg') + '">' + (c.gamma > 0 ? '+' : '−') + (Math.abs(c.gamma) / 1e6).toFixed(1) + 'M/1%</span><div class="t">' + esc(c.pb.style) + ' &middot; valid until ' + esc(c.valid_until) + ' &middot; conviction ' + esc(c.conviction.split(':')[0]) + '</div></div>').join('') + '</div>';
    }
    function html() {
        const s = sc(), M = lensOn() ? VT_LENS.mb : null, al = alerts(), lv = levels();
        const scopeLine = '<div class="vt-xb-scope"><b>' + (s.mode === 'single' ? 'SINGLE-BOOK &middot; ' + disp(s.ticker) : s.mode.replace('_', '-').toUpperCase() + ' &middot; ' + esc(s.family || '') + ' &middot; ' + (M ? M.books.filter(b => b.included).length : 0) + ' books') + '</b>' +
            '<span>' + (M ? 'confidence ' + Math.round(M.confidence * 100) + '% &middot; ' : '') + (s.mode === 'single' ? 'single_v1' : 'multibook_v1') + '</span></div>';
        const now = '<div class="vt-flow-section vt-xb-now"><div class="vt-flow-section-title">Right now &mdash; ranked</div>' + (al.length ? al.slice(0, 6).map(a => '<div class="vt-xb-al s' + a.sev + '"><span class="tag">' + a.tag + '</span><span class="tx">' + a.text + '</span></div>').join('') : '<div class="vt-placeholder-note">awaiting data</div>') + '</div>';
        const ctl = '<div class="vt-flow-section"><div class="vt-flow-section-title">Key control strikes' + (lensOn() ? ' <small>weighted median across books</small>' : '') + '</div>' + (lv.length ? lv.map(r => '<div class="vt-xb-lv ' + r.tag + '"><span class="n">' + r.name + '</span><span class="p">' + r.text + '</span><span class="d">' + (r.d == null ? '' : sgn(r.d, 2) + '%') + '</span></div>').join('') : '<div class="vt-placeholder-note">awaiting levels</div>') + '</div>';
        return scopeLine + now + sessionCard() + volCard() + strikeContext() + ctl + crossBook() + scopedCard(); }
    function bar() {
        const el = document.getElementById('vt-alertbar'); if (!el) return; const al = alerts().filter(a => a.sev >= 2).slice(0, 3);
        el.hidden = !al.length; el.innerHTML = al.length ? '<span class="lbl">RIGHT NOW</span>' + al.map(a => '<span class="al s' + a.sev + '"><b>' + a.tag + '</b> ' + a.text + '</span>').join('') : ''; }
    function refresh() { const el = document.getElementById('vt-sidebar-xb'); if (el) el.innerHTML = html(); bar(); }
    window.onScopedInsight = p => { X.scoped = typeof p === 'string' ? JSON.parse(p) : p; refresh(); };
    const prevModel = window.onMultibookModel; window.onMultibookModel = p => { X.model = typeof p === 'string' ? JSON.parse(p) : p; if (prevModel) prevModel(p); refresh(); };
    window.VT_SIDEBAR = { html, refresh, alerts, levels, setRegime: r => { X.regime = r; refresh(); }, state: X };
    window.addEventListener('DOMContentLoaded', () => { if (window.VT_LENS) VT_LENS.listeners.push(refresh); if (window.VT_SCOPE) VT_SCOPE.onChange(refresh); refresh(); });
})();
