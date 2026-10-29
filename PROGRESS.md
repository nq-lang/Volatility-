# VOLATILITY ANALYSIS TERMINAL — PROGRESS TRACKER
_Last updated: this session. Update this file whenever a task moves state._

## DONE
- [x] Quant core (include/vt/, src/core/): types, Black-Scholes greeks, DTE bucketing,
      GEX/DEX/VEX/CEX/SEX/COLEX with the corrected type-dependent KB formulas,
      VRP, RND (Breeden-Litzenberger), call/put-delta IV surfaces + divergence,
      IV z-score, call+put OI ratio momentum, dealer gamma-flip solver,
      RV-vs-IV time-of-day z-score. All compiled + headless-tested (tests/test_core.cpp).
- [x] Scheduler (src/core/scheduler.h): per-bucket recompute, headless-capable, tested.
- [x] UI bridge (src/bridge/ui_bridge.h, json_util.h): dependency-free JSON serialization.
- [x] Section 2 "Vol Structure" engine (src/insight/): four-layer confluence prompt
      (Structural Backdrop / Flow Dynamics / Distribution & Risk Pricing / Fresh Flow
      Confirmation) + Implied Z-Score x Vanna composite signal, dealer convention stated
      explicitly. Orchestration in structural_insight_engine.h.
- [x] Front end (web/): single scrolling page. Vol Structure section (TWO distinct panels:
      "Current Structural Read" single/live + "Structural Read Log" accumulating history)
      above Volatility Analysis (Greek-surface row, then distribution/risk-pricing row).
      Right sidebar: dense dealer-flow bullet rows (OPTIONS FORCE/MARKET STATE/SCENARIO),
      matching the supplied reference design. ORDER FLOW placeholder removed per request.
- [x] 2D/3D toggle on every Greek surface panel (web/js/surface_renderer.js): 2D bar chart +
      real Three.js wireframe 3D mode with hover crosshair + stats overlay. Works in the
      real webview (has network); offline preview shows an honest "requires real webview"
      notice instead of faking it.
- [x] PCA Regime Model (include/vt/pca.h, include/vt/gmm.h, src/core/regime_model.h,
      src/bridge/regime_bridge.h): Layer A (PCA, 17 standardized features -> 3D) + Layer B
      (5-component GMM, EM-fit, probabilistic not hard-label), cluster-to-regime label
      mapping re-derived every refit (bijective, from responsibility-weighted centroid
      profiles), dual-trigger system (soft Speed-percentile + hard flip-sign-change),
      decay-only projected path (Charm/Color forward projection) + deviation metric,
      Mean-Reversion/Charm-Vanna override flag, narrative generation, explained-variance
      transparency. Fully unit-tested standalone (tests/test_regime.cpp) — PASSES,
      including a bijective label-mapping check and correct classification of a held-out
      synthetic "Trending" day.
- [x] PCA section added to the web page (bottom, per instruction): header, 3D biplot
      (Three.js in production / honest placeholder offline), explained-variance line,
      three insight cards (current read + narrative, regime probability bars in the 5
      reference colors, dual-trigger + decay-deviation readout).
- [x] Screenshots captured via headless Chrome + a byte-for-byte-identical offline preview
      harness (app.js/style.css copied verbatim from web/; only the Three.js-dependent
      renderer is swapped for an offline Canvas2D stand-in) — in /mnt/user-data/outputs/.

## IN PROGRESS / NEXT UP (this session)
- [x] **Round PCA bubbles (user feedback: "bubbles seem flat").** Measured cause: the fitted
      covariance ellipsoids were genuinely flat (shortest axis only 31-42% of the longest).
      Default is now ROUND: a sphere with the same volume as the regime's fitted ellipsoid
      (r = cbrt(r1*r2*r3)), so size still shows how spread out a regime is. A "Fitted Shape"
      toggle restores the true covariance shape (the original spec's requirement). Tooltip states
      which shape is showing. Test `tests/ui/verify_bubbles.py`: all 5 round radii equal, volume
      preserved, fitted restored, rendered width/height ratio 1.016 (1.000 = perfect circle).
  - Hand-off note: this zip was packaged WITHOUT the usual fresh-extract rebuild (user asked for
    speed). Last full verification (JS syntax, HTML balance, C++ -Werror suites, 44-combo DTE test)
    predates only this bubble change; pca_biplot.js passed node --check and verify_bubbles.py.
- [x] **Page re-layout (signal first, raw data second, no dead space).**
  - Page 1 order is now: PCA Regime Model (absolute top, full-bleed, includes biplot +
    Regime Insights, unchanged internally) -> the two structural reads with the Dealer Flow
    sidebar beside them -> every Distribution & Risk-Pricing surface, full width.
  - Sidebar is stretched to exactly its neighbour's height (measured: 0 px difference on both
    pages) and its rows flex to fill it. To keep that honest rather than padded, added a real
    "Dealer exposure by DTE" matrix (net GEX/DEX/Vanna/Charm/Speed/Color/Theta for 0/1/2DTE/Term,
    from data already on the bridge).
  - greeksChannel page: sidebar beside the first two models (GEX, DEX); the other five run
    full width. The one sidebar element is moved into the active page's slot on navigation.
  - Surface panels are full width everywhere (1668 px at a 1700 px viewport = page gutters only).
  - Interpretation note: "sidebar proportional to the first two 3D/2D models" was read as
    "beside the lead block of each page" (page 1: the two structural reads; page 2: GEX+DEX).
    Easy to change if a different pairing was meant.
- [x] **PCA spread sliders** (Bubbles, Vectors). Bubbles = exploded view: each regime's bubble
      moves out together with ITS OWN points (clusters stay intact, the live dot/trail ride with
      the predicted regime's bubble). Vectors = multiplier on the single shared arrow constant
      (all arrows scale together, so Part B's one-constant rule still holds). A note appears
      whenever either is not 1.0x: exact score positions / exact per-feature decomposition apply
      at 1.0x only.
  - Bug found while testing: auto-fit re-zoomed on every slider move, so spreading barely
    changed the on-screen gap (bubbles just shrank). Fixed by capping the shrink at 72% of the
    1x scale. Measured result: Pinning<->Trending gap 334 px (1.0x) -> 411 (1.6x) -> 554 (2.2x);
    longest arrow 241 px -> 357 px at 2.0x.
  - Biplot now auto-fits to its box's aspect ratio (wide box -> wide cloud, not a small island)
    and has a regime legend (colour, name, n days, live probability) in the right-hand space.
  - Slider max capped at 2.5x.
- [x] HTML structure bug caught and fixed during the restructure: two stray closing </div>
      (38 open / 40 close) were found by tracing nesting depth line-by-line, now 38/38.
- [x] Re-ran the 44-combination DTE verification on the new layout: 0 failures.
  - Checks at hand-off: node --check on all 4 web JS files; HTML balanced + every referenced
    file resolves; both C++ suites compile with -Wall -Wextra -Werror and pass.
- [x] **DTE isolation audit (user-reported concern) — found and fixed a REAL production bug.**
      Frontend was already correct (2D chart + 3D mesh are fed by the same per-bucket
      `points`), but the C++ bridge was mislabelling buckets: `to_json(Surface)` inferred the
      bucket from the FIRST point's dte, so the Term payload for VRP / call-delta IV /
      put-delta IV (which spans all expiries) went out labelled "0DTE" and OVERWROTE the 0DTE
      tab with all-expiry data while the Term tab got nothing. RND was only published once
      (all expiries, labelled "0DTE"), so its 0/1/2DTE tabs never had real data. My earlier
      demo payloads were hand-tagged correctly, which hid this -- found by probing the real
      bridge output instead.
  - Fix: `Surface` now carries an explicit `bucket` (set by vrp/call/put-delta producers);
    `rnd_surface(chain, bucket)` filters expiries per bucket and the scheduler publishes RND
    for all four buckets; the bridge emits `s.bucket` instead of guessing.
  - Regression test added to `tests/test_core.cpp`: every bucketed channel x every bucket
    (44 payloads) must carry the right label, 0/1/2DTE payloads must contain ONLY that DTE,
    term must span multiple. Mutation-checked: re-introducing the bug makes it fail.
  - Frontend hardening: a DTE tab with no published data now shows "awaiting <bucket> data"
    instead of leaving the previous tab's data on screen; live updates repaint only when they
    belong to the currently-selected tab (was hard-coded to 'term').
  - Single-DTE view: still a true filled 3D surface (the one real strike profile extruded
    across 7 grid rows -- no invented expiries), explicitly labelled "N DTE (single expiry)";
    cross-section charts now read "N DTE cross-section" (old "3D cross-section" for 3 days
    was easily misread as "three-dimensional").
  - UI verification script `tests/ui/verify_dte.py` (also in preview/): 11 panels x 4 tabs =
    44 combinations -- checks DTE set, that 3D-grid values == payload values for that bucket
    (hence == the 2D chart), single-DTE mesh depth + label, term multi-row, and the
    empty-state. Result: 0 failures, 0 page errors.
  - Checks run at hand-off: `node --check` on all 4 web JS files (+ HTML references all
    resolve), both C++ suites compiled with `-Wall -Wextra -Werror` and passing.
- [x] **Verified, no bug found**: DTE tabs (0DTE/1DTE/2DTE/term) correctly isolate data
      for both the 3D mesh AND the 2D cross-section stack -- each bucket shows its own
      gamma-flip/wall values and its own cross-section(s), confirmed via screenshot
      (0DTE flip=6001, 1DTE flip=6002, term flip=6003, all distinct).
- [x] **Verified, already done**: Distribution & Risk-Pricing surfaces (VRP, RND,
      call/put-delta IV) on the main page already share the exact same `SurfacePanel`
      class as the Greeks, so they already have the simultaneous 3D+2D side-by-side
      layout, visually identical styling, and real per-expiry cross-sections --
      confirmed via screenshot, no code change needed (this was a side-effect of making
      the renderer generic rather than Greek-specific).
- [x] **Per-card high-conviction levels checklist**, added to BOTH the Current
      Structural Read and the Structural Read Log (same render function, so it's
      automatic on both): a distinctly-bordered, headed side area conjoined to each
      card (NOT the global dealer-flow sidebar) showing ONLY "price (Long/Short)" +
      its mechanism type (Magnet/Rejection/Breakthrough/Pin) + a confluence-based
      probability estimate shown to its right.
- [x] **Inline level highlighting**: every mention of a qualifying level's price
      inside the card's prose (key_levels, scenario, regime line, etc.) is now wrapped
      in the same colored badge, wherever it's stated alongside the description of the
      environment -- not just listed separately.
- [x] Color semantics, corrected per clarification: yellow marks the single highest
      -conviction LONG strike this cycle, purple marks the single highest-conviction
      SHORT strike (at most one of each) -- not a generic "very strong" flag. Every
      other qualifying level stays green(long)/red(short).
- [x] Extended the Section 2 LLM contract (`src/insight/prompt_template.h`) to match:
      `key_levels_structured` now carries `conviction: "normal"|"highest"` (capped at
      one "highest" per direction) and a `probability` (0-100, confluence-based, not a
      fixed/rounded number). Added explicit instruction that both the regime read and
      the levels list must synthesize the FULL stack every cycle -- dealer positioning,
      dealer flow, risk pricing, AND price distribution together -- never gamma or any
      single Greek in isolation; a level's strength comes from how many independent
      layers stack on the same price, which is the actual filter for inclusion.
- [x] New **greeksChannel** page: GEX/DEX/Vanna/Charm/Speed/Color/Theta moved off the
      main Volatility Analysis page entirely onto their own page, reached via a nav
      link in the header (`web/index.html`'s `.vt-nav`), stacked vertically (full width,
      one below the other) instead of the old horizontal scroll row. Main page now only
      carries Vol Structure, the Distribution & Risk-Pricing surfaces, and the PCA
      Regime Model. Sidebar stays visible on both pages.
  - Bug caught + fixed: panels built while their page was `display:none` (greeksChannel
    is built eagerly at startup so data keeps flowing in the background) sized their
    Chart.js canvas against a collapsed container. Fixed by re-firing a resize event
    (and calling `chart.resize()`) when a page becomes visible.
- [x] **Simultaneous 3D+2D layout** (2D/3D toggle removed per the new spec): every
      surface panel now shows the true 3D mesh on the left and a stacked column of real
      per-expiry 2D cross-section charts on the right, always both, side by side.
  - 2D cross-sections are dot+line charts (a solid node at every discrete data point,
    connected by a line; signed series color each segment green/red by local sign;
    non-signed series use the model's base color), each with its own dashed SPOT/ATM
    marker and a white ring on the local peak — matches the supplied reference style.
  - 3D projection walls are now DATA-DRIVEN, not empty grid panels: the shortest-dated
    expiry's strike/delta profile is silhouetted flat on the back wall ("skew at a
    glance"), and the ATM/center-strike value across every expiry is silhouetted on the
    side wall ("term structure at a glance"), both walls chosen dynamically so orbiting
    never puts a wall in front of the surface.
  - Verified for both visual styles (signed dealer-exposure surfaces and the Bloomberg
    -style IV surfaces) via screenshots.
- [x] **Root-cause fix for "surfaces, not ridges"**: GEX/DEX/Vanna/Charm/Speed/Color/Theta
      previously aggregated across ALL expiries in the Term bucket into one strike-only
      curve — there was no real second axis to surface. Fixed at the architecture level:
      `ExposurePoint` now carries `dte_days`, `exposure::accumulate_raw` keys by
      `(strike, dte_days)` instead of strike alone, and every model file + the UI bridge
      was updated to match. Verified in the headless test: the Term bucket's GEX curve
      went from 41 points (1 row) to 205 points (41 strikes × 5 real expiries).
- [x] Rewrote the 3D renderer (`web/js/surface_renderer.js`) from scratch as a TRUE
      continuous filled-quad mesh over the real (x, expiry) grid — no more ghosted/
      duplicated ridge lines. Two styles: `'signed'` (dealer-exposure/VRP/RND — green/red
      diverging shading, translucent SPOT plane, strike ticks, Net/flip/wall stats) and
      `'bloomberg'` (IV surfaces — green-to-orange shaded mesh, dark grid lines, grey
      translucent wall panels with a numeric scale, orange axis titles), matching the
      supplied Bloomberg and GEX reference images respectively.
- [x] Built `web/js/vt3d.js`: a small shared, dependency-free 3D camera + pointer/touch
      orbit controller (drag/pinch/wheel/tap/double-tap-to-reset) used by BOTH the
      surface panels and the PCA biplot, so desktop and mobile behave identically and
      there is no separate "offline stand-in" renderer anymore — the same code that ships
      is what gets screenshotted.
- [x] Rebuilt the offline preview harness to load the PRODUCTION `web/` files directly
      (`preview/make_preview.py` strips only the network CDN tags from `web/index.html`)
      instead of maintaining a parallel copy — eliminates an entire class of preview/
      production drift bugs going forward.
- [x] Regenerated the synthetic demo dataset with REAL multi-expiry grids (7 term-bucket
      expiries per model, each with its own curve/decay), replacing the earlier
      single-expiry-per-bucket demo data that could never have rendered as a real surface.

## PCA "bubbles and vectors" display spec — implemented in full (this session)
- [x] **Part A (bubbles)**: added `regime::component_ellipsoid()` (C++, eigendecomposes
      each GMM component's real 3x3 covariance) and wired it into
      `regime_static_json`'s new `"ellipsoids"` array (center + 3 scaled semi-axis
      vectors per regime). `pca_biplot.js` renders these as wireframe ellipsoids — never
      spheres — with a label rendered every frame (not hover-only), satisfying the
      spec's explicit "no hover-only labels" requirement.
- [x] **Part B (vectors)**: added a single shared `regime::kLoadingArrowScale` constant
      (sent to the client as `arrow_scale`) — points are always raw PCA scores, every
      arrow uses this one constant, never mixed conventions. Added
      `regime::feature_contributions()` (C++) computing the EXACT per-feature pull
      vectors for the live observation (`standardized_feature[i] * loadings[i]`), shipped
      via `regime_live_json`'s new `"feature_contributions"` field. The biplot draws the
      top pulls as a fan from the origin (not chained tip-to-tail — chaining let large
      offsetting pulls wander off-canvas even though their vector sum is bounded).
- [x] **Flip Dist direction bug, fixed at the root**: built a new demo generator
      (`preview/gen_pca_regime.py`) that computes a REAL PCA fit (numpy `eigh` on the
      standardized covariance) and REAL per-regime empirical covariance from the SAME
      projection — so loading directions and bubble positions are guaranteed
      self-consistent, unlike the old demo which hand-placed decorative bubble positions
      unrelated to the (also decorative) arrow directions. Verified numerically: the
      Trending archetype's centroid and the Flip Dist loading vector have opposite signs
      on the dominant axis, so the arrow correctly points AWAY from Trending (screenshot
      confirms this visually).
- [x] Part C reading protocol: tap-to-inspect wired for all three object types (bubble →
      regime name + n; arrow tip → feature name + today's pull magnitude; live dot →
      current predicted regime), auto-framing camera radius so the whole fitted structure
      is visible on first paint without manual zoom.
- [x] Screenshotted and verified end-to-end with the real (if simulated) C++-shaped data
      contract — not a mockup.

## BUG FOUND AND FIXED THIS SESSION (process note for future continuity)
- A `class PcaBiplotPanel { ... }` removal in `app.js` (meant to delete only the old
  inline biplot class, now superseded by `pca_biplot.js`) accidentally deleted every
  function defined after it in the file — `onModelUpdate`, `renderSidebar`,
  `wallsOf`/`netOf`, and the scenario-slider helpers all went with it, silently, because
  they happened to be positioned after that class in file order. Caught by actually
  loading the file in a Node sandbox and checking `typeof window.onModelUpdate` rather
  than just checking `node --check` (which only validates syntax, not that expected
  symbols still exist). Fully restored and reverified. Lesson applied: after any
  large structural edit to a shared file, verify the specific exports/symbols the rest
  of the app depends on, not just syntax validity.
- [x] Two distinct 3D surface styles implemented per supplied reference images:
      - [x] 'rainbow' continuous wireframe surface (IV surfaces: call/put-delta IV) —
            purple(high)->blue->green->yellow->orange->red(low) height-based coloring,
            right-side numeric axis-scale overlay, matching the reference exactly.
      - [x] 'ridge' ghosted multi-strata wireframe + translucent SPOT plane (every
            dealer-exposure surface + VRP + RND) — green above/red below zero, colored
            PER-SEGMENT (not one flat color per line) so a strata line crossing zero
            renders correctly, stats overlay with model-specific labels (GEX gets
            "Gamma Flip"/"Call Wall"/"Put Wall"; others get generic "Zero-cross"/
            "Wall(+)"/"Wall(-)"), hover crosshair + detail box.
      - [x] Enabled the 2D/3D toggle on the Distribution & Risk-Pricing row too (was
            previously Greek-row-only — bug, now fixed).
      - [x] Added a `spot` bridge channel + dashed orange strike-line marker on 2D charts.
- [x] Built a genuine offline 3D fallback (manual yaw/pitch projection + Canvas2D,
      painter's-algorithm depth sorting) for BOTH surface styles AND the PCA biplot,
      so preview screenshots show real projected geometry from real (simulated) data
      instead of a "not available offline" placeholder. Production still uses real
      Three.js (better quality/interactivity); the offline path is preview-only.
- [x] Captured and shared: rainbow call/put-delta IV 3D surfaces, ridge-style GEX and
      VRP 3D surfaces, and the PCA biplot with simulated 5-cluster data + live trail —
      all real renders, not mockups.
- [x] Brighter, more distinct section-identity system: colored top-border + glow per
      section (Vol Structure = blue, Volatility Analysis = amber, PCA Regime = purple),
      icons, LIVE (pulsing dot) vs. SESSION LOG badges, brighter descriptive subtext.
- [x] Restyled every Greek-exposure and distribution/risk-pricing panel to match the
      supplied reference dashboard: clean bar chart (Chart.js in production, matching
      Canvas2D in the offline preview), hidden axis clutter, dashed orange spot/strike
      marker, green/red signed coloring for exposure models, blue/red call-vs-put-side
      coloring for the IV surfaces, teal for RND. Added a one-line description + color
      legend key under every panel title.
- [x] Added a `spot` bridge channel (was missing) so panels can draw the strike marker.
- [x] Wire the PCA regime model into scheduler.h / main.cpp:
      - [x] Added `RegimeHistoryAdapter` interface to vt/adapters.h.
      - [x] `Scheduler::run_cycle()` now assembles the 17-feature raw vector from
            already-computed ModelState fields (ATM Implied Z-Score now wired via a
            second `iv_history(..., delta=0.5)` call; 25d Risk-Reversal + a rolling
            momentum tracker added; RND excess-kurtosis proxy added alongside the
            existing skewness proxy in `rnd.h`'s `shape_stats`) and, if a
            `RegimeModelService` is attached, runs the live layer and publishes
            `regime_live` each cycle.
      - [x] `RegimeModelService` (src/core/regime_service.h) wraps batch refit +
            live evaluation + trigger detection + decay path + narrative.
      - [x] `Scheduler::refit_regime_model()` pulls history via `RegimeHistoryAdapter`,
            fits, and publishes `regime_static` once.
      - [x] tests/test_core.cpp extended: full Scheduler + RegimeModelService
            integration proven headlessly (batch refit via the scheduler, then a
            `run_cycle()` that publishes a real `regime_live` payload). PASSES.
      - [x] main.cpp updated: constructs the regime service, attaches it, does the
            initial batch refit, refits hourly on the worker thread, routes
            `regime_static`/`regime_live` to `onRegimeStatic`/`onRegimeLive`.
      - [ ] KNOWN GAP, flagged in main.cpp: `RegimeModelService::establish_baseline()`
            (§5's pre-market snapshot) isn't called anywhere yet — needs a real
            market-calendar hook to know when "pre-market" is, which this sandbox
            can't build/test. Until wired, the decay-only-path deviation metric
            stays at 0 (harmless no-op, not a crash) rather than reflecting a real
            baseline comparison. Documented inline in main.cpp with the two ways to
            close the gap.
- [ ] CMakeLists.txt + README.md for the whole project (mentioned early on, not yet written).
- [x] Presented the two-table Vol Structure view + PCA Regime Model section screenshots.

## SESSION 2026-10-10: Pricing Surfaces page, ticker selector, no emoji, §10 validation
- [x] New "Pricing Surfaces" page (header nav). VRP, RND, Call-Delta IV, Put-Delta IV moved off the main page. The dealer-flow sidebar sits beside VRP and RND; the delta-IV pair is full width below. Main page now ends after the two structural reads.
- [x] Header ticker selector: SPX, SPY, NDX, QQQ, horizontal, next to the title. Click clears the panels to "awaiting" and calls `window.vtSetUnderlying(sym)`; main.cpp binds it to `Scheduler::set_underlying()` (thread-safe, applied at the top of the next cycle, resets OI-momentum trackers and the regime service, then refits). Tested: test_core asserts the adapter is queried with each new symbol.
- [x] All emoji removed from web/ (section/row icons, warning glyph) and their CSS. Plain typographic arrows and geometric bullets remain.
- [x] Removed the third futures-product asset and the extra data-source stub from the prompt template, app.js comments and this file.
- [x] §10 validation suite, src/validation/regime_validation.h, tests/test_validation.cpp, CLI `vt_validate`:
      10.1 out-of-sample forward-range separation (train/holdout, Kruskal-Wallis plus a Pinning<Trending Mann-Whitney),
      10.2 rolling-refit stability (loading |cos|, day-label agreement, regime-centroid drift),
      10.3 soft-trigger percentile sweep (precision, recall, false-positive rate, lift over chance),
      10.4 label mapping re-derived per refit, bijective, seed-stable,
      10.5 event-day segregation (fit with vs without flagged days).
      Positive controls pass; four negative controls (shuffled outcomes, inverted outcomes, uninformative speed, overlapping clusters) are asserted to FAIL.
- [x] Findings while building it (real, not cosmetic):
      - Single-start EM landed in different partitions per seed with near-identical likelihood. `gmm::fit` is now best-of-10 restarts.
      - After standardization every column has unit variance, so a cluster axis carried by one column cannot beat the noise plateau and PC3 is unstable. Feature design should use blocks of co-moving columns; check PC3 stability on real data.
      - 3 PCs cannot resolve five clusters whose centres need a 4th axis. That is a property of the spec's 3-PC choice.
      - The default 80th-percentile soft trigger is only a spec example: on the synthetic series it gave 1.3x lift over chance, p90 gave 3.4x. Calibrate on real data.
- [x] Source probe (src/tools/source_probe.h, tests/test_probe.cpp, src/app/probe_main.cpp): measures OI refresh cadence and available history depth against any adapter.
- [x] CMakeLists.txt (headless tests + vt_validate always; vt_app and vt_probe behind -DVT_BUILD_APP=ON; -UNDEBUG so asserts stay live in Release) and README.md.
- [x] Verification: JS syntax, HTML div balance, 4 C++ suites with -Werror (also via ctest), verify_dte.py (44 panel/tab combos, now including the new page), verify_bubbles.py.

## SESSION 2026-10-10 (evening): shared sidebar, pre-market baseline
- [x] Sidebar: one element shared by Pricing Surfaces and greeksChannel only (removed from the main page at the user's request; main page is the PCA section plus the two structural reads, full width), repainted on every page switch. tests/ui/verify_sidebar.py checks it is visible and never duplicated on all three pages, and that an update received on one page is already shown on the others (both directions).
- [x] Pre-market baseline (spec section 5) done inside Scheduler::run_cycle(): the first feature vector of each UTC date, and the first after startup or an underlying switch, becomes the baseline. An hourly refit re-projects the baseline through the new loadings without clearing the intraday trail. test_core asserts the baseline exists after the first cycle.

## OPEN ITEMS THAT NEED THE REAL DATA SOURCE (code is done, the answers are not)
- Intraday OI refresh cadence: `vt_probe` (src/tools/source_probe.h) polls the chain adapter and reports the measured cadence. Cannot be answered until the live API is connected; the sandbox has no network.
- Historical chain depth for the 6-12 month rolling window: the same probe requests 30-730 day windows, reports rows returned / non-finite cells / row width, and says whether 6 and 12 months are supported.
- Run `vt_validate days.csv bars.csv` on real exports; the regime output is not cleared for live-decision use until all five §10 gates pass on real data.

## DECISIONS (resolved by the user)
- Selectable underlyings are exactly SPX, SPY, NDX, QQQ. No other asset exists in the terminal.
- No order-flow data source is planned.

## STILL NOT DONE
- Real webview integration: vendor `third_party/webview/webview.h`, write `your_api_adapters.h` / `your_llm_client.h`, build with `-DVT_BUILD_APP=ON`. The CMake targets, the ticker binding and the probe are in place; cannot be built or run in the sandbox (no network, no GUI toolchain).

## 2026-10-18
- Task 1 (viz layer) complete except UMAP. Task 2: registry, Scope, normalization, selection UX done; see docs/PROGRESS_LOG.md.
- 2026-10-18 pm: per-book cache, scope-driven engine, Strike Lens (futures-equivalent strikes), 2D view controls. See docs/PROGRESS_LOG.md.


## 2026-10-19
T2-5 hedging, T2-6 vol/risk, T2-7 multibook_v1, T2-8 scoped LLM layer, cross-book sidebar + alert bar, page-1 strike/futures levels: done on mock data. 12/12 C++ suites, all UI suites pass. See docs/PROGRESS_LOG.md (addendum 3).

## 2026-10-21/22 (workfile A, B, C complete on mock data; Chart Factors M0 delivered, gated)
- Strike Lens chrome, Volatility section (bottom of page), shared Greek/time/regime logic (`strike_insight.h`, `session.h`), Intraday/Swinging tabs with full-Greek cards + trade playbook, Hedge Flow rework, session tracker, sidebar: done.
- Pricing Surfaces remodel (A-1..A-8): `src/multibook/pricing_analysis.h` + `web/js/pricing_analysis.js`: Risk Pricing, Distribution, Options-Market, Fundamental (unavailable rows, never inferred), Intraday/Weekly Structure, Cross-Asset Cascades (trigger + factor composer; macro states are hypotheses, live readings shown beside).
- VIX/VXN terminal-wide (C-3/C-4): one `VolCtx` (`vol_context.h`) printed by Hedge Flow, sidebar (vanna/vomma/divergence) and Pricing; strike cards read the same VolIn.
- Chart Factors: M0 only (`docs/chart-factors-*.md`, `schema/chart_factors.fbs`); no feature code until reviewed.
- Tests: `test_pricing_analysis`, `verify_pricing` added to `tools/run_all.sh`.

## 2026-10-23 (Chart Factors page built on mock order-flow data)
- New page "Chart Factors": candlestick/heatmap chart on top, then Strike Insights, Strike Lens, Hedge Flow (same epoch as the chart), sidebar on the right; Greeks page no longer hosts the three sections.
- Heatmap is model-agnostic: metric selector draws net GEX, vanna, charm, vomma, vega, delta hedge, touch probability, open interest, hedge $/1% (all from the C++ `chart_snapshot.h`). Hard-edged rows (default) or smooth toggle; pivots (left exposure·DTE tag, line, right role pill), MAGNET/LIKELY chips, 19 px label collision, price-axis flags, replay controls, settings.
- Colour rule: one fixed E_ref per metric (never decreases, reset on scope change) -> same value = same colour on every update; only the past (left of "now") is dimmed, projection region keeps full intensity.
- Order-flow layers (candles, volume, CVD, blocks, flags, bid/ask, L2 streaks, SET/PDL/ONL/wOPEN) are labelled EXAMPLE mock data until the API arrives.
- Tests: `tests/ui/verify_chart.py` (rows == strike rows, colour purity, E_ref fixed, every metric paints, future >= past brightness, 19 px spacing, epoch gating, parity). Lens/card/risk/pricing UI tests repointed to the new page; nav wraps on narrow screens.

## 2026-10-28 (heatmap pass)
- Heatmap restyled to the owner's directives + `dealer_flow_heatmap_dynamics_spec.md`: yellow (positive) / purple (negative) OKLab LUT ramps, hard-edged rows with 1 px gap, now line at 82%, future zone = live state projected flat, step price line, red/green dashed bid/ask markers, settings panel, fixed/adaptive/absolute scale, lock, bands, brightness.
- Backend: composite `struct` metric (dealer flow x touch prob x RND density, vol-adaptive spread via `vol_k`), per-level `pill` (asset + strike), `outcome` / `why` (MAGNET / PINNING / REJECTION / AMPLIFIER).
- See `docs/heatmap-spec-audit.md` for the audit and gaps.

## 2026-10-29 (colour overhaul)
- Heat ramp replaced by the single navy -> royal blue -> dark gold -> light yellow -> bright yellow -> red ramp; navy base `#0B132B`; contrast-aware halos/plates/outlines on tags, chips and pills; legend shows the ramp.
- Fixtures added for YM, CL, SI (all six tickers NQ ES GC CL SI YM tested in `verify_chart`); redraw benchmark (steady ~4 ms, texture rebuild ~8 ms headless at 1x).
- Mock data documented in `docs/mock-data.md`.
