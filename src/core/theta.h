#pragma once
// Theta harvest -- not part of the supplied glossary's strict exposure-metric
// list (GEX/DEX/VEX/CEX/SEX/COLEX), but retained as a practitioner add-on:
// ThetaEX = Sigma[-Theta_c*OI_c - Theta_p*OI_p] * m, i.e. plain addition of
// the (negated, since raw option theta is negative) per-contract decay --
// a magnitude gauge of daily premium being harvested by option sellers, not
// a directional signal. High harvest + long-gamma regime is the strongest
// "sell the range" backdrop; the same harvest under short-gamma tells you
// charm-driven rebalancing will still be large but says nothing about direction.
#include "vt/exposure.h"

namespace vt::model {

inline ExposureCurve theta_harvest(const ChainSnapshot& chain, DteBucket bucket, bool use_volume_proxy = false) {
    auto raw = exposure::accumulate_raw(chain, bucket, use_volume_proxy,
                                         [](const Greeks& g) { return -g.theta; });
    ExposureCurve curve;
    curve.bucket = bucket;
    curve.ts = chain.ts;
    curve.points.reserve(raw.size());
    for (const auto& [key, acc] : raw)
        curve.points.push_back({key.first, key.second, acc.call_raw + acc.put_raw});
    return curve;
}

} // namespace vt::model
