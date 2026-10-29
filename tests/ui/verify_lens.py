import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Strike Lens acceptance: INDX focus (no futures unless toggled), Pricing page = pricing levels only, greeks page = Greek levels, family = futures primary.
import sys, json
sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json")))
F = []
def chk(c, m):
    if not c: F.append(m)
def push(pg, key):
    d = LP[key]; pg.evaluate("([s])=>window.VT_SCOPE.apply(s.ticker,s.mode==='family'?'family':'multi_book',s.family)", [d["scope"]])
    pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"])); pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(d["lens"]))
def tick_round(v, t): return round(v / t) * t
def col(pg, name): return pg.evaluate("(n)=>{const h=[...document.querySelectorAll('.vt-lens-t thead th')].map(x=>x.textContent);return h.indexOf(n)}", name)
def cells(pg, i): return pg.eval_on_selector_all(".vt-lens-t tbody tr", f"r=>r.map(x=>x.cells[{i}].textContent)")
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        chk(pg.evaluate("document.getElementById('vt-lens').hidden"), "lens hidden in Single-Book")
        chk(pg.evaluate("Object.values(window.__panels).every(p=>!p.panel.lens)"), "no panel lens in Single-Book")
        chk("ES" not in pg.inner_text("#vt-scope-badge").replace("MULTI", ""), "no futures in single badge")
        push(pg, "ES_multi_SPX"); lens = LP["ES_multi_SPX"]["lens"]; spx = next(x for x in lens["books"] if x["id"] == "SPX"); spy = next(x for x in lens["books"] if x["id"] == "SPY")
        # ---- pricing page: pricing levels only, focus on SPX, no futures column
        pg.click('[data-page="vt-page-pricing"]'); pg.wait_for_timeout(300)
        chk(pg.is_visible("#vt-lens") and "Strike Lens" in pg.inner_text(".vt-lens-h") and "PRICING" not in pg.inner_text(".vt-lens-h").upper() and pg.eval_on_selector_all(".vt-lens-h em", "e=>e.map(x=>x.textContent)") == ["SPX"], "pricing lens visible, titled Strike Lens · SPX (B1-3/B1-4)")
        chk(pg.locator(".vt-lens-h span").count() == 0, "subtitle removed (B1-2)"); chk("Book" not in [x for x in pg.eval_on_selector_all(".vt-lens-t thead th", "e=>e.map(x=>x.textContent)")], "no Book column (B1-5)")
        labs = set(cells(pg, 0)); chk(labs and labs <= {l["label"] for l in spx["pricing"]}, "pricing labels only: %s" % labs)
        chk(not ({"Gamma Flip", "Call Wall", "Put Wall", "Top |GEX|"} & labs), "no Greek levels on Pricing page: %s" % labs)
        chk({"Exp Move +1s", "RND Median", "25D Put"} <= labs, "key pricing levels present")
        chk(pg.eval_on_selector_all("#vt-lens .vt-lens-chip.on", "e=>e.map(x=>x.dataset.id)") == ["SPX"], "default focus = selected ticker")
        txt = pg.inner_text("#vt-lens"); chk("ES equiv" not in txt and "futures" not in txt.replace("also show ES futures equivalent", ""), "no futures content for INDX focus")
        hdr = pg.eval_on_selector_all(".vt-lens-t thead th", "e=>e.map(x=>x.textContent)"); chk(not any("equiv" in h.lower() for h in hdr), "no Equiv column for INDX focus (B1-1): %s" % hdr)
        shots.shot(pg, 4, "SPX_multibook_pricing_lens", vp)
        # add SPY: its levels expressed in SPX space (via futures), still no futures column
        pg.click('#vt-lens .vt-lens-chip[data-id="SPY"]'); rows = pg.eval_on_selector_all(".vt-lens-t tbody tr", "r=>r.map(x=>[x.cells[0].querySelector('i').title,x.cells[0].textContent,x.cells[2].textContent])")
        r = next(x for x in rows if x[0] == "SPY" and x[1] == "RND Median"); lv = next(l for l in spy["pricing"] if l["label"] == "RND Median")
        exp = lv["fut"] - 12.5; chk(abs(float(r[2].replace(",", "")) - exp) < 0.011, "SPY RND median in SPX space: %s vs %.2f" % (r, exp))
        chk(col(pg, "ES equiv") == -1 and col(pg, "In SPX") == 2 and not any("equiv" in h.lower() for h in pg.eval_on_selector_all(".vt-lens-t thead th", "e=>e.map(x=>x.textContent)")), "other-book column is plain In SPX, no ES column by default")
        pg.check("#vt-lens-fut"); chk(col(pg, "ES equiv") >= 0, "ES column after toggle")
        nm = pg.evaluate("window.__panels.vrp.panel._lensMarkers().length"); chk(nm > 0, "pricing markers on VRP panel: %s" % nm)
        mk = pg.evaluate("window.__panels.vrp.panel.lens.markers()"); chk(all(("Flip" not in m["label"] and "Wall" not in m["label"]) for m in mk), "no Greek markers on pricing panels")
        chk(abs(pg.evaluate("window.__panels.vrp.panel.lens.toFut(6000)") - 6012.5) < 1e-9, "panel toFut after toggle")
        pg.uncheck("#vt-lens-fut"); chk(pg.evaluate("window.__panels.vrp.panel.lens.toFut") is None, "no futures hover when toggle off")
        # converter in SPX space
        pg.select_option("#vt-lens-cb", "SPY"); pg.fill("#vt-lens-ck", "600"); got = float(pg.inner_text("#vt-lens-cv").replace(",", ""))
        exp = (tick_round(600 * spy["ratio"] + spy["basis"], lens["tick"]) - 12.5); chk(abs(got - exp) < 0.011, "converter SPY 600 -> SPX %s (got %s)" % (exp, got))
        # ---- greeks page: Greek levels, same selection
        pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(300)
        chk(pg.evaluate("document.getElementById('vt-lens').parentElement.id") == "vt-lens-slot-cf" and pg.eval_on_selector_all(".vt-lens-h em", "e=>e.map(x=>x.textContent)") == ["SPX", "SPY"], "greeks lens titled Strike Lens · SPX SPY")
        ex = pg.eval_on_selector_all(".vt-lens-t td.ex", "e=>e.map(x=>x.textContent)"); chk(ex and all(x == "–" or x[0] in "+\u2212" for x in ex), "signed exposure per strike: %s" % ex); chk("$ per 1% move" in pg.inner_text("#vt-lens"), "exposure unit stated once")
        labs = set(cells(pg, 0)); chk({"Gamma Flip", "Call Wall", "Put Wall"} <= labs and not any(l.startswith(("RND", "Exp", "25D")) for l in labs), "greek labels only: %s" % labs)
        chk(pg.eval_on_selector_all("#vt-lens .vt-lens-chip.on", "e=>e.map(x=>x.dataset.id)") == ["SPX", "SPY"], "selection preserved")
        mk = pg.evaluate("window.__panels.gex.panel.lens.markers()"); chk(mk and all(("Flip" in m["label"] or "W" in m["label"]) for m in mk), "greek markers on gex panel")
        shots.shot(pg, 4, "SPX_multibook_greeks_lens", vp)
        # ---- degraded
        push(pg, "ES_multi_SPX_degraded"); ex = pg.eval_on_selector_all(".vt-lens-ex", "e=>e.map(x=>x.textContent)")
        chk(any("SPY" in t for t in ex) and any("XSP" in t for t in ex), "excluded books listed: %s" % ex); chk("72%" in pg.inner_text(".vt-lens-conf"), "confidence downgraded")
        shots.shot(pg, 4, "SPX_multibook_degraded_greeks_lens", vp)
        # ---- family: futures primary
        push(pg, "NQ_family"); pg.click('[data-page="vt-page-pricing"]'); pg.wait_for_timeout(200)
        chk("NQ equiv" in pg.inner_text("#vt-lens") and "Strike Lens" in pg.inner_text(".vt-lens-h"), "family: futures primary keeps Equiv (futures)")
        chk(pg.query_selector("#vt-lens-fut") is None, "no futures toggle in family")
        pg.evaluate("window.VT_SCOPE.apply('SPX','single',null)"); chk(pg.evaluate("document.getElementById('vt-lens').hidden"), "hidden after returning to Single")
        chk(pg.evaluate("Object.values(window.__panels).every(p=>!p.panel.lens)"), "panel lens cleared")
        push(pg, "GC_family"); pg.click('[data-page="vt-page-pricing"]'); pg.wait_for_timeout(200)
        chips = dict(pg.eval_on_selector_all(".vt-lens-chip", "e=>e.map(x=>[x.dataset.id,x.disabled])")); chk(chips.get("OG") is False and chips.get("GDX") is True, "gold chips: %s" % chips)
        shots.shot(pg, 4, "GC_family_pricing_lens", vp)
        chk(not errs, "page errors: %s" % errs); chk(pg.evaluate("document.documentElement.scrollWidth") <= W + 1, "no horizontal scroll (%s)" % vp)
    b.close()
print("FAILURES:", F if F else "none")
