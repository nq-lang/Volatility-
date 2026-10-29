import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Cross-book sidebar: scope line, ranked RIGHT NOW alerts (+ bar on every page), key control strikes as "BOOK $strike / ANCHOR $fut", cross-book rows, scope-badged validated read.
import sys, json, re; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json"))); SP = json.load(open((VT_ROOT + "/preview/synthetic_scoped_payload.json"))); CARD = json.load(open((VT_ROOT + "/preview/synthetic_mbmodel_payload.json")))
F = []
def chk(c, m):
    if not c: F.append(m)
def push(pg, key, mode):
    d = LP[key]; pg.evaluate("([s,m])=>window.VT_SCOPE.apply(s.ticker,m,s.family)", [d["scope"], mode]); pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"])); pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(d["lens"]))
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        pg.click('[data-page="vt-page-greeks"]'); pg.wait_for_timeout(300)
        t = pg.inner_text("#vt-sidebar-xb"); chk("SINGLE-BOOK" in t and "single_v1" in t, "single scope line"); chk("Session Greek tracking is a Multi-Book" in t, "single: session tracking stated unavailable, single_v1 unchanged"); chk(pg.locator(".vt-xb-row").count() == 0, "no cross-book block in Single-Book"); chk("NQ" not in t and "ES " not in t, "no futures content in Single-Book sidebar")
        chk(pg.locator(".vt-xb-al").count() >= 1, "single: ranked alerts from the one book"); chk(pg.locator(".vt-xb-lv").count() >= 1, "single: control strikes from the one book")
        pg.evaluate("(j)=>window.onScopedInsight(j)", json.dumps(SP["family"])); chk(pg.locator(".vt-xb-read").count() == 0, "a read generated under another scope is never shown")
        # ---- family
        pg.evaluate("(j)=>window.onMultibookModel(j)", json.dumps(CARD)); push(pg, "NQ_family", "family"); pg.evaluate("(j)=>window.onScopedInsight(j)", json.dumps(SP["family"])); pg.wait_for_timeout(300)
        t = pg.inner_text("#vt-sidebar-xb"); chk("FAMILY" in t and "NQ" in t and "multibook_v1" in t and "confidence" in t, "family scope line")
        tags = pg.evaluate("VT_SIDEBAR.alerts().map(a=>a.tag)"); chk({"GAMMA", "ZERO-GAMMA", "HEDGE FLOW"} <= set(tags), f"alerts {tags}")
        st = pg.inner_text("#vt-sidebar-xb"); su = st.lower(); chk("dealer greeks through the session" in su and "charm is" in su and "vanna × charm" in su and "into the close" in su and "as of tue 20 oct 10:10 et" in su and "tracking 09:30 et" in su, "session charm tracking + interaction + as-of/window")
        chk(pg.locator(".vt-xb-sp .l").count() == 1 and pg.locator(".vt-xb-gr").count() >= 3 and pg.locator(".vt-xb-gr.strengthening, .vt-xb-gr.fading, .vt-xb-gr.steady").count() >= 3, "charm series line + per-Greek strengthening/fading")
        chk(pg.locator(".vt-xb-sc").count() >= 3 and "high-exposure strikes" in su, "high-exposure strikes carry verdict/validity context")
        sev = pg.eval_on_selector_all(".vt-xb-al", "e=>e.map(x=>+x.className.match(/s(\\d)/)[1])"); chk(sev == sorted(sev, reverse=True), f"alerts ranked by severity {sev}")
        lv = pg.eval_on_selector_all(".vt-xb-lv .p", "e=>e.map(x=>x.textContent)"); chk(len(lv) >= 3 and all(re.match(r"^NQ \$[\d,]+(\.\d+)?$", x) or re.match(r"^[A-Z/_]+ \$[\d,.]+ / NQ \$[\d,.]+$", x) for x in lv), f"levels {lv}")
        chk(any(" / NQ $" in x for x in lv), "strike / futures format"); chk(pg.locator(".vt-xb-row:not(.off)").count() == 7, "7 books in cross-book"); chk(pg.locator(".vt-xb-read .vt-xb-badge").count() == 2 and "FAMILY" in pg.inner_text(".vt-xb-read").upper(), "scoped read is scope-badged")
        chk("numbers verified" in pg.inner_text(".vt-xb-read"), "validation badge"); chk("MOCK DATA" in pg.inner_text(".vt-xb-read"), "mock disclaimer shown")
        shots.shot(pg, 8, "NQ_family_sidebar_xbook", vp, locator="#vt-sidebar")
        # ---- the alert bar is on EVERY page
        for page in ["vt-page-main", "vt-page-pricing", "vt-page-greeks"]:
            pg.click(f'[data-page="{page}"]'); pg.wait_for_timeout(250); chk(pg.is_visible("#vt-alertbar") and "RIGHT NOW" in pg.inner_text("#vt-alertbar"), f"alert bar visible on {page}")
        pg.click('[data-page="vt-page-main"]'); pg.wait_for_timeout(200); shots.shot(pg, 8, "NQ_family_alertbar_main", vp)
        # ---- single again: the family-only content disappears
        pg.evaluate("window.VT_SCOPE.apply('QQQ','single')"); pg.wait_for_timeout(250); t = pg.inner_text("#vt-sidebar-xb"); chk(pg.locator(".vt-xb-row").count() == 0 and "NQ" not in t, "back to Single-Book: no cross-book / futures content")
        chk(not errs, f"errors {errs}")
    b.close()
print("FAILURES:", F or "none")
