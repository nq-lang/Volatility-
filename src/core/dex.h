#pragma once
// DEX -- Dealer hedge delta = Sigma[-Delta_c*OI_c + Delta_p*OI_p] * m  (per 07_dex.md, literal formula).
// This reports DEALER-SIDE hedge delta (opposite sign of what dealers sold to
// customers under the same "dealers long calls / short puts" convention as
// GEX -- see vt::exposure::DealerConvention; keep the same `conv` value used
// for GEX so the two stay internally consistent). Put delta is already
// negative by construction, so the "+Delta_p*OI_p" term naturally contributes
// a negative (short-delta-equivalent) amount from the put side.
//
// Read jointly with GEX: GEX gives curvature/regime, DEX gives the existing
// directional tilt already baked into the current hedge book.
#include "vt/exposure.h"

namespace vt::model {

inline ExposureCurve dex(const ChainSnapshot& chain, DteBucket bucket,
                          exposure::DealerConvention conv = {}, bool use_volume_proxy = false) {
    auto raw = exposure::accumulate_raw(chain, bucket, use_volume_proxy,
                                         [](const Greeks& g) { return g.delta; });
    ExposureCurve curve;
    curve.bucket = bucket;
    curve.ts = chain.ts;
    curve.points.reserve(raw.size());
    // Literal KB formula: -Delta_c*OI_c + Delta_p*OI_p. `invert` flips both signs
    // together to model the opposite dealer-positioning assumption.
    double call_sign = conv.invert ?  1.0 : -1.0;
    double put_sign  = conv.invert ? -1.0 :  1.0;
    for (const auto& [key, acc] : raw) {
        double value = call_sign * acc.call_raw + put_sign * acc.put_raw;
        curve.points.push_back({key.first, key.second, value});
    }
    return curve;
}

} // namespace vt::model
