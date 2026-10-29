// volatility.js - Volatility section (C-1/C-2/C-4). Analysis of VIX / VXN comes from the C++ core (vol_index.h) over the supplied history;
// this file only renders it. Missing feed / short history => explicit "unavailable" states, never a guessed regime.
// Also exposes VT_VOL.regimeFor(ticker) / VT_VOL.stripHtml(ticker) so other modules (Strike Lens, Hedge Flow, sidebar, Pricing) read the same vol regime (C-3).
(function () {
    const V = { data: null };
    const R = () => window.VT_REGISTRY || { volatility: [], vol_links: {} };
    const isVol = t => (R().volatility || []).includes(t);
    const parse = p => (typeof p === 'string' ? JSON.parse(p) : p);
    const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const ordn = n => n + ((n % 100 >= 11 && n % 100 <= 13) ? 'th' : ['th','st','nd','rd'][n % 10 > 3 ? 0 : n % 10]);
    const f = (v, d = 2) => v == null ? 'n/a' : Number(v).toFixed(d);
    const sgn = (v, d = 2) => v == null ? 'n/a' : (v >= 0 ? '+' : '−') + Math.abs(v).toFixed(d);
    const when = ts => ts ? new Date(ts * 1000).toISOString().slice(0, 16).replace('T', ' ') + 'Z' : 'n/a';
    // which vol index measures a given book (registry vol_links), e.g. QQQ/NQ -> VXN, SPX/ES -> VIX
    V.indexFor = id => { const L = R().vol_links || {}; for (const k of Object.keys(L)) if ((L[k].books || []).includes(id) || L[k].anchor === id) return k; return null; };
    V.get = id => V.data && V.data.indices && V.data.indices[id] || null;
    V.regimeFor = book => { const id = isVol(book) ? book : V.indexFor(book); const a = id && V.get(id);
        if (!a) return { id, available: false, reason: id ? 'no ' + id + ' data received yet' : 'no volatility index linked to ' + book }; return a; };
    V.divergence = () => V.data && V.data.divergence || null;
    // compact one-line reading used by other modules; always states unavailability instead of hiding
    V.stripHtml = book => { const a = V.regimeFor(book); if (!a.id) return '';
        if (!a.available) return '<span class="vt-volstrip na"><b>' + esc(a.id) + '</b> unavailable &mdash; ' + esc(a.reason) + '</span>';
        return '<span class="vt-volstrip"><b>' + esc(a.id) + '</b> ' + f(a.level, 2) + (a.chg_1d != null ? ' (' + sgn(a.chg_1d) + ' 1d)' : '') + (a.regime !== 'unknown' ? ' &middot; ' + a.regime + ' (' + ordn(Math.round(a.pct_rank * 100)) + ' pct)' : ' &middot; regime withheld') +
            (a.term_state !== 'unavailable' ? ' &middot; ' + a.term_state : '') + '</span>'; };
    function card(id) {
        const a = V.get(id); const lab = (R().display || {})[id] || id;
        const head = '<b class="h">Volatility &middot; <em>' + esc(lab) + '</em></b>';
        if (!a || !a.available) return '<div class="vt-vol-row">' + head + '<span class="na">Unavailable &mdash; ' + esc(a ? a.reason : 'no volatility feed received') + '</span></div>';
        const pos = Math.max(0, Math.min(1, a.range_pos)) * 100, cls = a.regime === 'stress' || a.regime === 'elevated' ? 'neg' : a.regime === 'compressed' ? 'pos' : '';
        const chip = (k, v, c, t) => '<span class="c" title="' + esc(t || '') + '"><small>' + k + '</small><b class="' + (c || '') + '">' + v + '</b></span>';
        const chips = chip('LEVEL', f(a.level, 2), '', 'annualised implied vol, index points') + chip('1D', sgn(a.chg_1d), a.chg_1d > 0 ? 'neg' : 'pos', 'change vs prior close') + chip('5D', sgn(a.chg_5d), a.trend > 0 ? 'neg' : a.trend < 0 ? 'pos' : '', '5-day change; trend ' + a.trend) +
            chip('PCTL', a.regime === 'unknown' ? 'n/a' : ordn(Math.round(a.pct_rank * 100)), cls, 'share of last ' + a.n + ' sessions below today') + chip('REGIME', a.regime, cls, 'percentile bands: <20 compressed, <60 normal, <85 elevated, else stress') +
            chip('TERM', a.term_state === 'unavailable' ? 'n/a' : a.term_state, a.term_state === 'backwardation' ? 'neg' : '', a.term_slope == null ? 'no front/back levels supplied' : 'back vs front ' + sgn(a.term_slope * 100, 1) + '%') +
            chip('IV\u2212RV', a.iv_rv == null ? 'n/a' : sgn(a.iv_rv * 100, 1), '', a.iv_rv == null ? 'no realised vol supplied' : 'vol points') +
            '<span class="c g" title="position in ' + a.n + '-session range ' + f(a.lo, 1) + ' to ' + f(a.hi, 1) + '"><small>RANGE ' + f(a.lo, 1) + '&ndash;' + f(a.hi, 1) + '</small><i><u style="left:' + pos.toFixed(0) + '%"></u></i></span>';
        const dv = V.divergence(); const div = dv && dv.available ? '<div class="vt-vol-div ' + dv.state + '">' + esc(dv.text) + '</div>' : '';
        const links = (a.books || []).length ? 'feeds ' + a.books.map(b => esc((R().display || {})[b] || b)).join(' ') : '';
        const na = (a.notes || []).length ? ' &middot; <span class="na">' + a.notes.map(esc).join(' &middot; ') + '</span>' : '';
        return '<div class="vt-vol-row">' + head + chips + '</div><div class="vt-vol-concl">' + esc(a.effect) + '</div>' + div +
            '<div class="vt-vol-ts">as of ' + when(a.as_of) + ' &middot; conf ' + Math.round(a.confidence * 100) + '% &middot; ' + a.n + ' sessions &middot; ' + links + na + '</div>';
    }
    V.render = function () {
        const el = document.getElementById('vt-vol'); if (!el) return;
        const t = window.VT_SCOPE ? VT_SCOPE.scope().ticker : null;
        if (!t || !isVol(t)) { el.hidden = true; el.innerHTML = ''; V.listeners.forEach(fn => { try { fn(); } catch (e) {} }); return; }
        el.hidden = false; el.innerHTML = card(t); V.listeners.forEach(fn => { try { fn(); } catch (e) {} });
    };
    V.listeners = [];
    window.onVolatility = p => { V.data = parse(p); V.render(); };
    window.VT_VOL = V;
    window.addEventListener('DOMContentLoaded', () => { if (window.VT_SCOPE) VT_SCOPE.onChange(V.render); V.render(); });
})();
