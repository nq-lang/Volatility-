import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Full-page real captures: every page x three scopes (Single-Book, Multi-Book, Family), desktop + mobile. MOCK-watermarked fixtures.
import sys, json; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json"))); SP = json.load(open((VT_ROOT + "/preview/synthetic_scoped_payload.json"))); CARD = json.load(open((VT_ROOT + "/preview/synthetic_mbmodel_payload.json")))
def feed(pg, key, mode, sp):
    d = LP[key]; pg.evaluate("([s,m])=>window.VT_SCOPE.apply(s.ticker,m,s.family)", [d["scope"], mode]); pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"])); pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(d["lens"]))
    pg.evaluate("(j)=>window.onMultibookModel(j)", json.dumps(CARD)); pg.evaluate("(j)=>window.onScopedInsight(j)", json.dumps(sp)); pg.wait_for_timeout(400)
PAGES = [("vt-page-main", "volatility"), ("vt-page-pricing", "pricing"), ("vt-page-greeks", "greeks")]
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        for scope, key, mode, sp in [("single_book", None, None, None), ("multi_book_SPX_ES", "ES_multi_SPX", "multi_book", SP["family"]), ("family_NQ", "NQ_family", "family", SP["family"])]:
            pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); shots.load(pg)
            if key: feed(pg, key, mode, sp)
            for page, nm in PAGES:
                pg.click(f'[data-page="{page}"]'); pg.wait_for_timeout(600); shots.shot(pg, 9, f"{nm}__{scope}", vp)
            pg.close()
    b.close()
print("done")
