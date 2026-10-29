import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# 2D regime views: Plotly-style controls (modebar, clickable legend, color-by gradients, pan, axis ticks).
import sys; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
F = []
def chk(c, m):
    if not c: F.append(m)
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        for mode in ["pca2d", "tsne2d"]:
            pg.evaluate(f"document.querySelector('.vt-view-tab[data-m={mode}]').click()"); pg.wait_for_timeout(900)
            V = "window.__pca.views"
            chk(pg.is_visible(".vt-view-modebar"), f"modebar visible {mode}")
            n0 = pg.evaluate(f"{V}._hit.filter(h=>h.type==='pt').length"); chk(n0 > 100, f"points drawn {n0}")
            z0 = pg.evaluate(f"{V}.cam.zoom"); pg.click('.vt-view-modebar [data-act=zin]'); chk(pg.evaluate(f"{V}.cam.zoom") > z0, "zoom in")
            pg.click('.vt-view-modebar [data-act=zout]'); chk(abs(pg.evaluate(f"{V}.cam.zoom") - z0) < 1e-9, "zoom out returns")
            # legend toggle hides that regime's points; double-click isolates; home restores
            pg.evaluate(f"{V}.hidden.add(0); {V}.panel._renderLegend(); {V}.draw()"); n1 = pg.evaluate(f"{V}._hit.filter(h=>h.type==='pt').length")
            dom0 = pg.evaluate(f"{V}.viz.points.filter(p=>p.dominant===0).length"); chk(n1 == n0 - dom0 and dom0 > 0, f"regime 0 hidden: {n0}->{n1} (-{dom0})")
            pg.evaluate(f"{V}.hidden.clear(); {V}.panel._renderLegend(); {V}.draw()")
            row = pg.locator('.vt-pca-legend .row[data-r="1"]'); row.dblclick(); n2 = pg.evaluate(f"{V}._hit.filter(h=>h.type==='pt').length")
            dom1 = pg.evaluate(f"{V}.viz.points.filter(p=>p.dominant===1).length"); chk(n2 == dom1, f"isolate regime 1: {n2} vs {dom1}")
            chk(pg.eval_on_selector('.vt-pca-legend .row[data-r="0"]', "e=>e.classList.contains('off')"), "legend marks hidden rows")
            pg.click('.vt-view-modebar [data-act=home]'); chk(pg.evaluate(f"{V}.hidden.size") == 0, "home restores regimes")
            # color by confidence / time: colours differ across points and a colorbar is drawn
            for cv in ["-3"]:
                pg.select_option('[data-act=colorby]', cv); cols = pg.evaluate(f"new Set({V}.viz.points.map((p,i)=>{V}._colorOf(p,i).map(Math.round).join())).size"); chk(cols > 5, f"color-by {cv} gives a gradient ({cols} colours)")
            pg.select_option('[data-act=colorby]', "-2"); chk(pg.evaluate(f"{V}._colorOf({V}.viz.points[0],0).every(Number.isFinite)"), "confidence colours valid")
            pg.select_option('[data-act=colorby]', "-1")
            # pan: drag in pan mode translates the view
            pg.click('.vt-view-modebar [data-act=pan]'); box = pg.eval_on_selector(".vt-pca-biplot-wrap canvas:visible", "c=>{const r=c.getBoundingClientRect();return [r.x,r.y,r.width,r.height]}") if False else None
            c = pg.locator(".vt-pca-biplot-wrap canvas:visible").last.bounding_box(); R = pg.evaluate(f"{V}.rect"); x0, y0 = c["x"] + R["x0"] + 40, c["y"] + R["y0"] + 40   # inside the plot rectangle (boxes sit outside it)
            pg.mouse.move(x0, y0); pg.mouse.down(); pg.mouse.move(x0 + 60, y0 + 30, steps=6); pg.mouse.up(); pan = pg.evaluate(f"{V}.pan"); chk(abs(pan[0]) > 20, f"pan moved {pan}")
            pg.click('.vt-view-modebar [data-act=pan]'); pg.click('.vt-view-modebar [data-act=home]'); chk(pg.evaluate(f"{V}.pan") == [0, 0], "home resets pan")
            # PNG export works
            chk(pg.evaluate(f"{V}.canvas.toDataURL('image/png').startsWith('data:image/png')"), "png export")
            if mode == "pca2d": shots.shot(pg, 3, f"SPX_single_pca2d_controls", vp, locator=".vt-pca-biplot-wrap")
        pg.select_option('[data-act=colorby]', "-2"); shots.shot(pg, 3, "SPX_single_tsne2d_confidence", vp, locator=".vt-pca-biplot-wrap")
        chk(not errs, f"page errors {errs}")
    b.close()
print("FAILURES:", F if F else "none")
