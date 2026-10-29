#pragma once
// VRP surface — IV(strike/delta, DTE) - RV. Continuous on the IV leg; RV leg
// updates only at bar close (Section 5). Two structurally distinct readings
// per the Framework doc, Section 6: elevated VRP => event-premium overpricing,
// mean-reversion-friendly; compressed/negative VRP => trend-continuation caution.
#include "vt/types.h"
#include "vt/dte.h"
#include "realized_vol.h"

namespace vt::model {

// One VRP reading per live strike within a DTE bucket, using the strike's own
// mid IV against a single realized-vol estimate for the underlying (RV doesn't
// vary by strike — only the IV leg does — so RV is computed once per call and
// broadcast across every strike in the bucket).
inline Surface vrp_surface(const ChainSnapshot& chain, DteBucket bucket,
                            const std::vector<Bar>& recent_bars, double bars_per_year) {
    Surface s;
    s.name = "VRP";
    s.bucket = bucket;
    s.x_axis_label = "strike";
    s.ts = chain.ts;

    double rv = rvol::parkinson(recent_bars, bars_per_year);
    auto quotes = dte::select_bucket(chain, bucket);
    s.points.reserve(quotes.size());
    for (const auto* q : quotes) {
        s.points.push_back(SurfacePoint{q->strike, q->dte_days, q->iv - rv});
    }
    return s;
}

// Convenience: the single most-quoted headline VRP number (ATM, nearest DTE bucket),
// useful for the Section 2 engine's "elevated/compressed" classification (KB 2.2).
inline std::optional<double> atm_vrp(const Surface& vrp, double spot) {
    if (vrp.points.empty()) return std::nullopt;
    const SurfacePoint* closest = &vrp.points.front();
    double best_dist = std::abs(closest->x - spot);
    for (const auto& p : vrp.points) {
        double d = std::abs(p.x - spot);
        if (d < best_dist) { best_dist = d; closest = &p; }
    }
    return closest->iv; // field reused: for VRP surfaces, .iv holds (IV - RV)
}

} // namespace vt::model
