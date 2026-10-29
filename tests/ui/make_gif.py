import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import sys, io, json; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
from PIL import Image
import shots
fr = []
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    pg = b.new_context(viewport={"width": 1700, "height": 1000}).new_page(); shots.load(pg)
    pg.evaluate("document.querySelector('.vt-view-tab[data-m=pca2d]').click()"); pg.wait_for_timeout(1200); pg.evaluate(shots.WM)
    V = "window.__pca.views"; n = pg.evaluate(f"{V}._timeline().length"); nf = pg.evaluate(f"{V}.viz.feature_short.length"); print("timeline", n, "features", nf)
    N = 48
    for i in range(N):
        t = round(i * (n - 1) / (N - 1)); sp = (i // 6) % nf if i % 12 >= 3 else -1
        pg.evaluate(f"(()=>{{const V={V}; V.stopPlay&&V.stopPlay(); V.tpos={t}; V.spot={sp}; V._syncScrub&&V._syncScrub(); V._renderVars&&V._renderVars(); V.draw();}})()"); pg.wait_for_timeout(120)
        im = Image.open(io.BytesIO(pg.locator(".vt-pca-biplot-wrap").screenshot())).convert("RGB"); im.thumbnail((1100, 800)); fr.append(im)
    b.close()
pal = [f.quantize(colors=128, method=Image.Quantize.MEDIANCUT) for f in fr]
pal[0].save("/tmp/gif/pca_replay.gif", save_all=True, append_images=pal[1:], duration=170, loop=0, optimize=True)
fr[0].save("/tmp/gif/first.png")
