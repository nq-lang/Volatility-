import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Volatility section: VIX/VXN tickers in their own "Volatility" row below futures; analysis card; unavailable states; no family/book behaviour.
import sys, json; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
VP = json.load(open((VT_ROOT + "/preview/synthetic_vol_payload.json"))); F = []
def chk(c, m):
    if not c: F.append(m)
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); shots.load(pg)
        rows = pg.eval_on_selector_all("#vt-tickers .vt-trow", "e=>e.map(r=>[r.dataset.row,r.querySelector('.vt-rlab').textContent,[...r.querySelectorAll('.vt-ticker')].map(x=>x.dataset.ticker)])")
        chk([r[0] for r in rows] == ["indx", "fut", "vol"], f"row order INDX, futures, Volatility: {rows}"); vr = rows[-1]; chk(vr[1] == "Volatility" and vr[2] == ["VIX", "VXN"], f"Volatility row {vr}")
        chk("VIX" not in rows[1][2] and "VXN" not in rows[0][2], "vol tickers not in futures / INDX rows")
        # before any feed: selecting VIX shows an explicit unavailable state
        pg.click('.vt-ticker[data-ticker="VIX"]'); pg.wait_for_timeout(250)
        chk(pg.is_visible("#vt-vol") and "Unavailable" in pg.inner_text("#vt-vol"), "no feed -> unavailable state")
        chk("VOLATILITY" in pg.inner_text("#vt-scope-badge") and not pg.is_visible("#vt-scope-tabs"), "badge says VOLATILITY, no Single/Multi tabs")
        pg.evaluate("(j)=>window.onVolatility(j)", json.dumps(VP)); pg.wait_for_timeout(200)
        t = pg.inner_text("#vt-vol"); chk("Volatility" in t and pg.eval_on_selector("#vt-vol .h em", "e=>e.textContent") == "VIX", "titled Volatility · VIX")
        chk(pg.locator(".vt-vol-row .c").count() == 8 and pg.locator(".vt-vol-concl").count() == 1, "8 compact chips + conclusion"); chk(pg.evaluate("(()=>{const v=document.getElementById('vt-vol').getBoundingClientRect(),m=[...document.querySelectorAll('.vt-page.active')][0].getBoundingClientRect();return v.top>=m.bottom-1})()"), "vol card sits below all page content"); chk(pg.evaluate("document.getElementById('vt-vol').offsetHeight") < (190 if W > 700 else 330), "compact")
        chk("percentile" in t and "stress" in t.lower() and "backwardation" in t, "regime + term reading derived from payload")
        chk("IV−RV" in t.replace("\u2212","−") or "n/a" in t, "IV-RV honestly unavailable")
        chk("as of" in t and "conf" in t, "as-of and confidence shown"); chk("ES" in t and "SPX" in t, "linked books listed")
        shots.shot(pg, 10, "VIX_volatility_section", vp)
        pg.click('.vt-ticker[data-ticker="VXN"]'); pg.wait_for_timeout(250); t = pg.inner_text("#vt-vol")
        chk(pg.eval_on_selector("#vt-vol .h em", "e=>e.textContent") == "VXN" and "term structure unavailable" in t, "VXN: term structure unavailable (no data), not guessed")
        chk(("QQQ" in t or "NQ" in t), "divergence + linked Nasdaq books")
        for page in ["vt-page-pricing", "vt-page-greeks", "vt-page-main"]:
            pg.click(f'[data-page="{page}"]'); pg.wait_for_timeout(150); chk(pg.is_visible("#vt-vol"), f"vol card visible on {page}")
        pg.evaluate("window.VT_SCOPE.apply('SPX','single')"); pg.wait_for_timeout(250); chk(not pg.is_visible("#vt-vol"), "card hidden for other tickers")
        chk(pg.evaluate("VT_VOL.indexFor('QQQ')") == "VXN" and pg.evaluate("VT_VOL.indexFor('ES')") == "VIX" and pg.evaluate("VT_VOL.indexFor('NQ')") == "VXN" and pg.evaluate("VT_VOL.indexFor('GC')") is None, "book -> vol index mapping from registry")
        chk(pg.evaluate("document.documentElement.scrollWidth") <= W + 1, f"no horizontal scroll {vp}"); chk(not errs, f"errors {errs}")
    b.close()
print("FAILURES:", F or "none")
