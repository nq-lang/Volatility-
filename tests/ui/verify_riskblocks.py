import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import sys, json; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json"))); F = []
def chk(c, m):
    if not c: F.append(m)
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        d = LP["NQ_family"]; pg.evaluate("([s])=>window.VT_SCOPE.apply(s.ticker,'family',s.family)", [d["scope"]]); pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"])); pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(d["lens"]))
        pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(300)
        t = pg.inner_text("#vt-lens"); chk("Hedge Flow" in t.replace("HEDGE FLOW", "Hedge Flow") and "contracts" in t and "Vanna" in t and "0DTE" in t, "hedge block on Greeks page: " + t[:80])
        chk("VOL & RISK" not in t.upper(), "no vol block on Greeks page")
        chk(pg.locator(".vt-hedge-t thead th").count() == 7, "7 labelled columns")
        rows = pg.eval_on_selector_all(".vt-hedge-t tbody tr", "r=>r.map(x=>[...x.cells].map(c=>c.textContent.trim()))")
        chk(any(r[0] == "Observed" for r in rows) and sum(r[0] == "Projected" for r in rows) >= 8, "observed and projected rows labelled")
        chk(all(len(r) == 7 and all(c for c in r) for r in rows), "every row fills every column (trigger, price, when, flow, meaning, effect)")
        chk(pg.evaluate("[...document.querySelectorAll('.vt-hedge-t td, .vt-hedge-t th')].every(c=>parseFloat(getComputedStyle(c).borderLeftWidth)>=1 && parseFloat(getComputedStyle(c).borderRightWidth)>=1)"), "vertical borders on every cell")
        chk(pg.locator(".vt-hedge-sum").inner_text().strip() != "" and ("AMPLIFIES" in pg.inner_text(".vt-hedge-sum") or "DAMPS" in pg.inner_text(".vt-hedge-sum") or "ASYMMETRIC" in pg.inner_text(".vt-hedge-sum")), "plain-language summary present")
        chk(all(("dealers" in r[5].lower()) for r in rows if r[0] == "Projected"), "meaning column states sign convention / who trades")
        chk("%" not in "".join(r[1][:3] for r in rows if r[1].startswith("-")) , "no bare percent")
        shots.shot(pg, 6, "NQ_family_greeks_hedge", vp)
        pg.click('[data-page="vt-page-pricing"]'); pg.wait_for_timeout(300); t = pg.inner_text("#vt-lens")
        chk("VOL & RISK" in t.upper() and ("contango" in t.lower() or "BACKWARDATED" in t), "vol block on Pricing page"); chk("HEDGE FLOWS" not in t.upper(), "no hedge on Pricing page"); chk("VRP n/a" in t, "VRP not invented without RV")
        shots.shot(pg, 6, "NQ_family_pricing_vol", vp); chk(not errs, f"errors {errs}")
    b.close()
print("FAILURES:", F or "none")
