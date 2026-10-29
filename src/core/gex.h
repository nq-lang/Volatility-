#pragma once
// GEX — GEX(S) = Σ[Γ_c·OI_c − Γ_p·OI_p] · m · S² · 0.01   (per 06_gex.md)
//
// Convention (state explicitly whenever this number is shown): dealers are
// modeled net LONG calls, net SHORT puts — i.e. customers are net put-buyers
// and net call-sellers/overwriters. This is an assumption, not an observed
// fact (see vt::exposure::DealerConvention).
//
// Regime read: GEX >> 0 = deep long-gamma (dealers buy dips/sell rips, range
// compresses, price pins). GEX << 0 = deep short-gamma (dealers sell dips/buy
// rips, range expands, moves extend). See 14_dealer_gamma_flip_distance.md
// for the zero-crossing solve (gamma_flip.h) -- the flip point matters more
// than any single strike's GEX contribution.
#include "vt/exposure.h"

namespace vt::model {

inline ExposureCurve gex(const ChainSnapshot& chain, DteBucket bucket,
                          exposure::DealerConvention conv = {}, bool use_volume_proxy = false) {
    auto raw = exposure::accumulate_raw(chain, bucket, use_volume_proxy,
                                         [](const Greeks& g) { return g.gamma; });
    ExposureCurve curve;
    curve.bucket = bucket;
    curve.ts = chain.ts;
    curve.points.reserve(raw.size());
    double s2_pct = chain.spot * chain.spot * 0.01; // "per 1% move" scaling, per the KB formula
    double call_sign = conv.invert ? -1.0 : 1.0;
    double put_sign  = conv.invert ?  1.0 : -1.0;
    for (const auto& [key, acc] : raw) {
        double value = (call_sign * acc.call_raw + put_sign * acc.put_raw) * s2_pct;
        curve.points.push_back({key.first, key.second, value});
    }
    return curve;
}

} // namespace vt::model
