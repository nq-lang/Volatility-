#pragma once
// Charm exposure -- CEX = Sigma[Charm_c*OI_c + Charm_p*OI_p] * m  (03_charm.md).
// Plain addition, per-day units. Time-decay-driven hedging flow; dominant near
// 0-1DTE and accelerates non-linearly (convex) into the close on 0DTE sessions
// -- expect the largest charm-driven flow concentrated in the final 30-60min.
// NOTE: the KB recommends computing charm via FINITE DIFFERENCE of delta
// (t vs t+1day) rather than trusting a single closed-form expression, since
// published closed-form variants disagree on drift-term treatment. This file
// still uses the analytic vt::bs::greeks() closed form for continuity with
// the rest of the engine; swap in a finite-difference charm (re-run
// bs::greeks with t_years - 1/365 and diff the deltas) if you want to match
// the KB's stricter recommendation exactly.
#include "vt/exposure.h"

namespace vt::model {

inline ExposureCurve charm_exposure(const ChainSnapshot& chain, DteBucket bucket, bool use_volume_proxy = false) {
    auto raw = exposure::accumulate_raw(chain, bucket, use_volume_proxy,
                                         [](const Greeks& g) { return g.charm; });
    ExposureCurve curve;
    curve.bucket = bucket;
    curve.ts = chain.ts;
    curve.points.reserve(raw.size());
    for (const auto& [key, acc] : raw)
        curve.points.push_back({key.first, key.second, acc.call_raw + acc.put_raw});
    return curve;
}

} // namespace vt::model
