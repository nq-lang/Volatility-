# Heatmap audit against `dealer_flow_heatmap_dynamics_spec.md`

Scope: the Chart Factors heatmap only (`web/js/chartfactors/cf_chart.js`, fed by `src/chartfactors/chart_snapshot.h`).
Where the spec and the owner's later written directives disagree, the owner's directive wins and the spec behaviour stays available as a setting.

## Owner directives applied (override the spec)
| Directive | Implementation |
|---|---|
| Single non-linear ramp (supersedes the earlier yellow/purple scheme) | Midnight navy `#0B132B` -> deep navy -> royal blue -> dark gold -> vivid light yellow -> bright yellow -> saturated red `#FF1E1E` (top ~3% only: gamma walls / pins), OKLab-interpolated, 1024-entry LUT, no banding (max channel step < 14 across the LUT). Positive and negative exposure share the ramp by magnitude (symmetric); `negWeight` setting gives distinct weights; sign is carried by tags, pills, chips and the hover readout. |
| Dynamic text contrast | Dark halo on every overlay text, dark plate + outer outline on chips and pills, luminance of the heat under each level (`heatLum`) selects plate opacity / halo strength; chips keep their price alignment and hover highlight. |
| Same data = same colour on every update; forward region keeps intensity; only the past is dimmer | Default scale mode `fixed` (one H per metric, running max of the model p98, never decreases within a scope). Past fade is a setting (`fade`, default 0.5; 0 = none, the spec's no-fade); the projection right of "now" is never dimmed. |
| Heatmap = all three models + sidebar structure, not only GEX | Metric selector: Structural (default), Net GEX, Vanna, Charm, Vomma, Vega, Delta hedge, Touch prob, Open int., Hedge $/1%. |
| "Liquidity" = dealer flow, risk/distribution pricing, vol-adaptive, probabilistic | `struct` metric (C++): dealer GEX x (0.5 + 0.5 x risk-neutral touch chance) x (0.6 + 0.4 x RND density); row spread widened by `field.vol_k` (0.8..1.4 from the VIX/VXN percentile). Updates every epoch. |
| MAGNET/LIKELY chips central and at their prices; say how an approach ends | Chips centred horizontally, vertically on the level's price row (19 px collision spacing, leader line only when displaced). Text = outcome (MAGNET / PINNING / REJECTION / AMPLIFIER) + touch % + reason. Rule-based in C++, assumes dealers long calls / short puts. |
| Right labels show asset and strike, e.g. "QQQ $564 - NQ $25,114" | `pill` built in C++ from the dominant contributing book: `QQQ $516 - NQ $21,678 +n` (+n = other books on that level). |

## Section audit (visual impact order)
| § | Item | Status | Note |
|---|---|---|---|
| 4.2-4.4 | Colour pipeline (ramp per directive above) t, t^gamma, soft alpha, LUT | Match | gamma 0.7, noise 5% of H, bands, brightness are settings |
| 4.3 | OKLab LUT 1024 | Match | luminance-profile / colour-vision simulation not run |
| 4.5 | Adaptive H (p99.5), asymmetric EMA 1 s / 8 s, hysteresis 2%, crossfade >15% | Match (option) | not the default, see directives |
| 4.5 | Reference windows rolling / session | Missing | visible window only |
| 4.7 | Rank blend beta | Missing | |
| 4.8 | Absolute mode | Match | single H in native units (not three thresholds) |
| 4.9 | Edge cases | Partial | empty, NaN, H=0 verified; a lone value in fixed mode renders full colour (no mid-colour cap) |
| 4.10 | Control panel | Match | "Heatmap settings" panel, Defaults button |
| 5.5 | No fade on history | Deviation | owner directive; `fade = 0` restores |
| 6.1 | Time-based X placement, rAF loop | Match | columns positioned by minute; sub-pixel scroll while replaying; values interpolated between model states (labelled setting) |
| 6.2 | Ingest / sampler / render split | Partial | one column per model epoch, not per second |
| 6.3 | Live-column value easing | Match | live column + projection ease previous -> new model state; history frozen |
| 6.4 | Auto-recenter with easing | Match | 180 ms time constant; one shared transform; manual pan/zoom and Reset view |
| 6.5 | GPU texture + shader | Deviation | canvas2D CPU bitmap, nearest-neighbour; WebGL2 planned |
| 7 | Pause / scroll-back, colours identical | Match | replay slider; lock freezes H |
| 7 | Ring buffer, max decimation, memory pressure | Partial | ring buffer (720 states) done; decimation not needed at one value per state |
| 8.1 | Now line at 82%, dotted | Match | test asserts 0.82 +/- 0.005 |
| 8.2-8.3 | Step price line, green up / red down, head on now line | Match | source = example bars (order-flow API pending) |
| 9 | Future zone = live state projected flat, no seam | Match | same texture, same scale |
| 9 | Red/green dashed markers | Match | bid/ask are example data; drawn >= 8 px apart for legibility |
| 10 | Rows 9 px + 1 px gap | Partial | auto bucket gives 8-13 px rows; `rowPx` and `bucket` settings |
| 13 | Crosshair readout, L/H legend, pan/zoom, live button | Match | |
| 3.6 | Feed-gap handling | Partial | gaps longer than the timeout draw empty columns (tested); no real feed yet

## Acceptance scenarios (section 15)
| # | Scenario | Result |
|---|---|---|
| 1 | Steady level crosses now line, no seam | Pass (visual; same texture and scale either side of the line) |
| 2/3 | Building / fading streak steps along the ramp | Pass on the 20-state NQ mock run (values and colours change smoothly; GIF) |
| 4 | Dissolving level | Not exercised (needs a live feed) |
| 5 | Outlier raises scale fast, recovers slowly | Pass (`verify_chart`, adaptive mode) |
| 6/7 | Quiet / heavy period | Not exercised on real data |
| 8 | Trending price recenter | Pass (view easing test, replay GIF) |
| 9 | Zoom-out max aggregation | Not applicable yet (one value per epoch) |
| 10 | Pause and scroll back | Pass (replay) |
| 11 | Far-from-price dim | Pass (visual) |
| 12 | Feed gap | Pass on mock states (empty columns after the timeout) |
| 13 | Scale lock | Pass |
| 14 | Empty / single value | Pass for empty / NaN / H=0; single value not capped |
| 15 | 60 fps / memory | Not benchmarked |

## Architecture blockers and fixes
1. Model snapshots arrive per epoch, not per second: the spec's 1 s column grid needs the publisher/protocol milestone or the live feed.
2. CPU canvas bitmap: move the texture + LUT to WebGL2 (uniforms H, L, gamma, bands) once the feed exists; raw values are already stored, never colours.
3. No ring buffer / gap timeout: add with the live feed.
