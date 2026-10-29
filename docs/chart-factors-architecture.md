# Chart Factors — Architecture (M0)
- **Core (C++):** existing header-only models (strike_insight, strike_lens, hedge, session_track, vol) produce a `ModelSnapshot` builder (`src/chartfactors/snapshot.h`, M1). Standard is C++23 as in the repo (conflict C1: spec says C++20; C++23 is a superset — no feature depends on it).
- **Publish:** scheduler → `bridge::publish("chart_factors", snapshot_json)` once per model epoch; ticks on a separate light channel.
- **UI:** new page `Chart Factors` in `web/js/app.js` page nav; `web/js/chartfactors/` (vanilla JS now; TS strict + WebGL2 workers is conflict C3 — repo has no TS toolchain; proposal: canvas2D first, WebGL2 field layer in M5, TS conversion by review).
- **Section move:** Strike Insights / Strike Lens / Hedge Flow DOM moves from the Greeks page into a Chart Factors lower stack; the Greeks page keeps the sidebar and loses the blocks (no duplicates; `strike_lens.js` slot `vt-lens-slot-greeks` retired, not hidden).
- **Layers:** bars/price → session levels → strike field (additive blend, super-Gaussian horizontal falloff, §7.5) → level lines/chips → markers → forensics → crosshair; heatmap layer renders "unavailable" with no depth feed.
- **Perf budget plan:** snapshot decode off-thread; field texture re-uploaded only on epoch change; tick path touches price line + crosshair only.
