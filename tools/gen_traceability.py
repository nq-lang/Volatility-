import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
#!/usr/bin/env python3
"""Emit docs/chart-factors-traceability.md + .json (CHART_FACTORS_TASK.md §3.3).
One row per output field of Strike Insights, Strike Lens, Hedge Flow and the right sidebar:
  section | field | source model | unit | chart element | label format | tooltip
Chart elements: HUD (top strip) / READ (narrative) / LEVEL (line+chips) / BAND (strike gradient field) / MARKER / ZONE / CROSSHAIR / STATUS / SUBPANEL / SIDEBAR-ONLY(mirrored).
GAP = the chart element exists in the spec but today's feeds cannot supply the data (see audit doc section 5); the field is still mapped, with the stated fallback.
"""
import json
R = []
def row(sec, field, src, unit, elem, label, tip, gap=""):
    R.append(dict(section=sec, field=field, source=src, unit=unit, element=elem, label=label, tooltip=tip, gap=gap))
SL, SI, HF, SB = "Strike Lens", "Strike Insights", "Hedge Flow", "Sidebar"
# ---------------- Strike Lens (strike_lens.h lens_json + multibook_json) ----------------
for f, u, e, l, t in [
 ("books[].id", "id", "LEVEL (contributingBooks tag)", "book symbol in chip tag / left chip colour", "Which book contributes this strike/level"),
 ("books[].convertible", "bool", "LEVEL (dimmed + note if false)", "n/a chip", "Book has no 1:1 price mapping to the axis instrument"),
 ("books[].spot", "price (book space)", "STATUS", "bid/ask strip: book spot", "Book's own underlying price"),
 ("books[].ratio / basis", "ratio / pts", "CROSSHAIR tooltip (conversion)", "ratio 41.9150, basis 50.25", "Index-to-futures conversion used by the AxisMapper"),
 ("books[].note", "text", "LEVEL tooltip", "verbatim", "Why the book is/isn't convertible"),
 ("books[].levels[Gamma Flip]", "price", "LEVEL (solid yellow) + HUD flip distance", "REVERT v/^ <px> [Flip+...]", "Zero-gamma level of the book"),
 ("books[].levels[Call Wall]", "price, $GEX/1%", "LEVEL + BAND peak", "REVERT v <px> [CallWall]; chip <px> · $<gex>", "Strike with the largest positive dealer GEX"),
 ("books[].levels[Put Wall]", "price, $GEX/1%", "LEVEL + BAND peak", "REVERT ^ <px> [PutWall]; chip <px> · $<gex>", "Strike with the largest negative dealer GEX"),
 ("books[].levels[Top |GEX|]", "price, $GEX/1%", "BAND (net GEX metric) + LEVEL if clustered", "band only; chip <px> · $<gex>", "Next-largest exposure strikes"),
 ("books[].levels[].fut", "price (axis)", "AxisMapper output (position of every LEVEL/BAND)", "n/a", "Level converted to the axis instrument (tick-rounded)"),
 ("books[].pricing[Exp Move +/-1s]", "price", "LEVEL (dashed) '-1σ EM <px>'", "+1σ EM <px> / -1σ EM <px>", "Options-implied 1-sigma move bounds"),
 ("books[].pricing[RND Median/Mode/Qxx]", "price, prob", "MARKER (dotted) + crosshair", "RND Mode <px> · <p>%", "Risk-neutral distribution landmarks"),
 ("books[].pricing[25D Put/Call]", "price, IV", "LEVEL (thin dotted)", "25D Put <px> · <iv>% IV", "25-delta strikes (skew anchors)"),
 ("books[].atm_iv", "vol", "HUD tooltip (EM chip)", "ATM IV 17.8%", "ATM implied vol at the reference expiry"),
 ("books[].rr25", "vol pts", "HUD tooltip (VRP/skew)", "25Δ RR -2.1", "Risk reversal: skew direction"),
 ("books[].em_pct / ref_dte", "% / days", "HUD 'EM ±<pts> used <x>%'", "EM ±400 used 47%", "1-sigma move at reference DTE; used = range travelled / EM"),
 ("books[].vol.term[]", "dte, IV, RR", "SIDEBAR-ONLY (mirrored in HUD tooltip)", "term spark", "ATM IV and RR by expiry"),
 ("books[].vol.slope / backwardated", "vol/√yr, bool", "HUD chip 'BACKWARDATED'", "contango|BACKWARDATED", "Term-structure shape"),
 ("books[].vol.rr25 / put_wing", "vol pts", "HUD tooltip", "put wing +3.2", "Downside wing richness"),
 ("books[].vol.rv / vrp / em_ratio", "vol, ratio", "HUD 'VRP <x>' (n/a without realised feed; GAP)", "VRP 1.06", "Realised vs implied; n/a when no realised feed"),
 ("books[].vol.em_1d / realized_move", "pts", "READ ('used 46% of the range')", "±405 pts", "Daily expected move and travelled range"),
 ("books[].vol.hhi / top3 / near_oi", "0..1", "HUD tooltip (concentration)", "top-3 strikes 61%", "Gamma concentration"),
 ("books[].vol.scen[] (MTM grid)", "$M", "CROSSHAIR tooltip row", "MTM @-1% IV flat -$212M", "Dealer P&L grid under spot/vol shocks"),
 ("multibook.books[].weight / prior / liquidity", "0..1", "LEVEL tooltip (weight per contributor)", "NDX 85%", "Book weight in the aggregate"),
 ("multibook.books[].included / reason", "bool, text", "STATUS data-health chip", "QQQ excluded: stale", "Excluded books and why"),
 ("multibook.books[].net_gex / flip / call_wall / put_wall", "$, price", "LEVEL tags + BAND", "see levels", "Per-book dealer exposure summary"),
 ("multibook.confidence", "0..1", "HUD chip 'CONF 94%'", "CONF 94%", "Data/model confidence"),
 ("multibook.flip_confluence", "0..1", "LEVEL tag [Flip] boldness + HUD tooltip", "flip confluence 72%", "Share of weight whose flip agrees"),
 ("multibook.sign_agreement", "-1..1", "HUD gamma chip tooltip", "agreement +0.9", "Do books agree on gamma sign"),
 ("multibook.raw_net_gex", "$/1%", "HUD gamma chip", "GAMMA NEGATIVE $-1.0B", "Summed dealer GEX of included books"),
 ("multibook.notes[]", "text", "STATUS tooltip", "verbatim", "Engine notes (exclusions, anchor price missing)"),
 ("converter (book, strike → axis)", "price", "CROSSHAIR 'strike → axis' readout", "QQQ 520 → NQ 21,846.00", "Same formula as the Strike Lens converter"),
 ("scope.mode / family / model_variant", "enum", "page header chip", "FAMILY · NQ · 7 books", "Selection driving chart + sections"),
]: row(SL, f, "strike_lens.h / multibook", u, e, l, t)
# ---------------- Strike Insights (strike_insight.h insight_json; per book, per horizon) ----------------
for f, u, e, l, t, g in [
 ("insight.as_of / spot", "ET time, price", "STATUS 'LIVE <ts>' + HUD", "Tue 20 Oct 10:10 ET", "Snapshot time all conclusions are dated to", ""),
 ("horizon.name / expiries[] (dte,label,oi_share,gamma_share)", "text", "HUD tab chip + BAND tooltip 'fed by'", "Intraday: 0DTE 50% OI, 1DTE 50% OI", "Source expiries feeding the horizon", ""),
 ("horizon.headline (regime)", "text", "READ line 1", "Negative-gamma regime ...", "Regime sentence for the horizon", ""),
 ("horizon.net_gamma", "$/1%", "HUD gamma chip", "GAMMA NEGATIVE $-1.0B (…%ile/30d)", "Horizon net dealer gamma; %ile needs a 30d store", "30d percentile needs stored history → shown only when present"),
 ("horizon.flip", "price", "LEVEL [Flip] + HUD distance", "REVERT v <px> [Flip]", "Nearest horizon gamma flip", ""),
 ("horizon.em_pct", "%", "HUD EM chip", "EM ±<pts>", "Horizon 1-sigma move", ""),
 ("horizon.assumption", "text", "STATUS tooltip", "dealers long calls / short puts (assumed)", "Positioning assumption", ""),
 ("card.strike", "price", "LEVEL + BAND centre (exact strike px)", "<px> chip", "Structural strike", ""),
 ("card.dist_pct / sigma_dist", "% / σ", "CROSSHAIR tooltip", "+0.34% · 0.95σ", "Distance from spot", ""),
 ("card.gamma", "$/1%", "BAND intensity (net GEX metric) + left chip", "<px> · $-35.8M", "Dealer gamma at strike (signed)", ""),
 ("card.vanna_up / vanna_dn", "shares per ±1 vol pt", "BAND metric 'vanna' + tooltip", "vanna +1pt SELL 1.5K", "Hedge flow per vol point", ""),
 ("card.charm_day / charm_close", "shares/day, shares", "BAND metric 'charm' + tooltip", "charm to close +15.5K", "Time-decay hedge flow", ""),
 ("card.vomma / vega / theta / color / speed / delta_sh", "$ / sh", "BAND metrics + tooltip rows", "tooltip table", "Remaining Greeks at the strike", ""),
 ("card.iv_k / skew", "vol", "tooltip", "IV 17.9% (+0.3 vs ATM)", "Risk pricing at the strike", ""),
 ("card.p_beyond / p_touch", "prob", "MARKER 'LIKELY ▲ <px> · <p>% touch'", "LIKELY ▲ 29,806 · 57% touch", "Risk-neutral chance of finishing beyond / touching", ""),
 ("card.gamma_share / importance", "0..1", "MARKER strength tag (WEAK/MODERATE/STRONG) + BAND normalisation", "◎ MAGNET ▼ <px> ±<band> · <share>% · WEAK", "Pin strength / structural importance", ""),
 ("card.verdict / contested", "enum", "LEVEL direction 'REVERT v/^' or MARKER '◎ MAGNET'", "Magnet|Support|Resistance|Acceleration|Pivot|Weak", "How the level tends to behave", ""),
 ("card.mechanism / approach / breach", "text", "LEVEL/MARKER tooltip", "verbatim", "Why and what to expect", ""),
 ("card.valid_until / valid_limit / as_of", "ET time, text", "LEVEL tooltip + dimming when expired", "valid until 16:00 ET", "Validity window from data", ""),
 ("card.invalidation[]", "text", "ZONE (stop band) + tooltip", "stop <px>", "What breaks the reading", ""),
 ("card.votes[] (Gamma/Color/Charm/Vanna/Vomma)", "+1/0/-1", "tooltip table + LEVEL border style (dashed = contested)", "agrees / conflicts", "Cross-Greek agreement", ""),
 ("card.conviction / confidence", "text", "MARKER strength + tooltip", "conviction medium", "Signal quality", ""),
 ("card.regime", "text", "tooltip", "verbatim", "Regime modifier", ""),
 ("card.pb.style / hold / potential / speed", "text", "READ line 3 + MARKER tooltip", "Momentum entry: break and hold", "Trade framing derived from Greeks", ""),
 ("card.pb.entry / stop / target (px, rr, p_target, capture)", "price, R, %", "ZONE entry/stop/target ladder drawn at the strike", "T <px> +<pts> (1.9R, 34%)", "Playbook levels", ""),
 ("card.pb.edge[]", "text", "tooltip", "verbatim", "Extra edge notes", ""),
 ("insight.reconcile[]", "text", "HUD tooltip on tab chip", "verbatim", "Why Intraday and Swinging disagree", ""),
]: row(SI, f, "strike_insight.h", u, e, l, t, g)
# ---------------- Hedge Flow ----------------
for f, u, e, l, t, g in [
 ("hedge.shock_pct[] / anchor_contracts[] / notional[]", "%, contracts, $", "CROSSHAIR '$<x>M/1%' via monotone-cubic interpolation + SUBPANEL hedge curve", "29,743 $493M/1%", "Cost to move / dealer hedge per spot move", "ladder has 7 points (±2%); finer grid is a model addition (§5.9)"),
 ("hedge.vanna_up_c / vanna_dn_c", "contracts per vol pt", "HUD tooltip + BAND metric tooltip", "vanna ±1pt", "Vanna flow in anchor contracts", ""),
 ("hedge.charm_1d_c", "contracts/day", "READ ('tailwind up/down') + HUD tooltip", "charm 1d +6,294", "Time-decay hedge flow", ""),
 ("hedge.zero_dte_up_c / zero_dte_dn_c", "contracts", "HUD tooltip", "0DTE ±1%", "0DTE-only flows", ""),
 ("hedge.per_book[] (0DTE gamma/OI share)", "0..1", "LEVEL tooltip (per contributor)", "NDX 0DTE 34% γ / 25% OI", "How much of each book expires today", ""),
 ("session.pts[] (hedge, charm, vanna, gex vs minute)", "contracts, $", "SUBPANEL charm/hedge series (shared time axis)", "line", "Greeks tracked open → now", ""),
 ("session.observed[] (window, move_pct, flow, reading)", "%, contracts, text", "MARKER on time axis (Observed) + READ", "Observed: dealers BOUGHT 2,026 while price rose 0.35%", "What already happened", "needs ≥2 snapshots; else 'unavailable'"),
 ("session.reads[] (strengthening/fading)", "enum", "tooltip + HUD chip colour", "fading", "Trend of each Greek through the session", ""),
 ("session.charm_headline / interaction / closing", "text", "READ lines 2-3", "verbatim (bold on emphasis tokens)", "Into-the-close interpretation", ""),
 ("session.to_close_min / as_of / window / n / into_close", "min, text", "STATUS bar + HUD tooltip", "350 min to close", "Time awareness", ""),
 ("projected rows (price/when/flow/effect per shock)", "text", "MARKER 'projected' glyph at trigger price + tooltip", "◇ +1% $22,068 SELL 2,182 DAMPS", "Expected flow if the trigger occurs", ""),
 ("summary conclusion", "text", "READ", "verbatim", "Plain-language hedge flow conclusion", ""),
]: row(HF, f, "hedge_analytics.h / session_track.h", u, e, l, t, g)
# ---------------- Sidebar (sidebar_xbook.js + app.js legacy block) ----------------
for f, u, e, l, t, g in [
 ("alerts[] (tag, severity, text)", "rank", "HUD event chips (sev≥2) + READ", "FOMC / GAMMA / 0DTE chips", "Ranked 'right now' items", ""),
 ("alert bar (RIGHT NOW)", "rank", "HUD (same items)", "chips", "Slim bar on every page stays as is", ""),
 ("key control strikes (zero-gamma, call wall, put wall, EM ±1σ)", "price", "LEVEL lines (identical px)", "see levels", "Weighted medians across books", ""),
 ("cross-book rows (weight, gamma sign, flip offset, 0DTE, backwardation)", "mixed", "LEVEL tooltip + HUD tooltip", "per book", "Who is pushing", ""),
 ("scoped read (validated text, scope badge, disclaimer)", "text", "READ (collapsed 'more')", "verbatim", "Validated analysis for the scope", ""),
 ("session card (charm track, interactions, per-Greek trend)", "see Hedge Flow session", "SUBPANEL + READ", "see session", "Same session block", ""),
 ("high-exposure strikes with context", "price, verdict", "LEVEL/MARKER (same as Strike Insights cards)", "see cards", "Strikes with verdict/validity", ""),
 ("volatility strip (VIX/VXN level, regime, term)", "index pts", "HUD chip 'VXN 26.1 stress'", "VXN 26.07 (+6.24) stress", "Vol regime feeding all analyses", "needs vol feed"),
 ("scope line (mode/family/books/confidence/variant)", "text", "page header chip", "FAMILY · NQ · 7 books", "Selection state", ""),
 ("primary-book legacy block (GAMMA(TERM), VANNA, CHARM, flip distance, RV/IV z)", "model channels", "HUD gamma chip + SUBPANEL", "GAMMA NEGATIVE ...", "Single-Book model reading", ""),
 ("regime probabilities / triggers (regime_live)", "prob", "HUD session chip 'RANGE' + READ", "RANGE", "PCA/GMM regime state", ""),
 ("model card (multibook_v1 vs single_v1)", "text", "SIDEBAR-ONLY (stays in sidebar; chip 'multibook_v1' in header)", "multibook_v1", "Model comparison", ""),
 ("session reference levels SET / PDL / ONL / wOPEN", "price", "ZONE + LEVEL (labelled)", "SET 30,081 / PDL 30,054 / ONL 29,826", "Session reference levels (spec §2.6)", "GAP: needs the session bar series (BarFeedAdapter has OHLC bars; PDL/ONL/SET derivable once bars are wired)"),
]: row(SB, f, "sidebar_xbook.js / app.js", u, e, l, t, g)
# ---------------- chart-only elements with their source (spec §2) ----------------
CHART_ONLY = [
 ("Liquidity heatmap (§2.3)", "L2 depth", "GAP: no depth feed in OptionsChainAdapter/BarFeedAdapter; layer renders 'unavailable' until a depth adapter exists"),
 ("Forensics: sweep / absorption / iceberg / CVD divergence (§2.7)", "trade tape + depth", "GAP: no tick/trade feed; EM-edge touch is derivable from bars + EM now"),
 ("Block prints (§2.7)", "trade tape", "GAP"),
 ("Volume + CVD sub-panels (§2.9)", "bars (+ aggressor side for CVD)", "volume available from bars; CVD needs aggressor-side trades"),
 ("Bid/ask with sizes (§2.9)", "top of book", "GAP"),
 ("Structural heat rows (all three models)", "chart.field metrics struct/gex/vanna/charm/vomma/vega/delta/ptouch/oi + hedge ladder", "BUILT: one non-linear navy->blue->gold->yellow->red ramp, fixed scale per metric, epoch blend + live easing"),
 ("Outcome chips (MAGNET / PINNING / REJECTION / AMPLIFIER)", "levels[].outcome / why / p_touch (rule-based in chart_snapshot.h)", "BUILT: centred, level with the price row, 19 px spacing"),
 ("Asset + strike pills (e.g. QQQ $516 - NQ $21,678)", "levels[].pill / src[] (dominant contributing book)", "BUILT"),
 ("Now line 82%, future zone, step price line, bid/ask dashed markers", "bars + epochs (+ top of book)", "BUILT on example bars/book; GAP: real tape/top of book"),
 ("Heat scale legend, settings panel, replay controls", "UI state", "BUILT"),
]
md = ["# Chart Factors — Traceability Matrix", "", "Generated by `tools/gen_traceability.py` (do not edit by hand). One row per output field of Strike Insights, Strike Lens, Hedge Flow and the sidebar. `GAP` rows are still mapped: the element exists, the data is not yet feedable (see audit §5).", ""]
for sec in [SL, SI, HF, SB]:
    md += [f"## {sec}", "", "| field | source | unit | chart element | label format | tooltip | gap |", "|---|---|---|---|---|---|---|"]
    for r in [x for x in R if x["section"] == sec]:
        md.append("| " + " | ".join(str(r[k]).replace("|", "\\|") for k in ["field", "source", "unit", "element", "label", "tooltip", "gap"]) + " |")
    md.append("")
md += ["## Chart-only elements (spec §2) and their data dependency", "", "| element | data needed | status |", "|---|---|---|"] + [f"| {a} | {b} | {c} |" for a, b, c in CHART_ONLY] + ["", f"**Fields mapped: {len(R)} / {len(R)} (100%).** Fields with a data gap: {sum(1 for r in R if r['gap'])}."]
open((VT_ROOT + "/docs/chart-factors-traceability.md"), "w").write("\n".join(md) + "\n")
json.dump(dict(rows=R, chart_only=[dict(element=a, needs=b, status=c) for a, b, c in CHART_ONLY]), open((VT_ROOT + "/docs/chart-factors-traceability.json"), "w"), indent=1)
print(len(R), "rows")
