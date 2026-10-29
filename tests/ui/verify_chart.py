import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
# Chart Factors: page, migration off Greeks, epoch gating, strike-field geometry, chart==sections parity, layers, crosshair.
import sys, json; sys.path.insert(0, (VT_ROOT + "/tests/ui"))
from playwright.sync_api import sync_playwright
import shots
LP = json.load(open((VT_ROOT + "/preview/synthetic_lens_payloads.json"))); F = []
def chk(c, m):
    if not c: F.append(m)
def push(pg, key, chart=True, order=("mb", "lens", "chart")):
    d = LP[key]; pg.evaluate("([s])=>window.VT_SCOPE.apply(s.ticker,s.mode==='family'?'family':'multi_book',s.family)", [d["scope"]])
    for o in order:
        if o == "mb": pg.evaluate("(j)=>window.onMultibook(j)", json.dumps(d["multibook"]))
        if o == "lens": pg.evaluate("(j)=>window.onStrikeLens(j)", json.dumps(d["lens"]))
        if o == "chart" and chart: pg.evaluate("(j)=>window.onChartFactors(j)", json.dumps(d["chart"]))
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=__import__("os").environ.get("PW_CHROME", "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"), headless=True, args=["--no-sandbox"])
    for (W, H, vp, dpr) in [(1700, 1100, "desktop", 1), (1700, 1100, "desktop2x", 2)]:
        pg = b.new_context(viewport={"width": W, "height": H}, device_scale_factor=dpr).new_page(); errs = []; pg.on("pageerror", lambda e: errs.append(str(e))); pg.on("console", lambda m: errs.append(m.text) if m.type == "error" and "chart.js" not in m.text and "ERR_" not in m.text and "Failed to load" not in m.text else None); shots.load(pg)
        navs = pg.eval_on_selector_all(".vt-nav-link", "e=>e.map(x=>x.textContent.trim())"); chk("Chart Factors" in navs and navs.index("Chart Factors") == navs.index("greeksChannel") + 1, f"nav {navs}")
        # --- migration: Greeks page hosts none of the three sections
        push(pg, "NQ_family"); pg.click('[data-page="vt-page-greeks"]'); pg.wait_for_timeout(300)
        chk(pg.locator("#vt-page-greeks .vt-ic, #vt-page-greeks .vt-hedge-t, #vt-page-greeks .vt-lens-t, #vt-page-greeks #vt-lens").count() == 0, "Greeks page has no Strike Insights / Strike Lens / Hedge Flow")
        chk(pg.is_visible("#vt-page-greeks .vt-sidebar") and pg.locator("#vt-page-greeks #vt-sidebar").count() == 1, "sidebar present on Greeks page")
        pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(500)
        secs = pg.eval_on_selector_all("#vt-lens-slot-cf .vt-cf-sec", "e=>e.map(x=>x.dataset.sec)"); chk(secs == ["insights", "lens", "hedge"], f"section order {secs}")
        chk(pg.locator("#vt-page-cf #vt-sidebar").count() == 1 and pg.evaluate("(()=>{const s=document.getElementById('vt-sidebar').getBoundingClientRect(),c=document.getElementById('vt-cf').getBoundingClientRect();return s.left>=c.right-2})()"), "sidebar beside the chart, right side")
        chk(pg.evaluate("(()=>{const c=document.getElementById('vt-cf').getBoundingClientRect(),i=document.querySelector('.vt-cf-sec').getBoundingClientRect();return i.top>=c.bottom-2})()"), "sections stacked below the chart")
        chk(pg.evaluate("(()=>{const a=[...document.querySelectorAll('.vt-cf-sec')].map(e=>e.getBoundingClientRect());return a[1].top>=a[0].bottom-2&&a[2].top>=a[1].bottom-2})()"), "three sections stacked vertically")
        # --- chart live + epoch gating
        chk(pg.is_visible("#vt-cf-cv"), "chart canvas visible"); st = pg.evaluate("VT_CF.state"); chk(st["shownEpoch"] == LP["NQ_family"]["chart"]["epoch"], "shown epoch == payload epoch")
        # mismatched chart epoch is held, then shown stale after 250 ms
        d = LP["NQ_family"]; c2 = json.loads(json.dumps(d["chart"])); c2["epoch"] = "deadbeefdeadbeef"; pg.evaluate("(j)=>window.onChartFactors(j)", json.dumps(c2)); pg.wait_for_timeout(100)
        chk(pg.evaluate("VT_CF.state.shownEpoch") != "deadbeefdeadbeef", "mismatched epoch is held back")
        pg.wait_for_timeout(300); chk(pg.evaluate("VT_CF.state.shownEpoch") == "deadbeefdeadbeef" and pg.is_visible("#vt-cf-stale"), "after 250 ms newest is shown with stale badge")
        pg.evaluate("(j)=>window.onChartFactors(j)", json.dumps(d["chart"])); pg.wait_for_timeout(200); chk(not pg.is_visible("#vt-cf-stale"), "stale badge clears when epochs match")
        # --- layers / HUD / read / status
        t = pg.inner_text("#vt-cf"); chk("GAMMA NEGATIVE" in t and "VXN" in t and "CONF" in t, "HUD chips"); chk("amplifying" in t and "Unavailable (not inferred)" in t, "Read + unavailable inputs"); chk("EXAMPLE" in t and "BID" in t and "LIVE" in t, "status bar + example labelling")
        chk(pg.locator("#vt-cf-metrics button").count() >= 4, "metric selector")
        # --- heat rows: strike row == argmax row; same value -> same colour; eRef non-decreasing; future >= past brightness
        res = pg.evaluate("""()=>{const c=VT_CF.state.chart,sp=c.axis.spot,em=Math.abs(c.em.up-sp);const out=[];for(const f of [1,0.6,0.25]){const half=Math.max(1.8*em,sp*0.008)*f;VT_CF.state.view={t0:570,t1:960,p0:sp-half,p1:sp+half};VT_CF.draw();const g=VT_CF.grid();const old=c.field.metrics;
            for(const price of [sp,sp+half*0.33,sp-half*0.51]){c.field.metrics=[{id:'gex',hz:'intraday',scale:1,strikes:[{price,v:1}]}];const v=VT_CF.rowVals(c,g,'gex');let ai=0;for(let i=0;i<v.length;i++)if(v[i]>v[ai])ai=i;out.push(Math.abs(ai-g.rowOf(price)));}c.field.metrics=old;}
            VT_CF.state.view=null;VT_CF.draw();return out;}""")
        chk(all(e <= 0 for e in res), f"peak heat row == strike row: {res}")
        ramp = pg.evaluate("""()=>{const L=VT_CF.LUT.pos,at=t=>{const i=Math.round(t*1023)*3;return [L[i],L[i+1],L[i+2]]};const n0=at(0),n1=at(.10),rb=at(.28),dg=at(.46),ly=at(.60),by=at(.78),rd=at(1),nr=at(.90);
            const sym=JSON.stringify(VT_CF.rgbFor(5e8,1e9))===JSON.stringify(VT_CF.rgbFor(-5e8,1e9));
            let mono=true,prevL=-1;const lum=[];for(let k=0;k<=60;k++){const q=at(k/60);lum.push(.2126*q[0]+.7152*q[1]+.0722*q[2]);}
            let maxJump=0;for(let k=1;k<=1023;k++){const a=at((k-1)/1023),b=at(k/1023);maxJump=Math.max(maxJump,Math.abs(a[0]-b[0])+Math.abs(a[1]-b[1])+Math.abs(a[2]-b[2]));}
            return {navy:Math.abs(n0[0]-11)<3&&Math.abs(n0[1]-19)<3&&Math.abs(n0[2]-43)<3,deep:n1[2]>n1[0]*2&&n1[2]<110,royal:rb[2]>rb[0]*2&&rb[2]>rb[1]*1.5&&rb[2]>150,gold:dg[0]>130&&dg[1]>100&&dg[2]<60&&dg[0]<200,light:ly[0]>230&&ly[1]>210,bright:by[0]>240&&by[1]>200&&by[2]<40,red:Math.abs(rd[0]-255)<3&&Math.abs(rd[1]-30)<3&&Math.abs(rd[2]-30)<3,redReserved:nr[1]>150,sym,smooth:maxJump<14,floor:VT_CF.rgbFor(1e6,1e9)[3]===0}}""")
        chk(all(ramp.values()), f"ramp navy->royal->dark gold->light yellow->bright yellow->red, symmetric, no banding, noise floor invisible: {ramp}")
        er = pg.evaluate("()=>[VT_CF.eref('gex'),VT_CF.eref('vanna'),VT_CF.eref('hedge')]"); chk(all(e > 0 for e in er), f"fixed E_ref per metric {er}")
        er2 = pg.evaluate("()=>{const S=VT_CF.state;const c0=S.hist.length;const a=VT_CF.eref('gex');S.view=null;VT_CF.draw();return [a,VT_CF.eref('gex')]}"); chk(er2[0] == er2[1], "E_ref unchanged by redraw / pan")
        # every metric selectable must render rows (all three models drive the heatmap)
        ms = pg.eval_on_selector_all("#vt-cf-metrics button", "e=>e.map(x=>x.dataset.m)"); chk("struct" in ms and "gex" in ms and "hedge" in ms and len(ms) >= 7, f"metrics {ms}")
        for m in ms:
            pg.click(f'#vt-cf-metrics button[data-m="{m}"]'); pg.wait_for_timeout(120); nz = pg.evaluate("()=>{const c=VT_CF.cur(),g=VT_CF.grid();const v=VT_CF.rowVals(c,g,VT_CF.state.metric);let n=0;for(const x of v)if(Math.abs(x)>0)n++;return n}"); chk(nz > 0, f"metric {m} paints rows")
        pg.click('#vt-cf-metrics button[data-m="struct"]'); pg.wait_for_timeout(100)
        # layout per spec: now line at 82% of the plot, ~10 px row pitch, pills carry asset+strike, chips/outcomes present, price-line head on the now line
        lay = pg.evaluate("()=>{VT_CF.state.view=null;VT_CF.draw();const g=VT_CF.geom(),gr=VT_CF.grid(),S=VT_CF.state,nm=VT_CF.nowMin();return {np:(g.xOf(nm)-g.x0)/(g.x1-g.x0),pitch:g.ph/gr.R,head:S.priceHead?Math.abs(S.priceHead.x-g.xOf(nm)):null,chips:(S.chips||[]).map(c=>c.text),sp:(S.chips||[]).map(c=>c.ly).sort((a,b)=>a-b)}}")
        chk(abs(lay["np"] - 0.82) < 0.005, f"now line at 82%: {lay['np']:.3f}"); chk(7.5 <= lay["pitch"] <= 13.5, f"row pitch {lay['pitch']:.1f}px"); chk(lay["head"] is not None and lay["head"] < 0.5, "price line head on the now line")
        chk(len(lay["chips"]) >= 2 and all(any(w in t for w in ("MAGNET", "PINNING", "REJECTION", "AMPLIFIER", "LIKELY")) for t in lay["chips"]), f"outcome chips {lay['chips'][:3]}"); chk(all(b2 - a >= 18.99 for a, b2 in zip(lay["sp"], lay["sp"][1:])), "chips >= 19 px apart")
        rl = pg.evaluate("VT_CF.labels().map(l=>l.text)"); chk(len(rl) > 0 and all((" \u2013 " in t) or t.startswith("NQ $") for t in rl), f"pills carry asset + strike: {rl[:2]}")
        br = pg.evaluate("""()=>{const S=VT_CF.state;S.set.fade=true;S.mouse=null;S.view=null;VT_CF.draw();const cv=document.getElementById('vt-cf-cv'),x=cv.getContext('2d'),g=VT_CF.geom(),d=window.devicePixelRatio||1,nm=VT_CF.nowMin(),nx=g.xOf(nm);
            const lum=(px0,px1)=>{let t=0,n=0;for(let yy=g.y0+4;yy<g.y0+g.ph-4;yy+=3)for(let xx=px0;xx<px1;xx+=3){const q=x.getImageData(xx*d,yy*d,1,1).data;t+=q[0]+q[1]+q[2];n++;}return t/n};
            return [lum(g.x0+4,g.x0+60),lum(nx+60,Math.min(g.x1-4,nx+140))]}""")
        chk(br[1] >= br[0], f"future region at least as bright as oldest past (past {br[0]:.0f}, future {br[1]:.0f})")
        # --- spec section 15 scenarios that are testable on fixtures
        sc = pg.evaluate("""()=>{const S=VT_CF.state,c=VT_CF.cur();S.set.scale='adaptive';S.sc={};VT_CF.draw();const e=S.sc.struct;const H0=e.H;let T=e.t;
            const m=c.field.metrics.find(x=>x.id==='struct'&&x.hz==='intraday');const sp=c.axis.spot;const keep=m.strikes.slice();m.strikes.push({price:sp+m.scale*0+ (c.em.ok?0:0)+ (c.axis.tick*40),v:m.scale*25,book:'x'});S.texKey='';
            VT_CF.updateScale(T+300);const H1=S.sc.struct.H;VT_CF.updateScale(T+1300);const H2=S.sc.struct.H;
            m.strikes.length=0;keep.forEach(k=>m.strikes.push(k));VT_CF.updateScale(T+1600);VT_CF.updateScale(T+2600);const H3=S.sc.struct.H;
            S.set.lock=true;const hl=S.sc.struct.H;const sv=S.view;S.view=null;VT_CF.updateScale(T+9000);VT_CF.updateScale(T+20000);const hl2=S.sc.struct.H;S.set.lock=false;
            const saved=m.strikes.slice();m.strikes.length=0;S.texKey='';let err=null;try{VT_CF.draw();}catch(x){err=String(x)}const nz=VT_CF.colorOf(0,1,0.05)[3],nan=VT_CF.colorOf(NaN,1,0.05)[3],h0=VT_CF.colorOf(5,0,0)[3];saved.forEach(k=>m.strikes.push(k));S.set.scale='fixed';S.sc={};S.texKey='';
            return {H0,H1,H2,H3,hl,hl2,err,nz,nan,h0}}""")
        chk(sc["H1"] > sc["H0"] * 1.2, f"outlier: scale rises quickly {sc['H0']:.3g}->{sc['H1']:.3g}"); chk(sc["H3"] < sc["H2"], "outlier leaves: scale decays (slower than it rose)"); chk(sc["H2"] - sc["H0"] > sc["H2"] - sc["H3"], "rise is faster than decay")
        chk(sc["hl"] == sc["hl2"], "scale lock freezes H"); chk(sc["err"] is None and sc["nz"] == 0 and sc["nan"] == 0 and sc["h0"] == 0, f"empty / NaN / H=0 data draws without error: {sc['err']}")
        # --- numeric parity: every level price on the chart equals a row in the Strike Lens table (same epoch)
        for _ in range(8):
            if pg.locator("#vt-lens-slot-cf .vt-lens-chip:not(.on):not(.na)").count(): pg.click("#vt-lens-slot-cf .vt-lens-chip:not(.on):not(.na)"); pg.wait_for_timeout(60)
        rows = pg.eval_on_selector_all("#vt-lens-slot-cf .vt-lens-t tbody tr", "r=>r.map(x=>parseFloat(x.dataset.v))"); lv = pg.evaluate("VT_CF.levelsPx().map(l=>l.price)")
        chk(len(rows) > 0 and len(lv) > 0, "rows and levels exist"); tick = LP["NQ_family"]["chart"]["axis"]["tick"]
        # each chart cluster must contain (be within tolerance of) at least one lens row, and each Flip/Wall lens row must map to a chart level
        tol = max(2 * tick, 0.0004 * LP["NQ_family"]["chart"]["axis"]["spot"]) + 1e-9
        chk(all(any(abs(l - r) <= tol for r in rows) for l in lv), "every chart level price matches a Strike Lens row")
        keyrows = pg.eval_on_selector_all("#vt-lens-slot-cf .vt-lens-t tbody tr", "r=>r.filter(x=>/Flip|Wall/.test(x.cells[0].textContent)).map(x=>parseFloat(x.dataset.v))")
        chk(all(any(abs(l - r) <= tol for l in lv) for r in keyrows), "every Flip / Wall row of the lens is on the chart")
        # --- label engine: 19 px spacing on both gutters
        for fn in ("labels", "leftLabels"):
            lb = sorted(pg.evaluate(f"VT_CF.{fn}().map(l=>l.ly)")); chk(all(b2 - a >= 18.99 for a, b2 in zip(lb, lb[1:])), f"{fn} >= 19 px apart")
        # --- crosshair: $/1% from the hedge curve + cross-highlight
        pg.evaluate("window.scrollTo(0,0)"); pg.wait_for_timeout(100); pg.evaluate("document.getElementById('vt-cf-cv').scrollIntoView({block:'start'})"); pg.wait_for_timeout(100); g = pg.evaluate("VT_CF.geom()"); box = pg.eval_on_selector("#vt-cf-cv", "e=>{const r=e.getBoundingClientRect();return [r.left,r.top]}")
        pg.mouse.move(box[0] + g["x0"] + 200, box[1] + g["y0"] + 150); pg.wait_for_timeout(150); tip = pg.inner_text("#vt-cf-tip"); chk("cost to move 1%" in tip and "$" in tip and "nearest strike" in tip, f"crosshair readout: {tip[:60]}")
        lvpx = pg.evaluate("VT_CF.levelsPx()"); L0 = [l for l in lvpx if "Flip" in l["label"] or "Wall" in l["label"]][0]; pg.mouse.move(box[0] + g["x0"] + 300, box[1] + L0["y"]); pg.wait_for_timeout(200)
        chk(pg.locator("#vt-lens-slot-cf .cf-hl").count() >= 1, "hovering a level highlights its row in the sections")
        pg.mouse.move(box[0] + 5, box[1] + 5)
        pg.hover("#vt-lens-slot-cf .vt-lens-t tbody tr:nth-child(3)"); pg.wait_for_timeout(150); chk(pg.evaluate("VT_CF.state.hl") is not None, "hovering a section row highlights it on the chart")
        # --- hedge curve interpolation sanity (monotone cubic stays within data bounds)
        ok = pg.evaluate("""()=>{const h=VT_CF.state.chart.hedge,f=VT_CF.mcInterp(h.prices,h.cost1pct);const lo=Math.min(...h.cost1pct),hi=Math.max(...h.cost1pct);for(let i=0;i<50;i++){const p=h.prices[0]+(h.prices[h.prices.length-1]-h.prices[0])*i/49;const v=f(p);if(v<lo-1e-6||v>hi+1e-6)return false;}for(let i=0;i<h.prices.length;i++)if(Math.abs(f(h.prices[i])-h.cost1pct[i])>1e-6)return false;return true;}""")
        chk(ok, "hedge curve interpolation: exact at nodes, no overshoot")
        # --- tabs drive chart + sections together
        pg.click('.vt-lens-hz button[data-hz="swinging"]'); pg.wait_for_timeout(250); chk(pg.evaluate("VT_LENS.hz") == "swinging" and pg.evaluate("VT_CF.texInfo() && VT_CF.texInfo().id==VT_CF.state.metric"), "Intraday/Swinging tab switches the chart heat too")
        pg.click('.vt-lens-hz button[data-hz="intraday"]'); pg.wait_for_timeout(150)
        # --- single-book: chart explains, sections hidden
        pg.evaluate("window.VT_SCOPE.apply('SPX','single')"); pg.wait_for_timeout(250); chk("Multi-Book / Family" in pg.inner_text("#vt-cf") or pg.evaluate("document.getElementById('vt-lens').hidden"), "single-book handled")
        # --- all active tickers: NQ ES GC CL SI YM render, with asset+strike pills, outcome chips, contrast helper, 19 px chips, no errors
        for key, sym in [("NQ_family", "NQ"), ("ES_multi_SPX", None), ("GC_family", "GC"), ("CL_family", "CL"), ("SI_family", "SI"), ("YM_family", "YM")]:
            push(pg, key); pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(450)
            r = pg.evaluate("""()=>{VT_CF.state.view=null;VT_CF.draw();const S=VT_CF.state,c=VT_CF.cur();const lv=VT_CF.levelsPx();const lum=lv.map(l=>VT_CF.heatLum(l.price));const sp=(S.chips||[]).map(x=>x.ly).sort((a,b)=>a-b);
                let gap=1e9;for(let i=1;i<sp.length;i++)gap=Math.min(gap,sp[i]-sp[i-1]);return {ok:c.ok,sym:c.axis.symbol,n:lv.length,chips:(S.chips||[]).length,gap,lumOk:lum.every(x=>x>=0&&x<=1),pills:VT_CF.labels().map(l=>l.text),metrics:c.field.metrics.length,shown:S.shownEpoch===c.epoch}}""")
            chk(r["ok"] and r["n"] >= 3 and r["chips"] >= 2 and r["gap"] >= 18.99 and r["lumOk"] and r["metrics"] > 0 and r["shown"], f"{key}: {r}")
            if sym: chk(r["sym"] == sym, f"{key} axis symbol {r['sym']}")
            chk(all((" \u2013 " in t) or t.startswith(r["sym"] + " $") for t in r["pills"]), f"{key} pills carry asset+strike: {r['pills'][:2]}")
        # --- performance: steady-state redraw and worst case (texture rebuilt every frame)
        pf = pg.evaluate("""()=>{const S=VT_CF.state;S.view=null;VT_CF.draw();const run=(n,reset)=>{const t0=performance.now();for(let i=0;i<n;i++){if(reset)S.texKey='';VT_CF.draw();}return (performance.now()-t0)/n};return {steady:run(40,false),rebuild:run(15,true)}}""")
        chk(pf["steady"] < 16.0, f"steady redraw {pf['steady']:.1f} ms"); chk(pf["rebuild"] < 50.0, f"worst-case redraw (texture rebuild) {pf['rebuild']:.1f} ms"); print(f"perf {vp}: steady {pf['steady']:.1f} ms, rebuild {pf['rebuild']:.1f} ms")
        # --- other scopes render without errors
        for key in ["ES_multi_SPX", "GC_family", "ES_multi_SPX_degraded"]:
            push(pg, key); pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(350); chk(pg.is_visible("#vt-cf-cv"), f"{key} chart")
        chk(pg.evaluate("document.documentElement.scrollWidth") <= W + 2, "no horizontal scroll")
        push(pg, "NQ_family"); pg.click('[data-page="vt-page-cf"]'); pg.wait_for_timeout(400); shots.shot(pg, 13, "chart_factors_top", vp)
        pg.evaluate("window.scrollTo(0, document.body.scrollHeight)"); pg.wait_for_timeout(200); shots.shot(pg, 13, "chart_factors_sections", vp)
        chk(not errs, f"page errors {errs[:3]}")
    b.close()
print("FAILURES:", "; ".join(F) if F else "none")
