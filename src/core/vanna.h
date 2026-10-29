#pragma once
// Vanna exposure -- VEX = Sigma[Vanna_c*OI_c + Vanna_p*OI_p] * m * S * 0.01  (02_vanna.md).
// Plain addition across calls/puts (NOT netted like GEX/DEX) -- vanna's own
// sign already encodes its directional meaning per strike (generally negative
// for OTM puts, positive for OTM calls in the standard convention), so the
// formula sums raw contributions rather than applying a further dealer-sign flip.
// IV-change-driven hedging flow; the trigger mechanism behind "Vanna Crush."
// Read together with Implied Z-Score (iv_zscore.h) per 17_implied_zscore_vanna_exposure.md:
// IV_z tells you the PROBABILITY of a reverting vol move; VEX tells you the
// MAGNITUDE/DIRECTION of the hedging flow that move would trigger.
#include "vt/exposure.h"

namespace vt::model {

inline ExposureCurve vanna_exposure(const ChainSnapshot& chain, DteBucket bucket, bool use_volume_proxy = false) {
    auto raw = exposure::accumulate_raw(chain, bucket, use_volume_proxy,
                                         [](const Greeks& g) { return g.vanna; });
    ExposureCurve curve;
    curve.bucket = bucket;
    curve.ts = chain.ts;
    curve.points.reserve(raw.size());
    double s_pct = chain.spot * 0.01; // per 1-vol-point move, futures-equivalent units
    for (const auto& [key, acc] : raw)
        curve.points.push_back({key.first, key.second, (acc.call_raw + acc.put_raw) * s_pct});
    return curve;
}

} // namespace vt::model
