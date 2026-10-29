#pragma once
// Call-delta and put-delta IV surfaces. Strike-indexed IV surfaces get
// contaminated by spot movement; delta-indexing isolates genuine skew dynamics
// by tracking a constant probability-of-exercise point (Framework doc, Section 5).
// Run as two SEPARATE surfaces per the spec — the divergence between them is
// the tradable signal and is invisible on one blended smile.
#include <cmath>
#include "vt/types.h"
#include "vt/blackscholes.h"
#include "vt/dte.h"

namespace vt::model {

inline Surface call_delta_iv_surface(const ChainSnapshot& chain, DteBucket bucket) {
    Surface s;
    s.name = "call_delta_iv";
    s.bucket = bucket;
    s.x_axis_label = "delta";
    s.ts = chain.ts;
    for (const auto* q : dte::select_bucket(chain, bucket)) {
        if (q->type != OptionType::Call || q->iv <= 0.0) continue;
        Greeks g = bs::greeks(OptionType::Call, chain.spot, q->strike, chain.risk_free_rate,
                               chain.dividend_yield, q->iv, q->t_years);
        double dlt = g.delta; // 0..1 for calls
        if (dlt >= 0.05 && dlt <= 0.50) // 5-50 delta band per the build spec
            s.points.push_back(SurfacePoint{dlt, q->dte_days, q->iv});
    }
    return s;
}

inline Surface put_delta_iv_surface(const ChainSnapshot& chain, DteBucket bucket) {
    Surface s;
    s.name = "put_delta_iv";
    s.bucket = bucket;
    s.x_axis_label = "delta";
    s.ts = chain.ts;
    for (const auto* q : dte::select_bucket(chain, bucket)) {
        if (q->type != OptionType::Put || q->iv <= 0.0) continue;
        Greeks g = bs::greeks(OptionType::Put, chain.spot, q->strike, chain.risk_free_rate,
                               chain.dividend_yield, q->iv, q->t_years);
        double dlt = std::abs(g.delta); // report puts as |delta|, 0..1 band, 5-50
        if (dlt >= 0.05 && dlt <= 0.50)
            s.points.push_back(SurfacePoint{dlt, q->dte_days, q->iv});
    }
    return s;
}

// Divergence metric between the two surfaces at a matched delta/DTE — the
// actual tradable signal per the Framework doc. Positive = put-delta IV
// running hotter than call-delta IV at the same |delta| (skew steepening /
// fear); negative = call side bid relative to put side (risk-on unwind).
inline std::optional<double> skew_divergence(const Surface& call_iv, const Surface& put_iv,
                                              double target_delta, int dte_days, double tol = 0.03) {
    const SurfacePoint* c = nullptr;
    const SurfacePoint* p = nullptr;
    for (const auto& pt : call_iv.points)
        if (pt.dte_days == dte_days && std::abs(pt.x - target_delta) < tol) { c = &pt; break; }
    for (const auto& pt : put_iv.points)
        if (pt.dte_days == dte_days && std::abs(pt.x - target_delta) < tol) { p = &pt; break; }
    if (!c || !p) return std::nullopt;
    return p->iv - c->iv;
}

} // namespace vt::model
