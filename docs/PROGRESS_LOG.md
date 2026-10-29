# PROGRESS LOG

## Master ledger
| ID | Task | Source | Status | Notes |
|---|---|---|---|---|
| T1-1 | PCA 2D tab | Task 1 | done (mock-verified) | canvas 2D, ortho, in-plane rotate, axis pairs, declutter |
| T1-2 | t-SNE 2D tab | Task 1 | done (mock-verified) | exact t-SNE, warm start + Procrustes, kNN placement, gradients, KDE, fidelity badges |
| T1-3 | Interaction layer | Task 1 | done (mock-verified) | scrubber, decomposition, displacement, spotlight, ribbon, morph |
| T1-4 | t-SNE 3D / UMAP | Task 1 | partial | t-SNE 3D built; UMAP NOT built |
| T2-1 | Registry + symbol master + ambiguities | Task 2 | done | config/registry.json, src/registry; OZA/B_LCO/SO remain open (vendor verification) |
| T2-2 | Scope object + guarded data layer + cache | Task 2 | done (mock-verified) | Scope, ScopedChainSource, BookCache `(book_id, snapshot_ts)`, run_scope, scheduler `set_scope`; consistency invariant tested. Panels' own surfaces are still the single scheduler underlying |
| T2-3 | Selection UX | Task 2 | done (mock-verified) | selector, popup/sheet, keys, remember, family chooser, badge, toggle; panels do not yet consume Scope |
| T2-4 | Normalization, level translation, weighting | Task 2 | done (mock-verified) | live ratio + basis, tick rounding, Strike Lens UI on Pricing + greeksChannel |
| T2-5 | Dealer hedging / flow engine | Task 2 | done (mock-validated) | hedge_analytics.h; ladder/vanna/charm/0DTE per book + anchor-contract aggregate; Hedge flows UI |
| T2-6 | Vol / options-market / risk modules | Task 2 | done (mock-validated) | vol_risk.h; term, skew, VRP (needs RV), scenario MTM, concentration; Vol & risk UI |
| T2-7 | multibook_v1 regime variant | Task 2 | done (framework + synthetic validation) | multibook_model.h, cross_block.h; needs real recorded history to produce a real card |
| T2-8 | LLM prompt + grounding validator | Task 2 | done (mock LLM) | scoped_analysis.h; real LLM client not wired |
| T2-9 | Screenshot suite + manifest | Task 2 | partial | phase-0 baseline, phase-1 selector/popup/badge shots (MOCK) |

## Session 2026-10-18 08:10 (session #1)
### Completed this session
- T2-1: `config/registry.json` (40 symbols, 6 families, aliases, leverage, ambiguity table), `src/registry/{mini_json,registry}.h`, `tools/gen_registry_js.py` -> `web/js/registry_data.js` (UI is registry-driven).
- T2-2: `src/scope/scope.h` (Scope, weight renormalization, confidence, Guarded source). `tests/test_scope.cpp`.
- T2-4: `src/multibook/normalize.h` (level map with live ratio/basis, dollar gamma, equivalent contracts, leveraged-ETF rebalance, confluence). `tests/test_normalize.cpp`.
- T2-3: `web/js/scope_ui.js`, header selector (futures | standard), badge, toggle, popup (modal desktop, sheet mobile), `tests/ui/verify_scope.py`.
### In progress
- T2-2 cache, T2-4 wiring to chains, panels consuming `window.__scope`.
### Blocked / needs decision
- ~~GC conflict~~ RESOLVED 2026-10-18: user re-enabled GC (families.GC.enabled=true). Original note: you earlier said gold must be removed entirely; Task 2 lists GC/OG/GLD again. GC family is in the registry with `enabled:false`, so GC, OG, GLD open no popup and GC is not selectable. Enable by setting `families.GC.enabled=true` and rerunning `tools/gen_registry_js.py`. Recommendation: keep OFF until you confirm.
- Vendor symbols (`vendor_symbol`) are null everywhere; need your data vendor's symbol master. OZA, B/LCO, SO unresolved or unverified (OZA book is excluded from weights).
### Assumptions made (flag for review)
- SI canonical, SL alias; PRO = UPRO; NQ #6 TQQQ/SQQQ, #7 VX; Dow order as given; prior = 0.75^(priority-1); `priority_x_liquidity` default; liquidity/relevance default to 1 until real data feeds exist.
- Scheduler has data feeds only for SPX/SPY/NDX/QQQ; other tickers show awaiting-data. Scope is pushed via optional `vtSetScope` (not yet bound in C++).
### Model integrity check
- Single View regression: PASS by construction (no regime/model file touched; Single View on SPX/SPY/NDX/QQQ takes the old `vtSetUnderlying` path). Scheduler byte-identical tests in test_core re-run below.
- Consistency invariant: NOT YET TESTABLE (per-book cache not built).
### Tests
- New: test_scope, test_normalize, verify_scope.py. Existing UI checks (views, sidebar, bubbles, dte): no failures.
### Screenshots added
- docs/screenshots/phase-1/: selector, QQQ popup, QQQ multi-book badge, NQ family badge (desktop + mobile), all MOCK DATA watermarked.
### Risks / issues discovered
- On mobile the ticker row scrolls horizontally and may start clipped.
- The real vendor/LLM/webview paths cannot run in the sandbox.
### Next session plan
1. Per-book cache + consistency test; wire Scope into scheduler/bridge. 2. T2-5/T2-6 engines. 3. T2-8 LLM validator. 4. T2-7 variant. 5. Single-vs-multi side-by-side screenshots.
### Ledger deltas
- T2-1 ->done; T2-3 ->done (UI); T2-2, T2-4, T2-9 ->partial.

## Session 2026-10-18 08:41 (session #1, addendum)
### Completed
- GC family enabled (user decision); GC/OG/GLD now selectable; GC keeps its own family/model.
- Header: two ticker rows, INDX (index/ETF/single names) above =F (NQ, ES, GC, CL, SI, YM). NVDA/AAPL/MSFT added to the standard list.
- "Single" renamed "Single-Book" (badge, popup, tabs). Single-Book/Multi-Book segmented tabs in the header.
- Futures + Single-Book opens a priority-ranked book picker (keys 1-9, Esc); unresolved books (OZA) disabled; anchor stays highlighted; Multi-Book returns to the family.
### Tests
- verify_scope.py extended (rows, GC, picker order, key pick, OZA disabled); test_scope updated. All pass.
### Screenshots
- phase-1: NQ_singlebook_picker_main, NQ_singlebook_NVDA_main, NQ_family_main (desktop+mobile), MOCK watermarked.

## Session 2026-10-18 13:09 (session #1, addendum 2)
### Completed
- T2-2/T2-4: `src/multibook/{book_analytics,strike_lens,engine,mb_bridge}.h`, `src/scope/scope_io.h`; Scheduler `attach_multibook/set_scope` publishes `multibook` and `strike_lens` only for non-Single scope; `vtSetScope` bound in main.cpp (not compilable here: needs webview). Registry: `cash_ref`, `cash_scale`, `tick`, per-symbol `level:"none"` for miners/energy equities.
- Strike Lens (web/js/strike_lens.js): choose which books to highlight; table of levels with futures-equivalent strikes (live ratio + basis, tick-rounded); custom strike converter; shared element on Pricing Surfaces and greeksChannel; dashed markers on strike-axis panels (3D + cross-sections); hover shows futures equivalent. Not convertible: single names, leveraged ETFs, VX, miners, energy equities (shown struck-through with the reason).
- 2D regime views: Plotly-style modebar (zoom, pan, reset, PNG), clickable regime legend (hide / double-click isolate), color-by Confidence and Time (plasma gradient + colorbar), numeric axis ticks.
### Tests
- test_multibook (Single bit-identical to model::gex/solve_gamma_flip; consistency invariant; cache hits; scope guard; partial data; conversions; scheduler channels), test_scope (+ UI request parsing), verify_lens, verify_viewctl. All pass; all earlier checks pass.
### Assumptions / interpretation (flag)
- "Parimetization similar to the photo" read as: Plotly-style interactive controls (toolbar, legend toggling, ticks, gradient color-by). Say if you meant adjustable embedding parameters (perplexity etc.).
- Gold/silver/crude: no cash index, so conversion is ratio-only (basis embedded) and flagged as such. Futures options (OG/LO/SO): offset between the option underlying and the anchor month.
- Panels still plot the scheduler underlying's surfaces; the Lens maps other books' levels onto that axis. Per-book surfaces (each book's own GEX surface) are not built.
### Not done
- Delta-ladder / vanna / charm / 0DTE for books, vol + risk modules (T2-6), multibook_v1 (T2-7), LLM layer (T2-8).


## Session 2026-10-19 (session #1, addendum 3)
### Completed
- Page-1 Key Levels: every level now shows "BOOK $strike / ANCHOR $fut" (e.g. QQQ $574.5 / NQ $24,475) for each highlighted book (chips in the panel); Single-Book stays plain.
- PCA replay GIF delivered (2D view, timeline stepping with variables spotlighted in turn).
- 2D layout verified: all points inside the plot rectangle, boxes never overlap; Variables box (show/hide/highlight/all/top 8/none) tested.
- T2-5 `hedge_analytics.h`: dealer hedge delta by full revaluation (hedge_delta(S0) == DEX), ladder -2..+2%, vanna +/-1 vol pt, charm 1 day and to close, 0DTE share and +/-1% flow; aggregate in anchor-equivalent contracts (index-convertible books only). UI: Hedge flows block on the Greeks page.
- T2-6 `vol_risk.h`: term structure, 25D RR, put wing, VRP (only if RV supplied, never invented), 1-day priced move, dealer scenario MTM grid, gamma concentration. UI: Vol & risk block on the Pricing page.
- T2-7 `multibook_model.h` + `cross_block.h`: separate multibook_v1 (own standardization/PCA/GMM, PCA dims by variance target, GMM size by forward-chained held-out likelihood, walk-forward only, centroid label alignment, collinear/constant pruning), leakage test, comparison vs single_v1 (eta^2 + permutation test), model card, `multibook_model` channel, MODEL CARD button, biplot arrows/rows colour-grouped by source (anchor vs cross-book).
- T2-8 `scoped_analysis.h`: spec section 9 prompt verbatim (+ separate number-format addendum), structured in-scope-only input, numeric-grounding + scope-leak + mock-disclaimer + section validator, regenerate-with-reasons, deterministic fallback, cache by (scope, snapshot_ts), `scoped_insight` channel.
- Sidebar: scope line, severity-ranked RIGHT NOW alerts (also a bar on every page), key control strikes (strike / futures), cross-book rows, validated scope-badged read; legacy detail kept below.
### Tests
- C++: 12/12 (new test_hedge, test_volrisk, test_multibook_model, test_llm). UI: all suites pass (new verify_keylevels, verify_riskblocks, verify_mbmodel, verify_sidebar_xb, verify_viewlayout).
### Screenshots
- phase-6/7/8/9 (phase-9: 3 pages x Single-Book / Multi-Book / Family, desktop + mobile).
### Assumptions (flag)
- Hedge/vol blocks use the synthetic chains; 0DTE shows 0% because mock chains have no 0DTE expiry.
- cross-book features pruned as unavailable (no data in feed): implied correlation, leveraged-ETF rebalance (needs AUM), VIX slope / G-S ratio / WTI-Brent.
- multibook_v1 card shown is from synthetic history; a real card needs recorded daily history of the 17 anchor features and the cross block.
### Not done
- Real LLM client, real adapters, webview build (main.cpp uncompiled here); UMAP; per-book surfaces; realized-vol feed per book (VRP shows n/a).

## 2026-10-22 - Pricing Surfaces remodel + VIX/VXN integration
Added pricing_analysis.h (Dist moments/histogram cached in BookAnalytics; per-book Analysis in the lens payload as `an`), pricing_analysis.js, vol_context.h (`volctx` in the multibook payload). Honest gaps: no DXY/bond/calendar/fundamental feeds (rows say unavailable), no realised-vol feed (VRP n/a), cascade macro states are user hypotheses. Single-Book is untouched (analysis hidden).
