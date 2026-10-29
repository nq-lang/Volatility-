import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
W = (VT_ROOT + '/web/')
p = W + 'js/sidebar_xbook.js'; s = open(p).read()
s = s.replace("} else if (window.flow) {\n            const fl = window.flow || flow;", "} else if (typeof flow !== 'undefined') {\n            const fl = flow;")
s = s.replace("const fl = window.flow, g = fl && fl.gex && fl.gex.term, P = typeof currentSpot !== 'undefined' ? currentSpot : null; if (!fl) return [];", "const fl = typeof flow !== 'undefined' ? flow : null, g = fl && fl.gex && fl.gex.term, P = typeof currentSpot !== 'undefined' ? currentSpot : null; if (!fl) return [];")
open(p, 'w').write(s)
p = W + 'js/app.js'; s = open(p).read()
old = "    if (pcaPanel) pcaPanel.updateLive(data);\n    renderPcaInsights(data);\n}"
assert old in s
s = s.replace(old, "    if (pcaPanel) pcaPanel.updateLive(data);\n    renderPcaInsights(data);\n    if (window.VT_SIDEBAR) VT_SIDEBAR.setRegime(data);\n}", 1)
# repaint the cross-book block whenever the legacy sidebar repaints (same cadence)
old = "function renderSidebar() {\n    const el = document.getElementById('vt-sidebar-body');\n    if (!el) return;"
assert old in s
s = s.replace(old, "function renderSidebar() {\n    const el = document.getElementById('vt-sidebar-body');\n    if (!el) return;\n    if (window.VT_SIDEBAR) VT_SIDEBAR.refresh();", 1)
open(p, 'w').write(s)
h = W + 'index.html'; x = open(h).read()
x = x.replace('<div id="vt-sidebar-body"></div>', '<div id="vt-sidebar-xb" class="vt-sidebar-xb"></div>\n            <div class="vt-xb-legacy-title">Primary book detail</div>\n            <div id="vt-sidebar-body"></div>', 1)
assert 'vt-sidebar-xb' in x
x = x.replace('    <div class="vt-shell">', '    <div id="vt-alertbar" class="vt-alertbar" hidden></div>\n\n    <div class="vt-shell">', 1)
assert 'vt-alertbar' in x
x = x.replace('<script src="js/model_card.js"></script>', '<script src="js/model_card.js"></script>\n    <script src="js/sidebar_xbook.js"></script>', 1)
assert 'sidebar_xbook' in x
open(h, 'w').write(x)
c = W + 'css/style.css'; t = open(c).read()
t += """
.vt-alertbar { display: flex; flex-wrap: wrap; gap: 6px 14px; align-items: center; padding: 6px 16px; background: #0b0f14; border-bottom: 1px solid var(--border); font-size: 11px; color: #b8c0cc; }
.vt-alertbar[hidden] { display: none; } .vt-alertbar .lbl { font: 800 9px monospace; letter-spacing: 1.4px; color: #ffa838; }
.vt-alertbar .al b { font-size: 9px; letter-spacing: 1px; margin-right: 4px; } .vt-alertbar .al.s3 { color: #ffb4b4; } .vt-alertbar .al.s3 b { color: #ff6b6b; } .vt-alertbar .al.s2 b { color: #ffa838; }
.vt-sidebar-xb { display: flex; flex-direction: column; gap: 10px; margin-bottom: 8px; }
.vt-xb-scope { display: flex; justify-content: space-between; gap: 8px; font-size: 10px; color: #7d8794; letter-spacing: .6px; } .vt-xb-scope b { color: #ffa838; letter-spacing: 1.2px; }
.vt-xb-al { display: flex; gap: 8px; align-items: flex-start; padding: 5px 7px; margin-bottom: 4px; border-left: 3px solid #3a4452; background: rgba(255,255,255,.02); font-size: 11px; line-height: 1.4; }
.vt-xb-al .tag { flex: 0 0 74px; font: 800 9px monospace; letter-spacing: 1px; color: #7d8794; padding-top: 2px; } .vt-xb-al.s3 { border-color: #ff5d5d; background: rgba(255,93,93,.07); } .vt-xb-al.s2 { border-color: #ffa838; } .vt-xb-al.s1 { border-color: #3d7fd8; }
.vt-xb-lv { display: grid; grid-template-columns: 92px 1fr auto; gap: 6px; font-size: 11px; padding: 3px 0; border-bottom: 1px dashed rgba(255,255,255,.06); font-variant-numeric: tabular-nums; } .vt-xb-lv .n { color: #9aa4b2; } .vt-xb-lv .p { color: #e6ebf5; } .vt-xb-lv.up .d { color: #6fd6a0; } .vt-xb-lv.dn .d { color: #ff8f8f; }
.vt-xb-row { display: grid; grid-template-columns: 64px 54px 32px 62px 1fr auto; gap: 6px; align-items: center; font-size: 10.5px; padding: 3px 0; } .vt-xb-row.out { background: rgba(255,168,56,.08); } .vt-xb-row.off { color: #5c6570; display: flex; gap: 8px; }
.vt-xb-row .bar { height: 5px; background: #1b222b; border-radius: 3px; overflow: hidden; } .vt-xb-row .bar i { display: block; height: 100%; background: #6ee2c4; } .vt-xb-row .o { color: #9aa4b2; } .vt-xb-row .z, .vt-xb-row .bk { color: #ffa838; font-size: 9px; }
.vt-xb-badge { margin-left: 6px; padding: 1px 6px; border: 1px solid #2a3340; border-radius: 9px; font-size: 9px; text-transform: none; letter-spacing: .3px; color: #9aa4b2; } .vt-xb-badge.ok { border-color: #2f8f63; color: #6fd6a0; } .vt-xb-badge.warn { border-color: #b07a22; color: #ffb86b; }
.vt-xb-line { font-size: 11px; line-height: 1.45; margin: 3px 0; color: #cfd6df; } .vt-xb-read summary { font-size: 10px; color: #7d8794; cursor: pointer; margin-top: 4px; } .vt-xb-ts { font-size: 9px; color: #5c6570; margin-top: 4px; }
.vt-xb-legacy-title { font: 800 9px monospace; letter-spacing: 1.4px; color: #5c6570; text-transform: uppercase; margin: 6px 0 4px; }
"""
open(c, 'w').write(t); print('ok')
