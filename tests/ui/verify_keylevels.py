import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Page-1 Key Levels: strike first, then futures equivalent ("QQQ $574.5 / NQ $24,475"). Single-Book stays plain.
import sys, json, re; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json"))); F = []
def chk(c, m):
    if not c: F.append(m)
def push(pg, key):
    d = LP[key]; pg.evaluate("([s])=>window.VT_SCOPE.apply(s.ticker,s.mode==='family'?'family':'multi_book',s.family)", [d["scope"]])
    pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"])); pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(d["lens"]))
RX = re.compile(r"^[A-Z_/]+ \$[\d,]+(\.\d+)? / [A-Z]{2} \$[\d,]+(\.\d+)?$")
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        chk(pg.locator(".vt-lvl-eq").count() == 0, "Single-Book: no equivalents")
        push(pg, "ES_multi_SPX"); pg.wait_for_timeout(300)
        L = pg.eval_on_selector_all("#vt-insight-current .vt-lvl-eq > div", "e=>e.map(x=>x.textContent)"); chk(len(L) >= 4, f"{vp}: lines {L}")
        chk(all(RX.match(t) for t in L), f"{vp}: format {L}"); chk(all(t.startswith("SPX ") and "/ ES $" in t for t in L), "SPX strike first, ES second")
        pg.click('#vt-insight-current .vt-lvl-chips [data-id="SPY"]'); pg.wait_for_timeout(200)
        L2 = pg.eval_on_selector_all("#vt-insight-current .vt-lvl-eq > div", "e=>e.map(x=>x.textContent)"); chk(any(t.startswith("SPY") for t in L2) and len(L2) == 2 * len(L), f"{vp}: SPY adds lines {len(L2)}")
        spy = [t for t in L2 if t.startswith("SPY")][0]; spx = [t for t in L2 if t.startswith("SPX")][0]; chk(spy.split(" / ")[1] == spx.split(" / ")[1], "same futures price for both books")
        shots.shot(pg, 6, "SPX_multibook_main_keylevels", vp, locator="#vt-insight-current")
        push(pg, "NQ_family"); pg.wait_for_timeout(300)
        L3 = pg.eval_on_selector_all("#vt-insight-current .vt-lvl-eq > div", "e=>e.map(x=>x.textContent)"); chk(all("/ NQ $" in t for t in L3) and L3, f"{vp}: family {L3[:2]}")
        print(vp, L[:2], L2[:3], L3[:2])
        chk(not errs, f"errors {errs}")
    b.close()
print("FAILURES:", F or "none")
