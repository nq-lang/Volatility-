import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Task 1 UI acceptance: tabs, morph time, orthographic flat view, rotation, label overlap at 390px, model integrity (JS side).
import json, sys
from playwright.sync_api import sync_playwright
ROOT = (VT_ROOT)
V = json.load(open(f"{ROOT}/preview/synthetic_viz_payloads.json"))
R = json.load(open(f"{ROOT}/preview/synthetic_regime_payloads.json"))
bad = []
def feed(pg):
    pg.evaluate("(j)=>window.onRegimeStatic(j)", json.dumps(R["static"])); pg.evaluate("(j)=>window.onRegimeLive(j)", json.dumps(R["live"]))
    pg.evaluate("(j)=>window.onRegimeViz(j)", json.dumps(V["static"]))
    for l in V["live"]:
        pg.evaluate("(j)=>window.onRegimeVizLive(j)", json.dumps(l["viz"])); 
    pg.wait_for_timeout(300)
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H) in [(1700, 1000), (390, 844)]:
        pg = b.new_page(viewport={"width": W, "height": H}); errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)))
        pg.goto(f"file://{ROOT}/preview/preview.html"); pg.wait_for_timeout(300)
        feed(pg)
        before = pg.evaluate("JSON.stringify([window.__pca.staticData, window.__pca.liveData])")
        for mode in ["pca2d", "tsne2d", "tsne3d", "pca3d", "pca2d"]:
            t = pg.evaluate("""(m) => new Promise(res => { const t0 = performance.now(); document.querySelector(`.vt-view-tab[data-m=${m}]`).click();
                const chk = () => { const v = window.__pca.views; if (!v.morph) res(performance.now() - t0); else requestAnimationFrame(chk); }; requestAnimationFrame(chk); })""", mode)
            if t > 650: bad.append((W, mode, "morph too slow", t))
            pg.wait_for_timeout(120)
            info = pg.evaluate("""() => { const v = window.__pca.views; return { active: v.active, mode: v.mode, persp: v.cam.persp, yaw: v.cam.yaw, pitch: v.cam.pitch,
                  boxes: v._labelBoxes || [], badge: v.badge.style.display, foot: v.foot.innerText } }""")
            if mode != "pca3d":
                if not info["active"]: bad.append((W, mode, "view inactive"))
                if "3d" not in mode and (abs(info["persp"]) > 1e-6 or abs(info["yaw"]) > 1e-6 or abs(info["pitch"]) > 1e-6): bad.append((W, mode, "flat view is not orthographic/top-down", info["persp"], info["yaw"], info["pitch"]))
                bx = info["boxes"]
                for i in range(len(bx)):
                    for j in range(i + 1, len(bx)):
                        a, c = bx[i], bx[j]
                        if a["x"] < c["x"] + c["w"] and a["x"] + a["w"] > c["x"] and a["y"] < c["y"] + c["h"] and a["y"] + a["h"] > c["y"]: bad.append((W, mode, "label overlap", i, j))
                if mode.startswith("tsne") and (info["badge"] != "block" or "Visual aid only" not in pg.evaluate("window.__pca.views.badge.textContent")): bad.append((W, mode, "missing honesty badge"))
                if mode.startswith("tsne") and "not meaningful" not in info["foot"]: bad.append((W, mode, "missing caveat line"))
            else:
                if info["active"]: bad.append((W, "pca3d", "new view still active"))
            if W == 1700 and mode in ("pca2d", "tsne2d", "tsne3d"):
                pg.evaluate("document.body.insertAdjacentHTML('beforeend','<div id=wm style=\"position:fixed;top:8px;left:50%;transform:translateX(-50%);z-index:99;color:#ff4d4d;font:700 14px monospace;letter-spacing:3px;background:rgba(0,0,0,.7);padding:3px 10px;border:1px solid #ff4d4d\">MOCK DATA</div>')")
                pg.locator("#vt-pca-biplot-wrap").screenshot(path=f"/mnt/user-data/outputs/viz_{mode}_{W}.png"); pg.evaluate("document.getElementById('wm')?.remove()")
        # in-plane rotation + tilt + flatten on PCA 2D
        pg.evaluate("document.querySelector('.vt-view-tab[data-m=pca2d]').click()"); pg.wait_for_timeout(700)
        r = pg.evaluate("""() => { const v = window.__pca.views; v.cam.roll = 0.6; v.draw(); const a = v.cam.project(1,0,0,800,600); v.cam.roll = 0; const b = v.cam.project(1,0,0,800,600);
            v.cam.yaw = 0.5; v._updatePersp(); const tilted = v.cam.persp; v.flatten(); return { rotated: Math.hypot(a.sx-b.sx, a.sy-b.sy) > 5, tilted } }""")
        pg.wait_for_timeout(800)
        fl = pg.evaluate("({y: window.__pca.views.cam.yaw, p: window.__pca.views.cam.pitch, ps: window.__pca.views.cam.persp})")
        if not r["rotated"] or r["tilted"] <= 0: bad.append((W, "rotate/tilt didn't change the view", r))
        if abs(fl["y"]) > 1e-3 or abs(fl["p"]) > 1e-3 or fl["ps"] > 1e-3: bad.append((W, "flatten did not return to top-down", fl))
        # selection card + decomposition
        pg.evaluate("(()=>{const v=window.__pca.views; v.sel={type:'pt', i:5}; v.decomp=1; v._renderCard(); v.draw();})()")
        if "change vs previous day" not in pg.evaluate("window.__pca.views.card.textContent").lower() or "pushing" not in pg.evaluate("window.__pca.views.card.textContent").lower(): bad.append((W, "card missing decomposition/displacement"))
        # exact decomposition: sum_j z_j * loading_jk == pca score
        ex = pg.evaluate("""() => { const v = window.__pca.views, V = v.viz; let worst = 0;
            for (const p of V.points) for (let k = 0; k < 3; k++) { let s = 0; V.feature_names.forEach((_, f) => s += p.z[f] * V.pca_meta.loadings[f][k]); worst = Math.max(worst, Math.abs(s - p.pca[k])); } return worst; }""")
        # payload numbers are serialized to ~6 significant digits; the identity itself is exact
        if ex > 1e-4: bad.append((W, "decomposition not exact", ex))
        # model integrity (JS side): views never mutate the static/live model payloads
        after = pg.evaluate("JSON.stringify([window.__pca.staticData, window.__pca.liveData])")
        if before != after: bad.append((W, "model payload mutated by the views"))
        # spotlight + replay
        pg.evaluate("(()=>{const v=window.__pca.views; v.spot=3; v.draw(); v.tpos=2; v._syncScrub(); v.draw();})()")
        if W == 390:
            pg.evaluate("document.querySelector('.vt-view-tab[data-m=tsne2d]').click()"); pg.wait_for_timeout(700)
            pg.locator("#vt-pca-biplot-wrap").screenshot(path="/mnt/user-data/outputs/viz_tsne2d_390.png")
        print(W, "page errors:", errs)
        if errs: bad.append((W, "page errors", errs))
print("FAILURES:", bad or "none")
