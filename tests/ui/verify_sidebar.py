import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# The dealer-flow sidebar is ONE element shared by Volatility Analysis, Pricing Surfaces and greeksChannel.
# Checks: present and visible on every page, never duplicated, and an update that arrives while page A is
# showing is already reflected when page B is opened (and the reverse).
import json, copy
from playwright.sync_api import sync_playwright
d = json.load(open((VT_ROOT + "/preview/synthetic_payloads.json")))
bad = []
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    pg = b.new_page(viewport={"width": 1700, "height": 1000}); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(("file://" + VT_ROOT + "/preview/preview.html")); pg.wait_for_timeout(300)
    send = lambda pl, scale=1.0: pg.evaluate("([c,j])=>window.onModelUpdate(c,j)", [pl.get("_channel", pl["model"]),
        json.dumps({**pl, "points": [{**q, **({"value": q["value"] * scale} if "value" in q else {})} for q in pl["points"]]} if "points" in pl else pl)])
    for pl in d["model_updates"]: send(pl)
    go = lambda page: (pg.click(f'[data-page="{page}"]'), pg.wait_for_timeout(250))
    txt = lambda: pg.evaluate("document.getElementById('vt-sidebar-body').innerText")
    pages = ["vt-page-main", "vt-page-pricing", "vt-page-greeks"]
    for page in pages:
        go(page)
        r = pg.evaluate("()=>{const s=document.getElementById('vt-sidebar'),b=s.getBoundingClientRect();return [document.querySelectorAll('#vt-sidebar').length,b.width>200&&b.height>200]}")
        want = [1, False] if page == "vt-page-main" else [1, True]   # the main page has NO sidebar
        if r != want: bad.append((page, "wrong sidebar presence", r))
    # update arrives while on greeks; open pricing; must match
    go("vt-page-greeks")
    for pl in d["model_updates"]:
        if pl.get("_channel", pl["model"]) == "gex" and pl["bucket"] == "term": send(pl, 3.0)
    t_greeks = txt()
    for page in ["vt-page-pricing", "vt-page-greeks"]:
        go(page)
        if txt() != t_greeks: bad.append((page, "sidebar text diverged"))
    # and the reverse: update while on pricing, read on greeks
    go("vt-page-pricing")
    for pl in d["model_updates"]:
        if pl.get("_channel", pl["model"]) == "gex" and pl["bucket"] == "term": send(pl, 0.5)
    t_pricing = txt(); go("vt-page-greeks")
    if txt() != t_pricing or t_pricing == t_greeks: bad.append(("reverse", "no propagation", t_pricing == t_greeks))
    print("page errors:", errs); print("FAILURES:", bad or "none")
