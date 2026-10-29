import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Verifies bubble geometry: round by default (3 equal radii, same VOLUME as the fitted
# ellipsoid), 'Fitted Shape' restores the true covariance axes, and the rendered bubble
# actually projects as a circle (equal horizontal/vertical extent) rather than a flat disc.
import json, math
from playwright.sync_api import sync_playwright
CHROME = "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"
regime = json.load(open((VT_ROOT + "/preview/synthetic_regime_payloads.json")))
bad = []
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=CHROME, headless=True, args=["--no-sandbox"])
    page = b.new_page(viewport={"width":1700,"height":1100})
    errs = []; page.on("pageerror", lambda e: errs.append(str(e)))
    page.goto(("file://" + VT_ROOT + "/preview/preview.html")); page.wait_for_timeout(300)
    page.evaluate("(j)=>window.onRegimeStatic(j)", json.dumps(regime["static"]))
    page.evaluate("(j)=>window.onRegimeLive(j)", json.dumps(regime["live"])); page.wait_for_timeout(200)
    res = page.evaluate("""() => { const P = window.__pca; const out = [];
        for (const e of P.staticData.ellipsoids) {
            const fitted = e.axes.map(a => a[3]);
            P.fittedShape = false; const round = P._bubbleAxes(e).map(a => a[3]);
            P.fittedShape = true;  const back  = P._bubbleAxes(e).map(a => a[3]);
            out.push({ name: e.name, fitted, round, back });
        } P.fittedShape = false; return out; }""")
    print(f"{'regime':32s} fitted radii (long/mid/short)   flatness(short/long)   round radius")
    for r in res:
        f = sorted(r["fitted"], reverse=True); rr = r["round"]
        vol_fit = math.prod(r["fitted"]); vol_round = math.prod(rr)
        print(f"{r['name']:32s} {f[0]:.2f} / {f[1]:.2f} / {f[2]:.2f}        {f[2]/f[0]:.2f}               {rr[0]:.2f}")
        if max(rr) - min(rr) > 1e-12: bad.append((r["name"], "round radii not equal", rr))
        if abs(vol_fit - vol_round) > 1e-9 * vol_fit: bad.append((r["name"], "volume changed", vol_fit, vol_round))
        if r["back"] != r["fitted"]: bad.append((r["name"], "fitted shape not restored"))
    # rendered silhouette: project a bubble's extreme points through the real camera and compare extents
    ext = page.evaluate("""() => { const P = window.__pca; const e = P.staticData.ellipsoids[0];
        const w = P.canvas.clientWidth, h = P.canvas.clientHeight; const out = {};
        for (const mode of [false, true]) { P.fittedShape = mode;
            let xs = [], ys = [];
            for (let i = 0; i < 400; i++) { const u = Math.acos(1 - 2 * (i + 0.5) / 400), v = Math.PI * (1 + Math.sqrt(5)) * i;
                const l = [Math.sin(u) * Math.cos(v), Math.cos(u), Math.sin(u) * Math.sin(v)]; let q = [...e.center];
                P._bubbleAxes(e).forEach((a, k) => { q = [q[0] + a[0] * l[k] * a[3], q[1] + a[1] * l[k] * a[3], q[2] + a[2] * l[k] * a[3]]; });
                const s = P.cam.project(q[0], q[1], q[2], w, h); xs.push(s.sx); ys.push(s.sy); }
            out[mode ? 'fitted' : 'round'] = (Math.max(...xs) - Math.min(...xs)) / (Math.max(...ys) - Math.min(...ys)); }
        P.fittedShape = false; return out; }""")
    print(f"\nrendered width/height ratio of the first bubble: round = {ext['round']:.3f} (1.000 = perfect circle)   fitted = {ext['fitted']:.3f}")
    if abs(ext["round"] - 1) > 0.06: bad.append(("round silhouette not circular", ext["round"]))
    print("page errors:", errs); print("FAILURES:", bad if bad else "none"); b.close()
