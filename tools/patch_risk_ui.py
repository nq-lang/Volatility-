import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
p = (VT_ROOT + '/web/js/strike_lens.js'); s = open(p).read()
block = r"""    // ---- T2-5 / T2-6 blocks. Hedge Flows (Greeks page): aggregate dealer re-hedge flow in anchor-equivalent contracts. Vol & Risk (Pricing page): per picked book.
    const sgn = v => (v > 0 ? '+' : '') + v.toLocaleString('en-US', { maximumFractionDigits: 0 });
    const cls = v => v > 0 ? 'pos' : v < 0 ? 'neg' : '';
    function hedgeBlock() {
        const h = L.mb && L.mb.hedge; if (!h) return '<div class="vt-lens-sec"><h4>Hedge flows</h4><div class="vt-lens-empty">no index-convertible books in scope</div></div>';
        const an = L.lens.anchor, hd = h.shock_pct.map(x => '<th>' + (x > 0 ? '+' : '') + x + '%</th>').join('');
        const row = '<tr><td>' + an + ' contracts</td>' + h.anchor_contracts.map(v => '<td class="' + cls(v) + '">' + sgn(v) + '</td>').join('') + '</tr><tr><td>$ notional (M)</td>' + h.notional.map(v => '<td class="' + cls(v) + '">' + sgn(v / 1e6) + '</td>').join('') + '</tr>';
        const pb = h.per_book.map(b => '<span class="vt-lens-ex" title="share of |dollar gamma| / open interest expiring today">' + disp(b.id) + ' 0DTE ' + Math.round(b.zero_dte_gamma_share * 100) + '% &gamma; / ' + Math.round(b.zero_dte_oi_share * 100) + '% OI</span>').join('');
        return '<div class="vt-lens-sec"><h4>Hedge flows <small>dealer re-hedge, ' + h.books + ' book' + (h.books > 1 ? 's' : '') + ', + = dealers buy</small></h4><table class="vt-lens-t vt-hedge"><thead><tr><th>spot shock</th>' + hd + '</tr></thead><tbody>' + row + '</tbody></table>' +
            '<div class="vt-lens-kv"><span>Vanna IV +1pt <b class="' + cls(h.vanna_up_c) + '">' + sgn(h.vanna_up_c) + '</b></span><span>IV &minus;1pt <b class="' + cls(h.vanna_dn_c) + '">' + sgn(h.vanna_dn_c) + '</b></span><span>Charm 1d <b class="' + cls(h.charm_1d_c) + '">' + sgn(h.charm_1d_c) + '</b></span>' +
            '<span>0DTE +1% <b class="' + cls(h.zero_dte_up_c) + '">' + sgn(h.zero_dte_up_c) + '</b></span><span>0DTE &minus;1% <b class="' + cls(h.zero_dte_dn_c) + '">' + sgn(h.zero_dte_dn_c) + '</b></span></div><div class="vt-lens-chips">' + pb + '</div>' +
            '<div class="vt-lens-note">' + an + '-equivalent contracts, full revaluation, dealers long calls / short puts (assumed, not observed). Single names, leveraged ETFs and vol products are excluded (not 1:1 with the index).</div></div>';
    }
    function volBlock() {
        const rows = [...L.picked].map(id => book(id)).filter(b => b && b.vol).map(b => { const v = b.vol, pct = x => (x * 100).toFixed(1);
            const spark = v.term.map(t => t.dte + 'd ' + pct(t.iv)).join(' &rsaquo; ');
            const big = v.scen.filter(x => x[1] === 0).map(x => '<td class="' + cls(x[2]) + '">' + sgn(x[2] / 1e6) + '</td>').join('');
            return '<div class="vt-lens-vr"><b style="color:' + color(b.id) + '">' + disp(b.id) + '</b> <span class="' + (v.backwardated ? 'neg' : '') + '">' + (v.backwardated ? 'BACKWARDATED' : 'contango') + ' ' + (v.slope * 100).toFixed(1) + ' vol/&radic;yr</span> &middot; term ' + spark +
                ' &middot; 25&Delta; RR ' + pct(v.rr25) + ' &middot; put wing +' + pct(v.put_wing) + ' &middot; 1d move &plusmn;' + (v.em_1d * 100).toFixed(2) + '%' + (v.vrp != null ? ' &middot; VRP ' + pct(v.vrp) + ' (RV ' + pct(v.rv) + ')' : ' &middot; VRP n/a (no realized vol)') + (v.em_ratio != null ? ' &middot; realized/priced ' + v.em_ratio.toFixed(2) + 'x' : '') +
                ' &middot; |&gamma;| top-3 strikes ' + Math.round(v.top3 * 100) + '% (HHI ' + v.hhi.toFixed(2) + ')<table class="vt-lens-t vt-hedge"><thead><tr><th>dealer MTM $M @ spot</th><th>&minus;2%</th><th>&minus;1%</th><th>0</th><th>+1%</th><th>+2%</th></tr></thead><tbody><tr><td>IV flat</td>' + big + '</tr></tbody></table></div>'; }).join('');
        return '<div class="vt-lens-sec"><h4>Vol &amp; risk <small>measurements, not forecasts</small></h4>' + (rows || '<div class="vt-lens-empty">pick a book</div>') + '</div>';
    }
    function render() {"""
assert s.count("    function render() {") == 1
s = s.replace("    function render() {", block, 1)
a = "el.innerHTML = '<div class=\"vt-lens-h\">"
assert a in s
s = s.replace(a, "const extra = view === 'greeks' ? hedgeBlock() : volBlock();\n        " + a, 1)
assert "+ futToggle + conv + conf;" in s
s = s.replace("+ futToggle + conv + conf;", "+ futToggle + conv + extra + conf;", 1)
open(p, 'w').write(s)
c = (VT_ROOT + '/web/css/style.css'); t = open(c).read()
t += """
.vt-lens-sec { margin-top: 10px; border-top: 1px solid var(--border); padding-top: 6px; }
.vt-lens-sec h4 { margin: 0 0 4px; font-size: 10px; letter-spacing: 1.2px; text-transform: uppercase; color: #9aa4b2; }
.vt-lens-sec h4 small { text-transform: none; letter-spacing: 0; color: #5c6570; margin-left: 6px; }
.vt-hedge td, .vt-hedge th { text-align: right; font-variant-numeric: tabular-nums; } .vt-hedge td:first-child, .vt-hedge th:first-child { text-align: left; }
.vt-lens-kv { display: flex; flex-wrap: wrap; gap: 4px 12px; font-size: 10px; color: #9aa4b2; margin: 6px 0; } .vt-lens-kv b { color: #cfd6df; }
.vt-lens-vr { font-size: 10.5px; line-height: 1.55; margin-bottom: 8px; color: #b8c0cc; }
.pos { color: #6fd6a0 !important; } .neg { color: #ff8f8f !important; }
"""
open(c, 'w').write(t); print("patched")
