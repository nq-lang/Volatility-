import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# T2-3 acceptance: selector, popup (modal/sheet), keys, remember, dismiss, badge, toggle, family no-popup. Captures phase-1 shots (MOCK).
import sys, json
sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
F = []
def chk(c, m):
    if not c: F.append(m)
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
        ctx = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr); pg = ctx.new_page(); shots.load(pg)
        badge = lambda: pg.inner_text("#vt-scope-badge")
        chk(badge() == "SINGLE-BOOK", "initial badge")
        tk = pg.eval_on_selector_all(".vt-ticker", "e=>e.map(x=>x.dataset.ticker)")
        chk(all(t in tk for t in ["NQ", "ES", "CL", "SI", "YM", "QQQ", "NDX", "OG", "GLD", "DIA", "DJX", "LO", "B_LCO", "SPX", "SPY"]), "tickers present")
        chk("GC" in tk, "GC selectable (user re-enabled)")
        rows = pg.eval_on_selector_all(".vt-trow", "r=>r.map(x=>[x.dataset.row,x.getBoundingClientRect().top,[...x.querySelectorAll('.vt-ticker')].map(b=>b.dataset.ticker)])")
        chk(rows[0][0]=="indx" and rows[1][0]=="fut" and rows[0][1] < rows[1][1], "INDX row above =F row")
        chk("QQQ" in rows[0][2] and "NVDA" in rows[0][2] and "NQ" not in rows[0][2] and rows[1][2][:3]==["NQ","ES","GC"], "row contents: %s" % rows)
        shots.shot(pg, 1, "ALL_selector_main", vp, locator=".vt-header")
        pg.click('.vt-ticker[data-ticker="QQQ"]'); chk(pg.is_visible(".vt-pop"), "popup on standard ticker")
        chk(pg.inner_text("#vt-scope-badge") == "SINGLE-BOOK", "badge unchanged while popup open")
        shots.shot(pg, 1, "QQQ_popup_main", vp)
        pg.keyboard.press("Escape"); chk(not pg.is_visible(".vt-pop"), "Esc closes"); chk(badge() == "SINGLE-BOOK" and pg.evaluate("window.__activeTicker()") == "SPX", "dismiss leaves view unchanged")
        pg.click('.vt-ticker[data-ticker="QQQ"]'); pg.keyboard.press("2")
        chk(badge().startswith("MULTI-BOOK · NQ · 7 books"), "multi-book badge: " + badge()); chk(pg.evaluate("window.__activeTicker()") == "QQQ", "ticker changed")
        shots.shot(pg, 1, "QQQ_multibook_main", vp)
        pg.click('#vt-scope-tabs [data-m="single"]'); chk(badge() == "SINGLE-BOOK", "tab to single-book")
        pg.click('#vt-scope-tabs [data-m="multi"]'); chk(badge().startswith("MULTI-BOOK"), "tab back to multi")
        pg.click('.vt-ticker[data-ticker="NQ"]'); chk(not pg.is_visible(".vt-pop"), "futures: no popup"); chk(badge().startswith("FAMILY · NQ · 7 books"), "family badge: " + badge())
        shots.shot(pg, 1, "NQ_family_main", vp)
        # futures: Single-Book tab opens priority-ranked book list
        pg.click('#vt-scope-tabs [data-m="single"]'); chk(pg.is_visible(".vt-pick"), "book picker opens")
        names = pg.eval_on_selector_all(".vt-pick-row .vt-pick-name", "e=>e.map(x=>x.textContent)"); chk(names == ["QQQ","NDX","NVDA","AAPL","MSFT","TQQQ/SQQQ","VIX" if False else "VX"], "priority order: %s" % names)
        shots.shot(pg, 1, "NQ_singlebook_picker_main", vp)
        pg.keyboard.press("Escape"); chk(badge().startswith("FAMILY"), "picker dismiss leaves family")
        pg.click('#vt-scope-tabs [data-m="single"]'); pg.keyboard.press("3")
        chk(badge() == "SINGLE-BOOK · NVDA (NQ)" and pg.evaluate("window.__activeTicker()") == "NVDA", "picked NVDA: " + badge())
        chk(pg.eval_on_selector('.vt-ticker[data-ticker="NQ"]', "e=>e.classList.contains('active')"), "anchor stays highlighted")
        shots.shot(pg, 1, "NQ_singlebook_NVDA_main", vp)
        pg.click('#vt-scope-tabs [data-m="multi"]'); chk(badge().startswith("FAMILY · NQ"), "multi returns to family")
        pg.click('.vt-ticker[data-ticker="YM"]'); pg.click('#vt-scope-tabs [data-m="single"]')
        chk(pg.eval_on_selector('.vt-pick-row[data-id="OZA"]', "e=>e.disabled"), "unresolved OZA disabled"); pg.keyboard.press("Escape")
        pg.click('.vt-ticker[data-ticker="DIA"]'); pg.check("#vt-pop-remember"); pg.keyboard.press("1")
        pg.click('.vt-ticker[data-ticker="ES"]'); pg.click('.vt-ticker[data-ticker="DIA"]'); chk(not pg.is_visible(".vt-pop") and badge() == "SINGLE-BOOK", "remembered choice skips popup")
        pg.evaluate("window.VT_SCOPE.select('MSFT', true)") ; chk(pg.query_selector(".vt-pop select") is not None, "family chooser for multi-family ticker")
        pg.select_option(".vt-pop select", "ES"); pg.keyboard.press("2"); chk(badge().startswith("MULTI-BOOK · ES"), "family chooser applied: " + badge())
        box = None
        pg.click('.vt-ticker[data-ticker="DJX"]'); box = pg.eval_on_selector(".vt-pop-opt", "e=>e.getBoundingClientRect().height"); chk(box >= 44, "tap target >=44px"); 
        chk(pg.evaluate("document.documentElement.scrollWidth") <= W + 1, "no horizontal page scroll")
        pg.keyboard.press("Escape")
    b.close()
print("FAILURES:", F if F else "none")
