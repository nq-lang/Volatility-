import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Pricing Surfaces analysis blocks (A-1..A-8): block set, inputs shown, unavailable states, cascade composer, no overflow.
import sys, json; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json"))); F = []
def chk(c, m):
    if not c: F.append(m)
def push(pg, key):
    d = LP[key]; pg.evaluate("([s])=>window.VT_SCOPE.apply(s.ticker,s.mode==='family'?'family':'multi_book',s.family)", [d["scope"]])
    pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"])); pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(d["lens"]))
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        chk(pg.evaluate("document.getElementById('vt-pa').hidden"), "hidden in Single-Book")
        push(pg, "NQ_family"); pg.click('[data-page="vt-page-pricing"]'); pg.wait_for_timeout(300)
        chk(pg.is_visible("#vt-pa"), "analysis visible on Pricing page in Family")
        heads = pg.eval_on_selector_all("#vt-pa .vt-pa-blk h4", "e=>e.map(x=>x.childNodes[0].textContent.trim())")
        chk(heads == ["Risk Pricing", "Distribution", "Options-Market View", "Fundamental View", "Intraday Structure", "Weekly Structure", "Cross-Asset Cascades"], f"block set {heads}")
        chk(pg.locator("#vt-pa details.vt-pa-in").count() >= 7, "every block shows its inputs (A-8)")
        t = pg.inner_text("#vt-pa"); chk("unavailable" in t.lower() and "not inferred" in t, "fundamental gaps stated, not inferred")
        chk(pg.locator(".vt-pa-hist i").count() == 25, "distribution histogram drawn")
        chk("25Δ risk reversal" in t and "Gamma flip" in t, "risk + options rows present")
        pg.select_option("#vt-pa-trig", index=0); n0 = pg.locator(".vt-pa-stages li").count()
        pg.select_option('select[data-fx="dxy"]', "up"); pg.select_option('select[data-fx="bonds"]', "up"); pg.wait_for_timeout(100)
        n1 = pg.locator(".vt-pa-stages li").count(); st = pg.inner_text(".vt-pa-stages")
        chk(n1 == n0 + 3 and "Combined" in st and "scenario assumption" in st, f"composer adds dxy, bonds, combined stages ({n0}->{n1})")
        l0 = pg.inner_text("#vt-pa-trig option:checked"); pg.select_option("#vt-pa-trig", index=2); chk(pg.inner_text("#vt-pa-trig option:checked") != l0 and "Break above" in pg.inner_text("#vt-pa-trig option:checked"), "trigger switch (up break)")
        pg.click('.vt-pa-bk:nth-child(2)'); pg.wait_for_timeout(100); chk(pg.locator("#vt-pa .vt-pa-bk.on").count() == 1, "book switch")
        push(pg, "GC_family"); pg.wait_for_timeout(150); chk("No rule table" in pg.inner_text("#vt-pa") or "gold" in pg.inner_text("#vt-pa").lower() or "dollar" in pg.inner_text("#vt-pa").lower(), "metal class has its own rules")
        chk(pg.evaluate("document.documentElement.scrollWidth") <= W + 2, "no horizontal page scroll")
        push(pg, "NQ_family"); pg.wait_for_timeout(150)
        chk("VXN" in pg.inner_text("#vt-pa"), "pricing analysis carries the VXN regime")
        pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(250)
        hv = pg.inner_text(".vt-hedge-vol") if pg.locator(".vt-hedge-vol").count() else ""; chk("VXN" in hv and "governs" in hv, f"hedge flow vol line: {hv[:80]}")
        sb = pg.inner_text(".vt-xb-vol") if pg.locator(".vt-xb-vol").count() else ""; chk("vanna" in sb.lower() and "Vomma" in sb and "VXN" in sb, "sidebar vol regime + vanna/vomma")
        chk(("diverge" in sb) or ("agree" in sb), "VXN vs VIX divergence statement")
        push(pg, "ES_multi_SPX"); pg.wait_for_timeout(150); chk("VIX" in pg.inner_text(".vt-xb-vol"), "S&P scope reads VIX")
        pg.click('[data-page="vt-page-pricing"]'); pg.wait_for_timeout(150)
        shots.shot(pg, 12, "pricing_analysis", vp); chk(not errs, f"page errors {errs}")
    b.close()
print("FAILURES:", "; ".join(F) if F else "none")
