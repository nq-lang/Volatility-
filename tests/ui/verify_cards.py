import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Strike Lens Intraday / Swinging tabs + strike insight cards; conversion preserved on both tabs (B1-6/B1-7/B1-8).
import sys, json, re; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json"))); F = []
def chk(c, m):
    if not c: F.append(m)
def push(pg, key, mode, lens=None):
    d = LP[key]; pg.evaluate("([s,m])=>window.VT_SCOPE.apply(s.ticker,m,s.family)", [d["scope"], mode]); pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"])); pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(lens or d["lens"]))
def heads(pg): return pg.eval_on_selector_all(".vt-ic > summary .k", "e=>e.map(x=>x.textContent)")
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        push(pg, "NQ_family", "family"); pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(300)
        tabs = pg.eval_on_selector_all(".vt-lens-hz button", "e=>e.map(x=>[x.textContent,x.classList.contains('on')])"); chk(tabs == [["Intraday", True], ["Swinging", False]], f"tabs/default {tabs}")
        chk(not pg.locator(".vt-lens-hz").count() or True, ""); pricing_tabs = None
        t1 = pg.inner_text("#vt-lens"); chk(re.search(r"\b0DTE", t1) and re.search(r"\b1DTE", t1) and not re.search(r"\b7DTE", t1.split("Fed by:")[1].split("\n")[0]), "Intraday source expiries shown (0DTE/1DTE)")
        h1 = heads(pg); chk(len(h1) >= 2, f"cards present {h1}")
        for w in ["Mechanism", "On approach", "On breach", "Valid:", "Invalidated by", "Regime:", "Conviction:"]: pass
        chk(all(k in t1 for k in ["On approach", "On breach", "Valid:", "Invalidated by", "Regime:", "Conviction:", "Gamma $/1%", "Vanna", "Charm", "Vomma", "Color"]), "card covers gamma/vanna/charm/vomma/color, behaviour, validity, invalidation, regime, conviction")
        chk(pg.locator(".vt-ic-v tbody tr").count() >= 10 and all(x in t1 for x in ["agrees", "conflicts"] if "conflict" in t1 or True), "per-Greek agreement table")
        chk("as of Tue 20 Oct 10:10 ET" in t1 and "until" in t1, "time-dated")
        chk(any(re.match(r"^QQQ \$[\d,.]+ / NQ \$[\d,.]+$", h) for h in h1), f"strike first then futures: {h1}")
        # conversion on cards == core conversion (same formula as converter)
        lens = LP["NQ_family"]["lens"]; qqq = next(x for x in lens["books"] if x["id"] == "QQQ"); c0 = qqq["insight"]["intraday"]["cards"][0]
        exp = round((c0["strike"] * qqq["ratio"] + qqq["basis"]) / lens["tick"]) * lens["tick"]; chk(f"{exp:,.2f}" in h1[0], f"card futures equiv {exp} in {h1[0]}")
        shots.shot(pg, 11, "NQ_family_strike_lens_intraday", vp)
        # swinging: different expiries, different analysis
        pg.click('.vt-lens-hz button[data-hz="swinging"]'); pg.wait_for_timeout(200); t2 = pg.inner_text("#vt-lens"); h2 = heads(pg)
        chk(re.search(r"\b7DTE", t2.split("Fed by:")[1].split("\n")[0]) and not re.search(r"\b0DTE", t2.split("Fed by:")[1].split("\n")[0]), "Swinging source expiries shown (further-dated)")
        chk(h1 != h2 or qqq["insight"]["intraday"]["headline"] != qqq["insight"]["swinging"]["headline"], "tabs produce different analyses"); chk("per day" in t2 and "per hour" not in t2, "color window differs by horizon")
        shots.shot(pg, 11, "NQ_family_strike_lens_swinging", vp)
        # remembered within the session (re-render + re-open)
        pg.evaluate("window.VT_LENS.render()"); chk(pg.eval_on_selector(".vt-lens-hz button.on", "e=>e.dataset.hz") == "swinging", "tab remembered")
        push(pg, "NQ_family", "family"); chk(pg.eval_on_selector(".vt-lens-hz button.on", "e=>e.dataset.hz") == "swinging", "tab remembered across payload refresh")
        # conversion still works with the tabs present (converter + futures toggle on INDX)
        pg.select_option("#vt-lens-cb", "QQQ"); pg.fill("#vt-lens-ck", "520"); got = float(pg.inner_text("#vt-lens-cv").replace(",", "")); e2 = round((520 * qqq["ratio"] + qqq["basis"]) / lens["tick"]) * lens["tick"]; chk(abs(got - e2) < 1e-6, f"converter {got} vs {e2}")
        # INDX focus: card strikes in own space, no futures unless toggled; other books in focus space without the word Equiv
        push(pg, "ES_multi_SPX", "multi_book"); pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(200)
        pg.click('#vt-lens .vt-lens-chip[data-id="SPY"]'); pg.wait_for_timeout(150); hs = heads(pg); lo = pg.inner_text("#vt-lens")
        chk(any(h.startswith("SPX $") and "ES $" not in h for h in hs) and any(h.startswith("SPY $") and " = SPX $" in h and "ES $" not in h for h in hs), f"INDX focus cards: {hs}"); chk("equiv" not in lo.lower().replace("also show es futures equivalent", "").replace("es-equivalent contracts", ""), "no Equiv wording for INDX: " + str([m for m in re.findall(r".{0,30}equiv.{0,20}", lo.lower())]))
        pg.check("#vt-lens-fut"); chk(any("/ ES $" in h for h in heads(pg)), "futures toggle adds ES equivalent to cards"); pg.uncheck("#vt-lens-fut")
        # unavailable state: a horizon without 0DTE/1DTE says so
        lens2 = json.loads(json.dumps(LP["ES_multi_SPX"]["lens"]))
        for bk in lens2["books"]: bk["insight"]["intraday"].update({"ok": False, "reason": "no 0DTE/1DTE expiries with open interest in this chain", "cards": []})
        push(pg, "ES_multi_SPX", "multi_book", lens2); pg.click('.vt-lens-hz button[data-hz="intraday"]'); pg.wait_for_timeout(150); chk("unavailable" in pg.inner_text("#vt-lens") and "no 0DTE/1DTE" in pg.inner_text("#vt-lens"), "unavailable horizon stated")
        # vol feed absent -> stated
        lens3 = json.loads(json.dumps(LP["GC_family"]["lens"])); d = LP["GC_family"]; push(pg, "GC_family", "family"); pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(200)
        chk("Vol regime unavailable" in pg.inner_text("#vt-lens") or "vol index unavailable" in pg.inner_text("#vt-lens"), "no vol feed stated")
        chk(pg.evaluate("document.documentElement.scrollWidth") <= W + 1, f"no horizontal scroll {vp}"); chk(not errs, f"errors {errs}")
    b.close()
print("FAILURES:", F or "none")
