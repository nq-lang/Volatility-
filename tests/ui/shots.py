import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Real screenshots of the running app (headless Chromium against preview.html built from production web/).
# MOCK DATA watermark on every capture (all fixtures here are synthetic). Usage: shots.py <phase> [extra-script.js-free flags]
import json, sys, os, subprocess, hashlib, time
from playwright.sync_api import sync_playwright
ROOT = (VT_ROOT)
def head():
    try: return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, stderr=subprocess.DEVNULL).decode().strip()
    except Exception: return "no-git"
WM = "document.getElementById('wm')||document.body.insertAdjacentHTML('beforeend','<div id=wm style=\"position:fixed;top:6px;left:50%;transform:translateX(-50%);z-index:99999;color:#ff4d4d;font:700 13px monospace;letter-spacing:3px;background:rgba(0,0,0,.75);padding:2px 10px;border:1px solid #ff4d4d;pointer-events:none\">MOCK DATA</div>')"
def load(pg, extra=None):
    d = json.load(open(f"{ROOT}/preview/synthetic_payloads.json")); r = json.load(open(f"{ROOT}/preview/synthetic_regime_payloads.json")); v = json.load(open(f"{ROOT}/preview/synthetic_viz_payloads.json"))
    pg.goto(f"file://{ROOT}/preview/preview.html"); pg.wait_for_timeout(300)
    for pl in d["model_updates"]: pg.evaluate("([c,j])=>window.onModelUpdate(c,j)", [pl.get("_channel", pl["model"]), json.dumps(pl)])
    pg.evaluate("(j)=>window.onInsightUpdate(j)", json.dumps(d["insight"]))
    pg.evaluate("(j)=>window.onRegimeStatic(j)", json.dumps(r["static"])); pg.evaluate("(j)=>window.onRegimeLive(j)", json.dumps(r["live"]))
    pg.evaluate("(j)=>window.onRegimeViz(j)", json.dumps(v["static"]))
    for l in v["live"]: pg.evaluate("(j)=>window.onRegimeVizLive(j)", json.dumps(l["viz"]))
    if extra: extra(pg)
    pg.wait_for_timeout(500)
def shot(pg, phase, name, viewport, source="MOCK", fixture="synthetic-v1", locator=None):
    out = f"{ROOT}/docs/screenshots/phase-{phase}"; os.makedirs(out, exist_ok=True)
    f = f"{name}_{viewport}.png"; pg.evaluate(WM)
    (pg.locator(locator) if locator else pg).screenshot(path=f"{out}/{f}", **({} if locator else {"full_page": True}))
    mp = f"{ROOT}/docs/screenshots/manifest.json"
    m = json.load(open(mp)) if os.path.exists(mp) else []
    m = [e for e in m if e["file"] != f"phase-{phase}/{f}"]
    m.append({"file": f"phase-{phase}/{f}", "commit": head(), "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "data_source": source, "fixture_id": fixture})
    json.dump(m, open(mp, "w"), indent=1); pg.evaluate("document.getElementById('wm')?.remove()")
if __name__ == "__main__":
    phase = sys.argv[1] if len(sys.argv) > 1 else "0"
    with sync_playwright() as p:
        b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
        for (W, H, vp, dpr) in [(1700, 1000, "desktop", 1), (390, 844, "mobile", 2)]:
            ctx = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr); pg = ctx.new_page(); load(pg)
            for page, nm in [("vt-page-main", "SPX_single_main"), ("vt-page-pricing", "SPX_single_pricing"), ("vt-page-greeks", "SPX_single_greeks")]:
                pg.click(f'[data-page="{page}"]'); pg.wait_for_timeout(500); shot(pg, phase, nm, vp)
