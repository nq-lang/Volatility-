# Mock data in this repository

Everything below is synthetic. It exists so the UI and tests run without live feeds, and it is labelled "example" / "MOCK DATA" in the UI.

| File | What it is | Produced by |
|---|---|---|
| `preview/synthetic_lens_payloads.json` | Strike Lens, multi-book and Chart Factors payloads (+ 6-epoch history) for ES/SPX multi-book (also degraded), NQ, GC, YM, CL, SI families. Built by the real engine on synthetic chains. | `preview/gen_lens_payloads.cpp` |
| `preview/synthetic_payloads.json`, `synthetic_regime_payloads.json`, `synthetic_viz_payloads*.json`, `synthetic_scoped_payload.json`, `synthetic_mbmodel_payload.json`, `synthetic_vol_payload.json` | Regime / PCA / viz / scoped / multi-book / VIX-VXN payloads | `preview/gen_*.cpp`, `gen_*.py` |
| `web/js/chartfactors/cf_mock.js` | Order-flow example layers: 1-min candles, volume, CVD, block prints, forensic flags, bid/ask, SET/PDL/ONL/wOPEN, L2 streaks | hand-written generator (deterministic) |
| `preview/preview.html` | Standalone page with the fixtures inlined | `python3 preview/make_preview.py` |

Not mocked on purpose (shown as unavailable, never inferred): realised volatility / VRP, event calendar, directional BIAS model, 30-day gamma percentile.
Real data replaces these when the order-flow API and VIX/VXN history arrive; the model snapshot format does not change.
