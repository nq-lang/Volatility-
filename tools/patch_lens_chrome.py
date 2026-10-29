import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
p = (VT_ROOT + '/web/js/strike_lens.js'); s = open(p).read()
def rep(a, b):
    global s
    assert a in s, a[:60]
    s = s.replace(a, b, 1)

# futures-ticker test: Equiv only where the primary space is a futures contract
rep("const famMode = () => scopeNow().mode === 'family';",
    "const famMode = () => scopeNow().mode === 'family';\n    const isFutTicker = id => !!(window.VT_REGISTRY && (VT_REGISTRY.futures || []).includes(id));\n    // B1-1: the visible \"Equiv\" column exists only when the primary price space is a futures contract (Family, or a futures ticker in focus). Conversion logic is untouched.\n    const showEquiv = () => famMode() || isFutTicker(scopeNow().ticker);\n    const fmtX = v => { const a = Math.abs(v), sg = v < 0 ? '\\u2212' : '+'; return sg + (a >= 1e9 ? (a / 1e9).toFixed(2) + 'B' : a >= 1e6 ? (a / 1e6).toFixed(2) + 'M' : a >= 1e3 ? (a / 1e3).toFixed(1) + 'K' : a.toFixed(0)); };")

# header: exposure column instead of Book, Equiv conditional
rep("const head = '<th>Book</th><th>Level</th><th>Own strike</th><th>' + (fam ? lens.anchor + ' equiv' : tgtName + ' equiv') + '</th>' +",
    "const eq = showEquiv(), gv = view === 'greeks';\n        const head = '<th>Level</th><th>Own strike</th>' + (eq ? '<th>' + (fam ? lens.anchor : tgtName) + ' equiv</th>' : '') + (gv ? '<th>Exposure</th>' : '') +")
rep("rows.map(r => '<tr data-v=\"' + r.pv + '\"><td><i style=\"background:' + color(r.id) + '\"></i>' + disp(r.id) + '</td><td>' + r.lv.label + '</td><td>' + fmt(r.lv.strike) + '</td><td class=\"f\">' + fmt(r.pv, fam ? lens.tick : 0.01) + '</td>' +",
    "rows.map(r => '<tr data-v=\"' + r.pv + '\"><td><i style=\"background:' + color(r.id) + '\" title=\"' + disp(r.id) + '\"></i>' + r.lv.label + '</td><td>' + fmt(r.lv.strike) + '</td>' + (eq ? '<td class=\"f\">' + fmt(r.pv, fam ? lens.tick : 0.01) + '</td>' : '') +\n            (gv ? '<td class=\"ex ' + (r.lv.value > 0 ? 'pos' : r.lv.value < 0 ? 'neg' : '') + '\" style=\"border-left:2px solid ' + color(r.id) + '\">' + (r.lv.value ? fmtX(r.lv.value) : '&ndash;') + '</td>' : '') +")

# title + no subtitle
rep("const title = view === 'pricing' ? 'STRIKE LENS &middot; PRICING' : 'STRIKE LENS &middot; GREEKS';",
    "const syms = [...L.picked].length ? [...L.picked] : [P];\n        const title = 'Strike Lens &middot; ' + syms.map(id => '<em style=\"--c:' + color(id) + '\">' + disp(id) + '</em>').join(' ');")
i = s.index("        const sub = (view === 'pricing'"); j = s.index("\n", i); s = s[:i] + s[j + 1:]
rep("'<div class=\"vt-lens-h\"><b>' + title + '</b><span>' + sub + '</span></div>", "'<div class=\"vt-lens-h\"><b>' + title + '</b></div>")
# unit stated once
rep("tbl + meta + futToggle", "tbl + (gv && rows.length ? '<div class=\"vt-lens-note\">Exposure = dealer GEX at the strike, $ per 1% move (+ stabilising / &minus; amplifying); dealers assumed long calls / short puts.</div>' : '') + meta + futToggle")
open(p, 'w').write(s)

c = (VT_ROOT + '/web/css/style.css'); t = open(c).read()
t += """
.vt-lens-h b { text-transform: none; letter-spacing: 1px; } .vt-lens-h b em { font-style: normal; color: var(--c, #ffa838); border-bottom: 2px solid var(--c, #ffa838); padding: 0 3px; margin-left: 4px; letter-spacing: 1.5px; }
.vt-lens-t td.ex.pos { color: #6fd6a0; } .vt-lens-t td.ex.neg { color: #ff8f8f; } .vt-lens-t td.ex { text-align: right; font-weight: 700; }
"""
open(c, 'w').write(t); print('ok')
