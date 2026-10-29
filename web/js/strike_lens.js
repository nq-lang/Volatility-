// strike_lens.js - Strike Lens. Pricing Surfaces page: market-pricing levels (expected-move bands, RND quantiles/mode, 25-delta strikes).
// greeksChannel page: dealer-Greek levels (flip, walls, top |GEX|). Never mixed. Highlight any in-scope book; levels are translated into
//  - INDX ticker (Multi-Book):  the selected ticker's own strike space (primary); futures equivalent only if toggled on
//  - futures Family:            the anchor futures price (primary)
// All conversion numbers (ratio, basis, per-level futures equivalents) come from the C++ core via the `strike_lens` channel.
// Hidden in Single-Book: Single View is the plain ticker with no futures or other-book content.
(function () {
    const COLORS = ['#7be0c6', '#f0b27a', '#9db7ff', '#ff8fb1', '#d6e36b', '#c39bff'];
    const GREEK = ['gex', 'dex', 'vanna', 'charm', 'speed', 'color', 'theta'];
    const L = { lens: null, mb: null, picked: null, view: 'pricing', showFut: false, customBook: null, hz: 'intraday' };
    try { const h = sessionStorage.getItem('vt_lens_hz'); if (h === 'intraday' || h === 'swinging') L.hz = h; } catch (e) {}   // B1-6: remembered for the session, default Intraday
    const disp = id => (window.VT_REGISTRY && VT_REGISTRY.display[id]) || id;
    const parse = p => (typeof p === 'string' ? JSON.parse(p) : p);
    const book = id => L.lens && L.lens.books.find(b => b.id === id);
    const scopeNow = () => (window.VT_SCOPE ? VT_SCOPE.scope() : { mode: 'single' });
    const active = () => scopeNow().mode !== 'single' && !!L.lens;
    const color = id => COLORS[(L.lens ? L.lens.books.findIndex(b => b.id === id) : 0) % COLORS.length];
    const famMode = () => scopeNow().mode === 'family';
    const isFutTicker = id => !!(window.VT_REGISTRY && (VT_REGISTRY.futures || []).includes(id));
    // B1-1: the visible "Equiv" column exists only when the primary price space is a futures contract (Family, or a futures ticker in focus). Conversion logic is untouched.
    const showEquiv = () => famMode() || isFutTicker(scopeNow().ticker);
    const fmtX = v => { const a = Math.abs(v), sg = v < 0 ? '\u2212' : '+'; return sg + (a >= 1e9 ? (a / 1e9).toFixed(2) + 'B' : a >= 1e6 ? (a / 1e6).toFixed(2) + 'M' : a >= 1e3 ? (a / 1e3).toFixed(1) + 'K' : a.toFixed(0)); };
    const decimals = t => (t && t < 1 ? Math.min(3, (String(t).split('.')[1] || '').length) : 0);
    const fmt = (v, t, dflt) => v == null ? 'n/a' : v.toLocaleString('en-US', { minimumFractionDigits: t != null ? decimals(t) : (dflt ?? 2), maximumFractionDigits: t != null ? decimals(t) : (dflt ?? 2) });
    const levelsOf = (b, view) => (view === 'greeks' ? b.levels : b.pricing) || [];
    // ---- conversions (same formulas as the core: fut = k*ratio + basis, tick-rounded)
    L.toFut = (id, k) => { const b = book(id); if (!b || !b.convertible) return null; const v = k * b.ratio + b.basis, t = L.lens.tick; return t > 0 ? Math.round(v / t) * t : v; };
    L.fromFut = (id, F) => { const b = book(id); if (!b || !b.convertible || !b.ratio) return null; return (F - b.basis) / b.ratio; };
    // level of book `id` expressed in the primary space: ticker strike space (INDX) or futures (family)
    L.primary = (id, lv) => { if (famMode()) return lv.fut; const P = scopeNow().ticker; if (id === P) return lv.strike; return lv.fut == null ? null : L.fromFut(P, lv.fut); };
    // Key Levels (page 1): a level in the primary price space -> one "BOOK $strike / ANCHOR $fut" line per highlighted book (strike first, then futures)
    const money = (v, t) => '$' + (v == null ? 'n/a' : v.toLocaleString('en-US', { minimumFractionDigits: 0, maximumFractionDigits: t && t < 1 ? decimals(t) : 2 }));
    L.equiv = function (price) {
        if (!active() || !L.picked || !L.picked.size) return [];
        const P = scopeNow().ticker, an = L.lens.anchor, F = famMode() || P === an ? price : L.toFut(P, price); if (F == null) return [];
        const out = []; for (const id of L.picked) { const b = book(id); if (!b || !b.convertible) continue;
            const k = id === P && !famMode() ? price : L.fromFut(id, F); if (k == null) continue;
            out.push({ id, text: disp(id) + ' ' + money(k, b.tick_hint || 0.01) + ' / ' + an + ' ' + money(F, L.lens.tick), color: color(id) }); }
        return out; };
    L.chipsHtml = function () { if (!active() || !L.lens) return ''; const P = scopeNow().ticker;
        return L.lens.books.filter(b => b.convertible).map(b => '<button class="vt-lens-chip sm' + (L.picked && L.picked.has(b.id) ? ' on' : '') + '" data-id="' + b.id + '" style="--c:' + color(b.id) + '">' + disp(b.id) + '</button>').join(''); };
    L.toggle = function (id) { if (!L.picked) L.picked = new Set(); L.picked.has(id) ? L.picked.delete(id) : L.picked.add(id); render(); };
    L.listeners = []; const fire = () => L.listeners.forEach(f => { try { f(); } catch (e) {} });
    L.panelApi = function (view) {
        if (!active()) return null; const P = scopeNow().ticker, isAnchor = P === L.lens.anchor, pb = book(P);
        if (!pb && !isAnchor) return null;
        const fromF = isAnchor ? (F => F) : (F => L.fromFut(P, F));
        const toF = isAnchor ? (k => k) : (k => L.toFut(P, k));
        const showFut = famMode() || L.showFut;
        return { anchor: L.lens.anchor, toFut: showFut ? toF : null,
            markers() { const out = []; for (const id of L.picked || []) { const b = book(id); if (!b || !b.convertible) continue;
                for (const lv of levelsOf(b, view)) { if (lv.label === 'Top |GEX|' || lv.fut == null) continue; const x = id === P ? lv.strike : fromF(lv.fut); if (x != null) out.push({ x, label: disp(id) + ' ' + short(lv.label), color: color(id) }); } }
                return out; } };
    };
    const short = l => l.replace('Gamma ', '').replace(' Wall', 'W').replace('Exp Move ', 'EM ').replace('Near Move ', 'NM ').replace('25D ', '25D ');
    function repaintPanels() { const pp = L.panelApi('pricing'), gp = L.panelApi('greeks');
        if (window.__panels) Object.entries(window.__panels).forEach(([ch, p]) => p.panel && p.panel.setLens && p.panel.setLens(GREEK.includes(ch) ? gp : pp)); }
    // ---- T2-5 / T2-6 blocks. Hedge Flows (Greeks page): aggregate dealer re-hedge flow in anchor-equivalent contracts. Vol & Risk (Pricing page): per picked book.
    const sgn = v => (v > 0 ? '+' : '') + v.toLocaleString('en-US', { maximumFractionDigits: 0 });
    const cls = v => v > 0 ? 'pos' : v < 0 ? 'neg' : '';
    // ---- B2 Hedge Flow: every row states trigger, price, time, flow (with sign + reference) and the effect on the ticker; observed vs projected are labelled
    function hedgeBlock() {
        const h = L.mb && L.mb.hedge; if (!h) return '<div class="vt-lens-sec"><h4>Hedge Flow</h4><div class="vt-lens-empty">Unavailable &mdash; no index-convertible books in scope.</div></div>';
        const an = L.lens.anchor, S = L.mb.session, P = scopeNow().ticker, fam = famMode();
        const spot = fam ? L.lens.anchor_px : (P === an ? L.lens.anchor_px : (book(P) || {}).spot), tk = fam ? L.lens.tick : 0.01, tgt = fam ? an : disp(P);
        const px = v => v == null ? 'n/a' : '$' + fmt(v, tk), sg = v => (v > 0 ? '+' : v < 0 ? '−' : '') + Math.abs(v).toLocaleString('en-US', { maximumFractionDigits: 0 }), usd = v => (v < 0 ? '−' : '+') + '$' + (Math.abs(v) / 1e6).toFixed(1) + 'M';
        const bs = v => v > 0 ? 'BUY' : v < 0 ? 'SELL' : 'flat', c0 = v => v > 0 ? 'pos' : v < 0 ? 'neg' : '';
        const maxAbs = Math.max(1, ...h.anchor_contracts.map(Math.abs));
        const rows = [];   // {st: 'observed'|'projected', trig, price, when, flow, usd, meaning, effect}
        if (S && S.ok) (S.observed || []).forEach(o => rows.push({ st: 'observed', trig: o.window + ': price ' + (o.move_pct >= 0 ? '+' : '−') + Math.abs(o.move_pct).toFixed(2) + '%', price: spot ? px(spot / (1 + o.move_pct / 100)) + ' → ' + px(spot) : (o.move_pct >= 0 ? '+' : '') + o.move_pct.toFixed(2) + '%', when: o.from + ' → ' + o.to + ' ET (already happened)',
            flow: o.flow_c, usd: o.flow_notional, meaning: 'Change in the dealers’ net hedge over the window (price, time and vol effects combined). Dealers ' + bs(o.flow_c) + ' = they bought/sold the underlying.', effect: o.reading }));
        h.shock_pct.forEach((s, i) => { if (s === 0) return; const f = h.anchor_contracts[i], n = h.notional[i], same = (f > 0) === (s > 0), rel = Math.abs(f) / maxAbs, spd = rel >= 0.66 ? 'sharp' : rel >= 0.33 ? 'firm' : 'mild';
            rows.push({ st: 'projected', trig: tgt + ' moves ' + (s > 0 ? '+' : '') + s + '% from ' + px(spot), price: spot ? px(spot * (1 + s / 100)) : 'n/a', when: 'Immediately, as price trades there (re-hedged at that level)', flow: f, usd: n,
                meaning: 'Dealers would ' + bs(f) + ' ' + Math.abs(Math.round(f)).toLocaleString() + ' ' + an + '-equivalent contracts (' + usd(n) + ') to stay delta-neutral after a ' + s + '% move. + = dealers buy, − = dealers sell.',
                effect: (same ? 'AMPLIFIES: dealers ' + bs(f).toLowerCase() + ' into a ' + (s > 0 ? 'rise' : 'fall') + ', so the move tends to accelerate' : 'DAMPS: dealers ' + bs(f).toLowerCase() + ' into a ' + (s > 0 ? 'rise' : 'fall') + ', so the move tends to stall / revert') + ' (' + spd + ' vs the rest of this ladder).' }); });
        const vr = window.VT_VOL ? VT_VOL.regimeFor(an) : null, vtxt = vr && vr.available ? vr.id + ' ' + (vr.trend > 0 ? 'rising' : vr.trend < 0 ? 'falling' : 'flat') + (vr.regime !== 'unknown' ? ', ' + vr.regime : '') : (vr && vr.id ? vr.id + ' unavailable' : 'vol index unavailable');
        [['up', h.vanna_up_c, '+1'], ['dn', h.vanna_dn_c, '−1']].forEach(a => { const live = vr && vr.available && ((a[0] === 'up' && vr.trend > 0) || (a[0] === 'dn' && vr.trend < 0));
            rows.push({ st: 'projected', trig: 'Implied vol ' + a[2] + ' vol pt (' + vtxt + ')', price: 'unchanged (' + px(spot) + ')', when: 'While vol re-prices; ' + (live ? 'this is the live direction' : 'only if vol moves this way'), flow: a[1], usd: null,
                meaning: 'Vanna: dealers would ' + bs(a[1]) + ' ' + Math.abs(Math.round(a[1])).toLocaleString() + ' contracts per ' + a[2] + ' vol pt with price unchanged.', effect: (a[1] > 0 ? 'Supports price' : 'Pressures price') + ' if vol moves ' + (a[0] === 'up' ? 'up' : 'down') + '; ' + (live ? 'vol is currently moving this way.' : 'not the current vol direction.') }); });
        const ccl = S && S.ok && S.pts.length ? S.pts[S.pts.length - 1].charm_close : null, tc = S && S.ok ? S.to_close_min : -1;
        if (ccl != null) rows.push({ st: 'projected', trig: 'Time passes to the 16:00 close (0DTE decay)', price: 'unchanged', when: tc >= 0 ? 'Over the next ' + (tc >= 90 ? (tc / 60).toFixed(1) + ' h' : tc + ' min') + ', fastest late in the session' : 'n/a (market closed)', flow: ccl, usd: null, meaning: 'Charm: dealers ' + bs(ccl) + ' ' + Math.abs(Math.round(ccl)).toLocaleString() + ' contracts to re-balance from time alone.', effect: 'Drift ' + (ccl > 0 ? 'UP' : 'DOWN') + ' into the close with no price move needed; ' + (S.closing || '') });
        rows.push({ st: 'projected', trig: 'One day of time decay (all expiries)', price: 'unchanged', when: 'Over the next trading day', flow: h.charm_1d_c, usd: null, meaning: 'Charm per day: dealers ' + bs(h.charm_1d_c) + ' ' + Math.abs(Math.round(h.charm_1d_c)).toLocaleString() + ' contracts.', effect: 'Background ' + (h.charm_1d_c > 0 ? 'buying' : 'selling') + ' pressure from time alone.' });
        [['+1%', h.zero_dte_up_c], ['−1%', h.zero_dte_dn_c]].forEach(a => rows.push({ st: 'projected', trig: '0DTE only: ' + tgt + ' ' + a[0], price: spot ? px(spot * (1 + (a[0] === '+1%' ? 0.01 : -0.01))) : 'n/a', when: 'Today only; gone at expiry', flow: a[1], usd: null, meaning: 'Hedge flow from contracts expiring today: dealers ' + bs(a[1]) + ' ' + Math.abs(Math.round(a[1])).toLocaleString() + ' contracts.', effect: ((a[1] > 0) === (a[0] === '+1%') ? 'Amplifies' : 'Damps') + ' the move while 0DTE is live; the effect disappears at the close.' }));
        const up = h.anchor_contracts[h.shock_pct.indexOf(1)], dn = h.anchor_contracts[h.shock_pct.indexOf(-1)];
        let sum = '';
        if (up != null && dn != null) { const amp = up > 0 && dn < 0, damp = up < 0 && dn > 0;
            sum = (amp ? 'Dealer hedging currently AMPLIFIES moves: they buy ' + Math.abs(Math.round(up)).toLocaleString() + ' ' + an + ' contracts on a +1% move (' + px(spot * 1.01) + ') and sell ' + Math.abs(Math.round(dn)).toLocaleString() + ' on a −1% move (' + px(spot * 0.99) + '), so breaks tend to extend.' :
                damp ? 'Dealer hedging currently DAMPS moves: they sell ' + Math.abs(Math.round(up)).toLocaleString() + ' ' + an + ' contracts on a +1% move (' + px(spot * 1.01) + ') and buy ' + Math.abs(Math.round(dn)).toLocaleString() + ' on a −1% move (' + px(spot * 0.99) + '), so price tends to pin.' :
                'Dealer hedging is ASYMMETRIC: ' + bs(up).toLowerCase() + ' ' + Math.abs(Math.round(up)).toLocaleString() + ' on +1% (' + px(spot * 1.01) + ') but ' + bs(dn).toLowerCase() + ' ' + Math.abs(Math.round(dn)).toLocaleString() + ' on −1% (' + px(spot * 0.99) + ').');
            if (ccl != null) sum += ' Time alone pushes ' + (ccl > 0 ? 'buying' : 'selling') + ' of ' + Math.abs(Math.round(ccl)).toLocaleString() + ' contracts into the close' + (tc >= 0 ? ' (' + tc + ' min left)' : '') + '.';
            const o0 = S && S.ok && S.observed && S.observed[0]; if (o0) sum += ' So far: ' + o0.reading; else sum += ' Observed flow unavailable: ' + (S ? S.reason || 'no history yet' : 'no session history recorded') + '.'; }
        const key = '<div class="vt-hedge-key"><span class="pos">+ dealers BUY (green)</span> <span class="neg">− dealers SELL (red)</span> &middot; <b>Observed</b> = already happened, <b>Projected</b> = expected if the trigger occurs &middot; flow in ' + an + '-equivalent contracts (full Black-Scholes revaluation) &middot; dealers assumed long calls / short puts (not observed)</div>';
        const tr = rows.map(r => '<tr class="' + r.st + '"><td><span class="st ' + r.st + '">' + (r.st === 'observed' ? 'Observed' : 'Projected') + '</span></td><td>' + esc(r.trig) + '</td><td>' + esc(r.price) + '</td><td>' + esc(r.when) + '</td><td class="' + c0(r.flow) + '">' + bs(r.flow) + ' ' + sg(Math.abs(r.flow)).replace('+', '') + (r.usd != null && isFinite(r.usd) ? '<br><small>' + usd(r.usd) + '</small>' : '') + '</td><td>' + esc(r.meaning) + '</td><td>' + esc(r.effect) + '</td></tr>').join('');
        const pb = h.per_book.map(b => '<span class="vt-lens-ex" title="share of |dollar gamma| / open interest expiring today">' + disp(b.id) + ' 0DTE ' + Math.round(b.zero_dte_gamma_share * 100) + '% &gamma; / ' + Math.round(b.zero_dte_oi_share * 100) + '% OI</span>').join('');
        return '<div class="vt-lens-sec"><h4>Hedge Flow <small>' + h.books + ' book' + (h.books > 1 ? 's' : '') + (S && S.ok ? ' &middot; as of ' + esc(S.as_of) : '') + '</small></h4><div class="vt-hedge-sum">' + esc(sum) + '</div>' + volLine() + key +
            '<div class="vt-hedge-w"><table class="vt-hedge vt-hedge-t"><thead><tr><th title="Observed = already happened; Projected = expected if the trigger occurs">Status</th><th title="What causes the flow">Trigger</th><th title="Price at which the flow takes effect">Price</th><th title="When it takes effect">When</th><th title="Dealer buy/sell size in anchor-equivalent contracts and dollars">Dealer flow</th><th title="What the number measures, its unit, and the sign convention">What it measures</th><th title="Expected effect on the ticker and its price fluctuation">Effect on price</th></tr></thead><tbody>' + tr + '</tbody></table></div><div class="vt-lens-chips">' + pb + '</div></div>';
    }
    // C-3: the shared VIX/VXN reading (same object the sidebar and Pricing use) as it bears on dealer hedging
    function volLine() { const v = L.mb && L.mb.volctx; if (!v) return '';
        if (!v.ok) return '<div class="vt-hedge-vol na"><b>Vol regime:</b> unavailable &mdash; ' + esc(v.reason) + '.</div>';
        return '<div class="vt-hedge-vol"><b>' + esc(v.headline) + '.</b> ' + esc(v.hedge) + (v.divergence ? ' <i>' + esc(v.divergence) + '</i>' : '') + '</div>'; }
    function volBlock() {
        const rows = [...L.picked].map(id => book(id)).filter(b => b && b.vol).map(b => { const v = b.vol, pct = x => (x * 100).toFixed(1);
            const spark = v.term.map(t => t.dte + 'd ' + pct(t.iv)).join(' &rsaquo; ');
            const big = v.scen.filter(x => x[1] === 0).map(x => '<td class="' + cls(x[2]) + '">' + sgn(x[2] / 1e6) + '</td>').join('');
            return '<div class="vt-lens-vr"><b style="color:' + color(b.id) + '">' + disp(b.id) + '</b> <span class="' + (v.backwardated ? 'neg' : '') + '">' + (v.backwardated ? 'BACKWARDATED' : 'contango') + ' ' + (v.slope * 100).toFixed(1) + ' vol/&radic;yr</span> &middot; term ' + spark +
                ' &middot; 25&Delta; RR ' + pct(v.rr25) + ' &middot; put wing +' + pct(v.put_wing) + ' &middot; 1d move &plusmn;' + (v.em_1d * 100).toFixed(2) + '%' + (v.vrp != null ? ' &middot; VRP ' + pct(v.vrp) + ' (RV ' + pct(v.rv) + ')' : ' &middot; VRP n/a (no realized vol)') + (v.em_ratio != null ? ' &middot; realized/priced ' + v.em_ratio.toFixed(2) + 'x' : '') +
                ' &middot; |&gamma;| top-3 strikes ' + Math.round(v.top3 * 100) + '% (HHI ' + v.hhi.toFixed(2) + ')<table class="vt-hedge"><thead><tr><th>dealer MTM $M @ spot</th><th>&minus;2%</th><th>&minus;1%</th><th>0</th><th>+1%</th><th>+2%</th></tr></thead><tbody><tr><td>IV flat</td>' + big + '</tr></tbody></table></div>'; }).join('');
        return '<div class="vt-lens-sec"><h4>Vol &amp; risk <small>measurements, not forecasts</small></h4>' + (rows || '<div class="vt-lens-empty">pick a book</div>') + '</div>';
    }
    // ---- B1-6/B1-7: Intraday / Swinging tabs + strike insight cards (all numbers and sentences come from the C++ strike_insight core)
    const esc = t => String(t == null ? '' : t).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const ex = v => v == null ? 'n/a' : (v < 0 ? '−' : '+') + (Math.abs(v) >= 1e9 ? (Math.abs(v) / 1e9).toFixed(2) + 'B' : Math.abs(v) >= 1e6 ? (Math.abs(v) / 1e6).toFixed(1) + 'M' : Math.abs(v) >= 1e3 ? (Math.abs(v) / 1e3).toFixed(0) + 'K' : Math.abs(v).toFixed(0));
    const shs = v => v == null ? 'n/a' : (v < 0 ? '−' : '+') + (Math.abs(v) >= 1e6 ? (Math.abs(v) / 1e6).toFixed(2) + 'M' : Math.abs(v) >= 1e3 ? (Math.abs(v) / 1e3).toFixed(1) + 'K' : Math.abs(v).toFixed(0));
    const VCLS = { Magnet: 'mag', Support: 'sup', Resistance: 'res', Acceleration: 'acc', Pivot: 'piv', Weak: 'weak' };
    // strike label: own space first; futures equivalent where the primary space is futures (or the toggle is on); other-book strikes shown in the focus ticker's space. Same conversion maths as the table / converter.
    function strikeLabel(id, k) {
        const P = scopeNow().ticker, b = book(id), an = L.lens.anchor, own = disp(id) + ' $' + fmt(k, b && b.tick_hint || 0.01);
        if (!b || !b.convertible) return own; const F = L.toFut(id, k); if (F == null) return own;
        if (famMode() || isFutTicker(P)) return own + ' / ' + an + ' $' + fmt(F, L.lens.tick);
        const extra = id !== P ? ' = ' + disp(P) + ' $' + fmt(P === an ? F : L.fromFut(P, F), 0.01) : '';
        return own + extra + (L.showFut ? ' / ' + an + ' $' + fmt(F, L.lens.tick) : '');
    }
    function axisPx(id, k) { const b = book(id); if (!b) return null; if (famMode()) return L.toFut(id, k); const P = scopeNow().ticker; if (id === P) return k; const F = L.toFut(id, k); return F == null ? null : L.fromFut(P, F); }
    L.axisPx = axisPx;
    function cardHtml(id, c, i) {
        const vcls = VCLS[c.verdict] || '', votes = c.votes.map(v => '<tr><td>' + esc(v.greek) + '</td><td class="v' + (v.v > 0 ? ' pos' : v.v < 0 ? ' neg' : '') + '">' + (v.v > 0 ? 'agrees' : v.v < 0 ? 'conflicts' : 'no vote') + '</td><td>' + esc(v.text) + '</td></tr>').join('');
        const greeks = '<table class="vt-ic-g"><thead><tr><th>Gamma $/1%</th><th>Vanna sh/+1 vol pt</th><th>Charm sh/day</th><th>Charm to close</th><th>Vomma $/+1 vol pt</th><th>Color $/1% per ' + (L.hz === 'intraday' ? 'hour' : 'day') + '</th></tr></thead><tbody><tr><td class="' + (c.gamma > 0 ? 'pos' : 'neg') + '">' + ex(c.gamma) + '</td><td>' + shs(c.vanna_up) + ' up / ' + shs(c.vanna_dn) + ' down</td><td class="' + (c.charm_day > 0 ? 'pos' : 'neg') + '">' + shs(c.charm_day) + '</td><td>' + (c.charm_close == null ? 'n/a (market closed)' : shs(c.charm_close)) + '</td><td>' + ex(c.vomma) + '</td><td class="' + (c.color > 0 ? 'pos' : 'neg') + '">' + ex(c.color) + '</td></tr></tbody></table>';
        const pct = v => Math.round(v * 100) + '%', pb = c.pb;
        const g2 = '<table class="vt-ic-g"><thead><tr><th>Delta hedge</th><th>Vega $/vol pt</th><th>Theta $/day</th><th>Speed $/1% per +1% move</th><th>IV at strike</th><th>Distance</th><th>P(finish beyond)</th><th>P(touch)</th><th>Gamma share</th></tr></thead><tbody><tr><td>' + shs(c.delta_sh) + ' sh</td><td>' + ex(c.vega) + '</td><td class="' + (c.theta > 0 ? 'pos' : 'neg') + '">' + ex(c.theta) + '</td><td class="' + (c.speed > 0 ? 'pos' : 'neg') + '">' + ex(c.speed) + '</td><td>' + (c.iv_k * 100).toFixed(1) + '% (' + (c.skew >= 0 ? '+' : '\u2212') + (Math.abs(c.skew) * 100).toFixed(1) + ' vs ATM)</td><td>' + c.sigma_dist.toFixed(2) + ' &sigma; (' + (c.dist_pct >= 0 ? '+' : '') + c.dist_pct.toFixed(2) + '%)</td><td>' + pct(c.p_beyond) + '</td><td>~' + pct(c.p_touch) + '</td><td>' + pct(c.gamma_share) + '</td></tr></tbody></table>';
        const play = '<div class="vt-ic-pb"><div class="ph"><b>Trade playbook</b> <span class="vd2">' + esc(pb.style) + '</span> <span class="hp ' + (pb.potential === 'high' ? 'pos' : pb.potential.indexOf('low') === 0 ? 'neg' : '') + '">hold potential: ' + esc(pb.potential) + '</span></div>' +
            '<table class="vt-ic-v"><tbody><tr><td>Entry</td><td>' + esc(pb.entry) + '</td></tr><tr><td>Stop</td><td>' + esc(pb.stop) + '</td></tr><tr><td>Target</td><td>' + esc(pb.target) + '</td></tr><tr><td>Holding</td><td>' + esc(pb.hold) + '</td></tr><tr><td>Reaction speed</td><td>' + esc(pb.speed) + '</td></tr></tbody></table>' +
            '<ul class="edge">' + pb.edge.map(t => '<li>' + esc(t) + '</li>').join('') + '</ul><small>Derived from the Greek profile and the risk-neutral distribution; a framing of how the level tends to trade, not a trade signal.</small></div>';
        return '<details data-sk="' + (axisPx(id, c.strike) ?? '') + '" class="vt-ic ' + vcls + (c.contested ? ' contested' : '') + '"' + (i < 2 ? ' open' : '') + '><summary><b class="k" style="border-left:3px solid ' + color(id) + '">' + esc(strikeLabel(id, c.strike)) + '</b><span class="x ' + (c.gamma > 0 ? 'pos' : 'neg') + '">' + ex(c.gamma) + '/1%</span><span class="vd">' + esc(c.verdict) + (c.contested ? ' (contested)' : '') + '</span><span class="pbs">' + esc(c.pb.style.split(':')[0]) + ' &middot; hold ' + esc(c.pb.potential.split(' ')[0]) + '</span><span class="cv">conviction ' + esc(c.conviction.split(':')[0]) + '</span><span class="dt">valid until ' + esc(c.valid_until) + '</span></summary>' +
            '<div class="vt-ic-b"><p class="m">' + esc(c.mechanism) + '</p><p><b>On approach:</b> ' + esc(c.approach) + ' <b>On breach:</b> ' + esc(c.breach) + '</p>' + greeks + g2 + play +
            '<table class="vt-ic-v"><thead><tr><th>Greek</th><th>vs verdict</th><th>Reading</th></tr></thead><tbody>' + votes + '</tbody></table>' +
            '<p><b>Valid:</b> from ' + esc(c.as_of) + ' until ' + esc(c.valid_until) + ' <small>(limited by ' + esc(c.valid_limit) + ')</small></p>' +
            '<div class="vt-ic-i"><b>Invalidated by:</b><ul>' + c.invalidation.map(t => '<li>' + esc(t) + '</li>').join('') + '</ul></div>' +
            '<p class="rg"><b>Regime:</b> ' + esc(c.regime) + '</p><p class="cf"><b>Conviction:</b> ' + esc(c.conviction) + ' &middot; <b>data confidence:</b> ' + esc(c.confidence) + ' &middot; as of ' + esc(c.as_of) + '</p></div></details>';
    }
    function insightBlock() {
        const ids = [...L.picked].filter(id => { const b = book(id); return b && b.insight; });
        const tabs = '<div class="vt-lens-hz" role="tablist">' + [['intraday', 'Intraday'], ['swinging', 'Swinging']].map(t => '<button role="tab" data-hz="' + t[0] + '" class="' + (L.hz === t[0] ? 'on' : '') + '">' + t[1] + '</button>').join('') + '</div>';
        if (!ids.length) return '<div class="vt-lens-sec"><h4>Strike insight</h4>' + tabs + '<div class="vt-lens-empty">Pick a book above to see its strike analysis.</div></div>';
        const parts = ids.map(id => { const I = book(id).insight, H = I[L.hz];
            const head = '<div class="vt-ic-h"><b style="color:' + color(id) + '">' + disp(id) + '</b> ' + (H.ok ? esc(H.headline) : '') + '</div>';
            if (!H.ok) return head + '<div class="vt-lens-warn">' + L.hzName() + ' unavailable &mdash; ' + esc(H.reason) + '.</div>';
            const src = '<div class="vt-ic-src">Fed by: ' + esc(H.expiries_text) + ' &middot; as of ' + esc(H.as_of) + ' &middot; ' + esc(H.assumption) + '</div>';
            const rec = I.reconcile.length ? '<div class="vt-ic-rec"><b>Why the tabs differ:</b> ' + I.reconcile.map(esc).join(' ') + '</div>' : '';
            return head + src + (H.cards.length ? H.cards.map((c, i) => cardHtml(id, c, i)).join('') : '<div class="vt-lens-empty">No strike carries enough gamma to be structural in this horizon.</div>') + rec; }).join('');
        return '<div class="vt-lens-sec"><h4>Strike insight <small>' + (L.hz === 'intraday' ? '0-1 DTE: the current session' : 'further-dated: the coming days') + '</small></h4>' + tabs + parts + '<div class="vt-lens-note">Key: <span class="pos">+ stabilising / dealers buy</span> &middot; <span class="neg">&minus; amplifying / dealers sell</span>. Flows are revalued with full Black-Scholes; dealers assumed long calls / short puts.</div></div>';
    }
    L.strikeLabel = strikeLabel;
    L.hzName = () => L.hz === 'intraday' ? 'Intraday' : 'Swinging';
    L.setHorizon = h => { if (h === 'intraday' || h === 'swinging') { L.hz = h; try { sessionStorage.setItem('vt_lens_hz', h); } catch (e) {} render(); } };
    function render() {
        const el = document.getElementById('vt-lens'); if (!el) return;
        const on = active(); el.hidden = !on; if (!on) { repaintPanels(); fire(); return; }
        const lens = L.lens, sc = scopeNow(), mb = L.mb, view = L.view, fam = famMode(), P = sc.ticker;
        if (!L.picked) { const own = book(P); const first = own && own.convertible ? own : lens.books.find(b => b.convertible); L.picked = new Set(first ? [first.id] : []); }
        const wmap = {}; (mb && mb.books || []).forEach(b => wmap[b.id] = b);
        const tgtName = fam ? lens.anchor : disp(P), tgtTick = fam ? lens.tick : null;
        const chips = lens.books.map(b => '<button class="vt-lens-chip' + (L.picked.has(b.id) ? ' on' : '') + (b.convertible ? '' : ' na') + (b.id === P ? ' own' : '') + '" data-id="' + b.id + '" title="' + (b.note || '').replace(/"/g, '&quot;') + '"' + (b.convertible ? '' : ' disabled') +
            ' style="--c:' + color(b.id) + '">' + disp(b.id) + (wmap[b.id] ? '<small>' + Math.round(wmap[b.id].weight * 100) + '%</small>' : '') + '</button>').join('');
        const showFutCol = !fam && L.showFut;
        const rows = []; for (const id of L.picked) { const b = book(id); if (b) levelsOf(b, view).forEach(lv => { const pv = L.primary(id, lv); if (pv != null) rows.push({ id, lv, pv, b }); }); }
        rows.sort((a, c) => c.pv - a.pv);
        const valCell = r => { const u = r.lv.value; if (view !== 'pricing') return '';
            return r.lv.label.indexOf('RND') === 0 && r.lv.label !== 'RND Mode' ? Math.round(u * 100) + '%' : (r.lv.label.indexOf('25D') === 0 || r.lv.label === 'ATM') ? (u * 100).toFixed(1) + '% IV' : ''; };
        const gv = view === 'greeks', eq = showEquiv() || [...L.picked].some(id => id !== P); // futures: labelled "equiv"; non-futures: only a plain "In <TICKER>" column when another book is picked (own-book rows would just repeat the strike)
        const head = '<th>Level</th><th>Own strike</th>' + (eq ? '<th>' + (showEquiv() ? (fam ? lens.anchor : tgtName) + ' equiv' : 'In ' + tgtName) + '</th>' : '') + (gv ? '<th>Exposure</th>' : '') + (view === 'pricing' ? '<th>Read</th>' : '') + (showFutCol ? '<th>' + lens.anchor + ' equiv</th>' : '') + '<th>vs spot</th>';
        const spotP = fam ? lens.anchor_px : (P === lens.anchor ? lens.anchor_px : (book(P) || {}).spot);
        const tbl = rows.length ? '<table class="vt-lens-t"><thead><tr>' + head + '</tr></thead><tbody>' + rows.map(r => '<tr data-v="' + r.pv + '"><td><i style="background:' + color(r.id) + '" title="' + disp(r.id) + '"></i>' + r.lv.label + '</td><td>' + fmt(r.lv.strike) + '</td>' + (eq ? '<td class="f">' + fmt(r.pv, fam ? lens.tick : 0.01) + '</td>' : '') +
            (gv ? '<td class="ex ' + (r.lv.value > 0 ? 'pos' : r.lv.value < 0 ? 'neg' : '') + '" style="border-left:2px solid ' + color(r.id) + '">' + (r.lv.value ? fmtX(r.lv.value) : '&ndash;') + '</td>' : '') +
            (view === 'pricing' ? '<td>' + valCell(r) + '</td>' : '') + (showFutCol ? '<td>' + fmt(r.lv.fut, lens.tick) + '</td>' : '') +
            '<td>' + (spotP ? ((r.pv - spotP) >= 0 ? '+' : '') + fmt(r.pv - spotP, fam ? lens.tick : 0.01) : '') + '</td></tr>').join('') + '</tbody></table>' : '<div class="vt-lens-empty">Pick a book above to highlight its levels.</div>';
        const meta = [...L.picked].map(id => book(id)).filter(Boolean).map(b => '<div class="vt-lens-note"><b>' + disp(b.id) + '</b>' + (view === 'pricing' && b.atm_iv ? ' ATM IV ' + (b.atm_iv * 100).toFixed(1) + '% &middot; 1&sigma; move ' + (b.em_pct * 100).toFixed(2) + '% (' + b.ref_dte + 'd) &middot; 25&Delta; RR ' + (b.rr25 * 100).toFixed(1) + ' vol pts' : '') +
            (b.id !== P || fam ? ' &middot; ratio ' + b.ratio.toFixed(4) + ', basis ' + b.basis.toFixed(2) + ' &mdash; ' + b.note : '') + '</div>').join('');
        const futToggle = !fam ? '<label class="vt-lens-tg"><input type="checkbox" id="vt-lens-fut"' + (L.showFut ? ' checked' : '') + '> also show ' + lens.anchor + ' futures equivalent</label>' : '';
        const cb = L.customBook && L.picked.has(L.customBook) ? L.customBook : [...L.picked][0];
        const conv = '<div class="vt-lens-conv"><select id="vt-lens-cb">' + lens.books.filter(b => b.convertible).map(b => '<option value="' + b.id + '"' + (b.id === cb ? ' selected' : '') + '>' + disp(b.id) + '</option>').join('') + '</select>' +
            '<input id="vt-lens-ck" type="number" step="any" placeholder="strike" inputmode="decimal"><span>&rarr; ' + tgtName + '</span><b id="vt-lens-cv">&ndash;</b></div>';
        const conf = mb ? '<div class="vt-lens-conf">confidence <b>' + Math.round(mb.confidence * 100) + '%</b>' + (view === 'greeks' ? ' &middot; flip confluence <b>' + Math.round(mb.flip_confluence * 100) + '%</b> &middot; sign agreement <b>' + mb.sign_agreement.toFixed(2) + '</b>' : '') + ' &middot; weights: ' + (mb.scope.weighting === 'priority_only' ? 'priority only' : 'priority &times; liquidity') +
            (mb.notes && mb.notes.length ? '<div class="vt-lens-warn">' + mb.notes.map(n => n.replace(/</g, '&lt;')).join(' | ') + '</div>' : '') + '</div>' : '';
        const excl = mb ? mb.books.filter(b => !b.included).map(b => '<span class="vt-lens-ex" title="' + b.reason.replace(/"/g, '&quot;') + '">' + disp(b.id) + ' excluded</span>').join('') : '';
        const syms = [...L.picked].length ? [...L.picked] : [P];
        const title = 'Strike Lens &middot; ' + syms.map(id => '<em style="--c:' + color(id) + '">' + disp(id) + '</em>').join(' ');
        const lensHtml = '<div class="vt-lens-h"><b>' + title + '</b></div><div class="vt-lens-chips" role="group" aria-label="Books to highlight">' + chips + excl + '</div>' + tbl + (gv && rows.length ? '<div class="vt-lens-note">Exposure = dealer GEX at the strike, $ per 1% move (+ stabilising / &minus; amplifying); dealers assumed long calls / short puts.</div>' : '') + meta + futToggle + conv + (view === 'greeks' ? '' : volBlock()) + conf;
        // Chart Factors page: Strike Insights, Strike Lens, Hedge Flow as three bordered full-width sections (workfile CF §8.1); Pricing page keeps the single lens panel
        el.innerHTML = view === 'greeks' ? '<section class="vt-cf-sec" data-sec="insights">' + insightBlock() + '</section><section class="vt-cf-sec" data-sec="lens">' + lensHtml + '</section><section class="vt-cf-sec" data-sec="hedge">' + hedgeBlock() + '</section>' : lensHtml;
        el.dataset.epoch = L.lens.epoch || '';
        el.querySelectorAll('.vt-lens-chip:not(.na)').forEach(c => c.addEventListener('click', () => { const id = c.dataset.id; L.picked.has(id) ? L.picked.delete(id) : L.picked.add(id); render(); }));
        el.querySelectorAll('.vt-lens-t tr[data-v]').forEach(tr => tr.addEventListener('click', () => { const v = tr.dataset.v; if (v && navigator.clipboard) try { navigator.clipboard.writeText(v); } catch (e) {} tr.classList.add('copied'); }));
        const tg = el.querySelector('#vt-lens-fut'); if (tg) tg.addEventListener('change', () => { L.showFut = tg.checked; render(); });
        const upd = () => { const id = el.querySelector('#vt-lens-cb').value, k = parseFloat(el.querySelector('#vt-lens-ck').value); L.customBook = id; const out = el.querySelector('#vt-lens-cv');
            const F = isNaN(k) ? null : L.toFut(id, k); const v = F == null ? null : (fam || P === lens.anchor ? F : L.fromFut(P, F)); out.textContent = v == null ? '–' : fmt(v, fam ? lens.tick : 0.01); };
        el.querySelector('#vt-lens-ck').addEventListener('input', upd); el.querySelector('#vt-lens-cb').addEventListener('change', upd);
        el.querySelectorAll('.vt-lens-hz button').forEach(b => b.addEventListener('click', () => L.setHorizon(b.dataset.hz)));
        repaintPanels(); fire();
    }
    L.render = render;
    L.setView = v => { if (v === 'pricing' || v === 'greeks') { L.view = v; render(); } };
    window.onStrikeLens = p => { L.lens = parse(p); if (L.picked) { for (const id of [...L.picked]) if (!book(id) || !book(id).convertible) L.picked.delete(id); } render(); };
    window.onMultibook = p => { L.mb = parse(p); render(); };
    L.reset = () => { L.lens = null; L.mb = null; L.picked = null; L.showFut = false; render(); };
    window.VT_LENS = L;
    window.addEventListener('DOMContentLoaded', () => { if (window.VT_SCOPE) { let last = ''; VT_SCOPE.onChange(sc => { const k = sc.mode + '|' + sc.family + '|' + sc.ticker; if (k !== last) { last = k; if (sc.mode === 'single') render(); else L.reset(); } else render(); }); } });
})();
