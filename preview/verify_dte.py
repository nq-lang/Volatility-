import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
import json
from playwright.sync_api import sync_playwright
CHROME = "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"
data = json.load(open((VT_ROOT + "/preview/synthetic_payloads.json")))
CH = ["gex","dex","vanna","charm","speed","color","theta","vrp","rnd","call_delta_iv","put_delta_iv"]
EXPECT = {"0DTE": {0}, "1DTE": {1}, "2DTE": {2}}
payload = {(pl.get("_channel", pl["model"]), pl["bucket"]): pl for pl in data["model_updates"] if "bucket" in pl}
fails, n = [], 0
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=CHROME, headless=True, args=["--no-sandbox"])
    page = b.new_page(viewport={"width":1700,"height":1300}); errs=[]
    page.on("pageerror", lambda e: errs.append(str(e)))
    page.goto(("file://" + VT_ROOT + "/preview/preview.html")); page.wait_for_timeout(300)
    for pl in data["model_updates"]:
        page.evaluate("([c,j]) => window.onModelUpdate(c,j)", [pl.get("_channel", pl["model"]), json.dumps(pl)])
    for ch in CH:
        for tab in ["0DTE","1DTE","2DTE","term"]:
            r = page.evaluate("""([ch, tab]) => {
                const pan = document.querySelector(`.vt-panel[data-channel='${ch}']`);
                pan.querySelector(`[data-dte='${tab}']`).click();
                const sp = window.__panels[ch].panel, g = sp._grid;
                return { dtes:[...new Set(sp.points.map(p=>p.dte))].sort((a,b)=>a-b), single:g.single, meshRows:g.ys.length,
                         xs:g.xs, Z:g.Z, minis:pan.querySelectorAll('.vt-mini-chart').length,
                         label:pan.querySelector('.vt-mini-chart-label')?.textContent };
            }""", [ch, tab])
            n += 1
            pl = payload[(ch, tab)]
            exp = {}
            for pt in pl["points"]:
                exp[(pt.get("strike", pt.get("x")), pt["dte"])] = pt.get("value", pt.get("y"))
            # 1) only the selected DTE (or the full set for term)
            want = EXPECT.get(tab) or {d for (_, d) in exp}
            if set(r["dtes"]) != want: fails.append((ch, tab, "dte set", r["dtes"]))
            # 2) 3D grid values == payload values for that bucket (and therefore == 2D chart values, same grid)
            rows = sorted(want)
            for j, d in enumerate(rows if not r["single"] else [rows[0]]):
                for i, x in enumerate(r["xs"]):
                    if abs(r["Z"][j][i] - exp[(x, d)]) > 1e-9 * max(1, abs(exp[(x, d)])): fails.append((ch, tab, "value", d, x)); break
            # 3) single DTE: exactly one cross-section, labelled with its DTE, and a real multi-row 3D mesh
            if tab != "term":
                if r["minis"] != 1 or r["label"] != f"{rows[0]} DTE cross-section": fails.append((ch, tab, "label", r["minis"], r["label"]))
                if not r["single"] or r["meshRows"] < 3: fails.append((ch, tab, "mesh rows", r["meshRows"]))
            else:
                if r["single"] or r["minis"] < 2: fails.append((ch, tab, "term not multi", r["minis"]))
    # 4) empty-state: a tab with no data must not show another tab's data
    page2 = b.new_page(viewport={"width":1700,"height":1300}); page2.goto(("file://" + VT_ROOT + "/preview/preview.html")); page2.wait_for_timeout(300)
    only_term = payload[("gex","term")]
    page2.evaluate("([c,j]) => window.onModelUpdate(c,j)", ["gex", json.dumps(only_term)])
    res = page2.evaluate("""() => { const pan=document.querySelector(".vt-panel[data-channel='gex']"); pan.querySelector("[data-dte='1DTE']").click();
        const sp=window.__panels.gex.panel; return { points: sp.points, awaiting: pan.querySelector('.vt-split-right').textContent }; }""")
    if res["points"] is not None or "awaiting 1DTE" not in res["awaiting"]: fails.append(("gex","1DTE","stale data not cleared", res["awaiting"][:60]))
    print(f"checked {n} panel/tab combinations (11 panels x 4 tabs), page errors: {errs}")
    print("FAILURES:", fails if fails else "none")
    b.close()
