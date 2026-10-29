import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
import re
W = (VT_ROOT + '/web/')
# ---- pca_views.js: arrows + variable rows grouped by source (anchor vs cross-book)
p = W + 'js/pca_views.js'; s = open(p).read()
old = "            ctx.strokeStyle = spotted ? `rgba(255,196,92,${alpha + 0.3})` : `rgba(230,235,245,${alpha})`; ctx.lineWidth = spotted ? 2.2 : 1.2;"
new = "            const xb = this.viz.feature_group && this.viz.feature_group[a.f] === 'cross_book', rgb = xb ? '110,226,196' : '230,235,245';   // cross-book block arrows are teal, anchor arrows white\n            ctx.strokeStyle = spotted ? `rgba(255,196,92,${alpha + 0.3})` : `rgba(${rgb},${alpha})`; ctx.lineWidth = spotted ? 2.2 : 1.2;"
assert old in s; s = s.replace(old, new, 1)
old = "this.panel._arrowHead(ctx, o, t, spotted ? 'rgba(255,196,92,0.95)' : `rgba(230,235,245,${alpha + 0.05})`);"
assert old in s; s = s.replace(old, "this.panel._arrowHead(ctx, o, t, spotted ? 'rgba(255,196,92,0.95)' : `rgba(${rgb},${alpha + 0.05})`);", 1)
old = "color: spotted ? '#ffc45c' : '#eef2f7', pri: spotted ? 4 : 1, anchorX"
assert old in s; s = s.replace(old, "color: spotted ? '#ffc45c' : (xb ? '#8ff0d2' : '#eef2f7'), pri: spotted ? 4 : 1, anchorX", 1)
old = "return `<div class=\"vr${hid ? ' off' : ''}${this.spot === i ? ' sp' : ''}\" data-f=\"${i}\""
assert old in s; s = s.replace(old, "return `<div class=\"vr${hid ? ' off' : ''}${this.spot === i ? ' sp' : ''}${this.viz.feature_group && this.viz.feature_group[i] === 'cross_book' ? ' xb' : ''}\" data-f=\"${i}\"", 1)
old = "<span><button data-vact=\"all\">all</button><button data-vact=\"top\">top 8</button><button data-vact=\"none\">none</button></span></div><div class=\"vl\">'"
assert old in s
s = s.replace(old, "<span><button data-vact=\"all\">all</button><button data-vact=\"top\">top 8</button><button data-vact=\"none\">none</button></span></div>' + (this.viz.feature_group ? '<div class=\"vgrp\"><i class=\"a\"></i>anchor book <i class=\"x\"></i>cross-book block (multibook_v1)</div>' : '') + '<div class=\"vl\">'", 1)
open(p, 'w').write(s)
# ---- css
c = W + 'css/style.css'; t = open(c).read()
t += """
.vt-view-vars .vr.xb .nm { color: #8ff0d2; } .vt-view-vars .vgrp { font-size: 9px; color: #7d8794; padding: 2px 0 4px; display: flex; align-items: center; gap: 4px; }
.vt-view-vars .vgrp i { width: 8px; height: 8px; border-radius: 2px; display: inline-block; margin-left: 4px; } .vgrp i.a { background: #e6ebf5; margin-left: 0; } .vgrp i.x { background: #6ee2c4; }
.vt-mcard-btn { margin-left: 6px; font: 700 10px monospace; letter-spacing: 1px; padding: 3px 8px; border-radius: 12px; border: 1px solid #6ee2c4; background: transparent; color: #6ee2c4; cursor: pointer; }
#vt-mcard { position: fixed; inset: 0; z-index: 9000; background: rgba(0,0,0,.6); display: flex; align-items: center; justify-content: center; padding: 16px; }
#vt-mcard[hidden] { display: none; } #vt-mcard .box { width: min(760px, 100%); max-height: 88vh; overflow: auto; background: #0e1217; border: 1px solid #2a3340; border-radius: 10px; padding: 16px 18px; font-size: 12px; color: #cfd6df; }
#vt-mcard h3 { margin: 0 0 2px; font-size: 13px; letter-spacing: 1.4px; } #vt-mcard h5 { margin: 12px 0 4px; font-size: 10px; letter-spacing: 1.2px; text-transform: uppercase; color: #9aa4b2; }
#vt-mcard .kv { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 6px 14px; } #vt-mcard .kv div span { display: block; color: #7d8794; font-size: 10px; } #vt-mcard .kv b { font-size: 13px; }
#vt-mcard .warn { color: #ffb86b; } #vt-mcard .ok { color: #6fd6a0; } #vt-mcard .chip { display: inline-block; margin: 2px 4px 2px 0; padding: 1px 7px; border-radius: 10px; border: 1px solid #2a3340; font-size: 10px; } #vt-mcard .chip.x { border-color: #6ee2c4; color: #8ff0d2; } #vt-mcard .chip.off { text-decoration: line-through; opacity: .5; }
#vt-mcard ul { margin: 4px 0 0 16px; padding: 0; color: #9aa4b2; } #vt-mcard .close { float: right; background: none; border: 1px solid #2a3340; color: #cfd6df; border-radius: 6px; cursor: pointer; padding: 2px 9px; }
"""
open(c, 'w').write(t)
# ---- html: script tag
h = W + 'index.html'; x = open(h).read()
if 'model_card.js' not in x:
    x = x.replace('<script src="js/strike_lens.js"></script>', '<script src="js/strike_lens.js"></script>\n    <script src="js/model_card.js"></script>', 1)
    assert 'model_card.js' in x
open(h, 'w').write(x); print('ok')
