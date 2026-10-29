import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# 2D views layout: every plotted point inside the plot rectangle, boxes never overlap each other or the plot, variables show/hide + highlight work.
import sys; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
F = []
def chk(c, m):
    if not c: F.append(m)
JS_BOXES = """() => { const V = window.__pca.views, wrapR = V.panel.container.closest('.vt-pca-biplot-wrap').getBoundingClientRect(), cr = V.canvas.getBoundingClientRect();
  const bx = e => { const r = e.getBoundingClientRect(); return e.offsetParent === null ? null : { x0: r.left - cr.left, y0: r.top - cr.top, x1: r.right - cr.left, y1: r.bottom - cr.top }; };
  return { rect: V.rect, size: V.size, boxes: { vars: bx(V.vars), card: bx(V.card), legend: bx(V.panel.legendBox), scrub: bx(V.scrub), ribbon: bx(V.ribbon), foot: bx(V.foot), bar: bx(V.bar) } }; }"""
def ov(a, b): return a and b and a["x0"] < b["x1"] - 1 and a["x1"] > b["x0"] + 1 and a["y0"] < b["y1"] - 1 and a["y1"] > b["y0"] + 1
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        for mode in ["pca2d", "tsne2d"]:
            pg.evaluate(f"document.querySelector('.vt-view-tab[data-m={mode}]').click()"); pg.wait_for_timeout(1400)
            d = pg.evaluate(JS_BOXES); R = d["rect"]; V = "window.__pca.views"
            pts = pg.evaluate(f"{V}._hit.filter(h=>h.type==='pt').map(h=>[h.sx,h.sy])"); out = [q for q in pts if not (R["x0"] <= q[0] <= R["x1"] and R["y0"] <= q[1] <= R["y1"])]
            chk(len(pts) > 200 and not out, f"{vp}/{mode}: {len(out)} of {len(pts)} points outside the plot rectangle {R}")
            names = [k for k, v in d["boxes"].items() if v]; rect = {"x0": R["x0"], "x1": R["x1"], "y0": R["y0"], "y1": R["y1"]}
            for i, a in enumerate(names):
                if a in ("bar",): continue
                chk(not ov(d["boxes"][a], rect), f"{vp}/{mode}: {a} overlaps the plot rectangle")
                for c in names[i + 1:]:
                    if c == "bar": continue
                    chk(not ov(d["boxes"][a], d["boxes"][c]), f"{vp}/{mode}: {a} overlaps {c}")
            chk(d["boxes"]["vars"] and d["boxes"]["card"], f"{vp}/{mode}: vars + card visible")
            # variables: hide/show/highlight
            if mode == "pca2d":
                n0 = pg.evaluate(f"{V}._hit.filter(h=>h.type==='feature').length"); nfeat = pg.evaluate(f"{V}.viz.feature_short.length")
                pg.click('.vt-view-vars [data-vact="none"]'); n1 = pg.evaluate(f"{V}._hit.filter(h=>h.type==='feature').length"); chk(n1 == 0 and n0 > 5, f"none hides arrows {n0}->{n1}")
                chk(pg.evaluate(f"{V}.vars.querySelectorAll('.vr.off').length") == nfeat, "all rows marked hidden")
                pg.click('.vt-view-vars [data-vact="top"]'); n2 = pg.evaluate(f"{V}._hit.filter(h=>h.type==='feature').length"); chk(0 < n2 <= 8, f"top 8: {n2}")
                pg.click('.vt-view-vars [data-vact="all"]'); chk(pg.evaluate(f"{V}._hit.filter(h=>h.type==='feature').length") == n0, "all restores")
                pg.click('.vt-view-vars .vr[data-f="3"] .eye'); chk(pg.evaluate(f"{V}._hit.filter(h=>h.type==='feature').length") == n0 - 1, "eye hides one arrow")
                pg.click('.vt-view-vars .vr[data-f="3"] .eye')
                pg.click('.vt-view-vars .vr[data-f="5"] .nm'); chk(pg.evaluate(f"{V}.spot") == 5 and pg.evaluate("document.querySelector('[data-act=colorby]').value") == "5", "click name = highlight + colour by it")
                chk(pg.evaluate(f"{V}.vars.querySelector('.vr.sp') !== null"), "highlighted row styled"); pg.click('.vt-view-vars .vr[data-f="5"] .nm'); chk(pg.evaluate(f"{V}.spot") == -1, "second click clears highlight")
                shots.shot(pg, 5, "SPX_single_pca2d_layout", vp, locator=".vt-pca-biplot-wrap")
            else: shots.shot(pg, 5, "SPX_single_tsne2d_layout", vp, locator=".vt-pca-biplot-wrap")
        # leaving the view restores the original layout (height + inline styles)
        pg.evaluate("document.querySelector('.vt-view-tab[data-m=pca3d]').click()"); pg.wait_for_timeout(1200)
        chk(pg.evaluate("window.__pca.container.closest('.vt-pca-biplot-wrap').style.height") == "", f"{vp}: wrap height restored in PCA 3D")
        chk(not errs, f"page errors {errs}")
    b.close()
print("FAILURES:", F if F else "none")
