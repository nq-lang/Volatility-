#pragma once
// Dealer gamma flip distance -- D_flip = S - S* where GEX(S*) = 0 (per
// 14_dealer_gamma_flip_distance.md). THE primary regime filter: deep positive
// D_flip% = strong long-gamma/dampening regime (favor mean-reversion); deep
// negative = strong short-gamma/amplifying regime (favor trend/momentum);
// near zero = unstable regime boundary -- reduce confidence, widen stops, and
// weight breakout signals more heavily than mean-reversion signals, since
// crossing S* often ITSELF triggers the regime-consistent move rather than
// merely reflecting it.
//
// Method: re-run the GEX aggregation (term/all-DTE bucket, OI-weighted) at a
// small grid of hypothetical spot levels bracketing current spot, and find the
// sign change via linear interpolation -- this captures gamma's own
// spot-dependence (through d1/d2) rather than assuming GEX(strike) is
// spot-invariant. Track S* itself as a time series: it moves with spot AND
// decays with Color/Speed, it is not a fixed level for the session.
#include <vector>
#include <cmath>
#include <optional>
#include "vt/types.h"
#include "vt/exposure.h"
#include "gex.h"

namespace vt::model {

struct GammaFlipResult {
    std::optional<double> flip_level;   // zero-gamma spot level (S*), if found in the search band
    double distance{};                  // D_flip = spot - flip_level (0 if not found)
    bool   regime_long_gamma{true};     // true if current spot's net GEX > 0
    double net_gex_at_spot{};
};

// Recomputes the OI-weighted GEX net at a hypothetical spot by re-pricing every
// contract's gamma at that spot (OI/strikes held fixed -- this is the standard
// "solve for zero-gamma spot" construction).
inline double net_gex_at_hypothetical_spot(const ChainSnapshot& chain, DteBucket bucket,
                                            double hypothetical_spot,
                                            exposure::DealerConvention conv,
                                            bool use_volume_proxy) {
    ChainSnapshot probe = chain;
    probe.spot = hypothetical_spot;
    auto curve = gex(probe, bucket, conv, use_volume_proxy);
    return exposure::net(curve);
}

inline GammaFlipResult solve_gamma_flip(const ChainSnapshot& chain,
                                         DteBucket bucket = DteBucket::Term,
                                         double search_pct = 0.10,   // search +/-10% around spot
                                         int steps = 41,
                                         exposure::DealerConvention conv = {},
                                         bool use_volume_proxy = false) {
    GammaFlipResult res;
    res.net_gex_at_spot = net_gex_at_hypothetical_spot(chain, bucket, chain.spot, conv, use_volume_proxy);
    res.regime_long_gamma = res.net_gex_at_spot > 0.0;

    double lo = chain.spot * (1.0 - search_pct);
    double hi = chain.spot * (1.0 + search_pct);
    double step = (hi - lo) / static_cast<double>(steps - 1);

    double prev_spot = lo;
    double prev_val = net_gex_at_hypothetical_spot(chain, bucket, prev_spot, conv, use_volume_proxy);
    for (int i = 1; i < steps; ++i) {
        double cur_spot = lo + step * i;
        double cur_val = net_gex_at_hypothetical_spot(chain, bucket, cur_spot, conv, use_volume_proxy);
        if ((prev_val <= 0.0 && cur_val > 0.0) || (prev_val >= 0.0 && cur_val < 0.0)) {
            double t = (0.0 - prev_val) / (cur_val - prev_val);
            double flip = prev_spot + t * (cur_spot - prev_spot);
            if (!res.flip_level || std::abs(flip - chain.spot) < std::abs(*res.flip_level - chain.spot)) {
                res.flip_level = flip;
            }
        }
        prev_spot = cur_spot;
        prev_val = cur_val;
    }

    if (res.flip_level) res.distance = chain.spot - *res.flip_level;
    return res;
}

} // namespace vt::model
