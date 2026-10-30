# Renders the Chart Factors heatmap replay (NQ mock history) frame by frame into a GIF. Usage: python3 tests/ui/make_heatmap_gif.py out.gif [metric]
import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
import sys, json, io; sys.path.insert(0, VT_ROOT + "/tests/ui")
from playwright.sync_api import sync_playwright
from PIL import Image
import shots
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/heatmap_replay.gif"; metric = sys.argv[2] if len(sys.argv) > 2 else "struct"
LP = json.load(open(VT_ROOT + "/preview/synthetic_lens_payloads.json")); d = LP["NQ_family"]; frames = []
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=_os.environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    pg = b.new_context(viewport={"width": 1700, "height": 1100}).new_page(); shots.load(pg)
    pg.evaluate("([s])=>window.VT_SCOPE.apply(s.ticker,'family',s.family)", [d["scope"]])
    for h in d["history"]: pg.evaluate("(j)=>window.onChartFactors(j)", json.dumps(h) if not isinstance(h, str) else h)
    for k, f in (("multibook", "onMultibook"), ("lens", "onStrikeLens"), ("chart", "onChartFactors")): pg.evaluate(f"(j)=>window.{f}(j)", json.dumps(d[k]))
    pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(600)
    pg.click(f'#vt-cf-metrics button[data-m="{metric}"]'); pg.evaluate("document.getElementById('vt-cf-cv').scrollIntoView({block:'start'})"); pg.evaluate("(()=>{const m=document.querySelector('.vt-main');if(m)m.scrollTop-=190;window.scrollBy(0,-190)})()"); pg.wait_for_timeout(250)
    t0, t1 = pg.evaluate("[VT_CF.state.hist[0].t, VT_CF.state.hist[VT_CF.state.hist.length-1].t]"); el = pg.query_selector(".vt-cf-cw")
    pos = t0 + 0.5
    while pos < t1:
        pg.evaluate("(p)=>{const S=VT_CF.state;S.pos=p;S.play=false;VT_CF.stepView(0.07);VT_CF.draw();}", pos)
        frames.append(Image.open(io.BytesIO(el.screenshot())).convert("RGB")); pos += 0.45
    for _ in range(8):
        pg.evaluate("()=>{const S=VT_CF.state;S.pos=null;VT_CF.stepView(0.07);VT_CF.draw();}"); frames.append(Image.open(io.BytesIO(el.screenshot())).convert("RGB"))
    b.close()
W = 900; fr = [f.resize((W, int(f.height * W / f.width)), Image.LANCZOS) for f in frames]
pal = fr[len(fr) // 2].quantize(colors=128, method=Image.MEDIANCUT)
q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in fr]
q[0].save(out, save_all=True, append_images=q[1:], duration=80, loop=0, optimize=True, disposal=1)
print(out, len(q), "frames", _os.path.getsize(out) / 1e6, "MB")
