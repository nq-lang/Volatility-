#pragma once
// RND surface — Breeden-Litzenberger: q(K) = e^{rT} * d2C/dK2, computed from a
// smoothed, arbitrage-free call price curve per expiry, stacked across expiries.
// Recomputed every few minutes per Section 5 (too noisy at tick frequency).
//
// Shape-vs-shift is the actual signal (Framework doc Section 3 / final section):
// a parallel translation of the RND with spot is not new information; a change
// in shape (tail fattening, kurtosis, mode shift relative to spot) is.
#include <vector>
#include <algorithm>
#include <cmath>
#include "vt/types.h"
#include "vt/blackscholes.h"
#include "vt/dte.h"

namespace vt::model {

namespace detail {

// Simple monotone smoothing of the call-price curve across strikes via a
// centered moving average, then enforce convexity (arbitrage-free requirement:
// call prices must be convex and non-increasing in strike) by a single
// pool-adjacent-violators style pass. This is intentionally simple/tunable —
// swap in a SVI or cubic-spline-with-convexity-constraint fit for production
// if the raw chain is thin or noisy.
inline std::vector<double> smooth_convex(std::vector<double> strikes, std::vector<double> calls) {
    // 3-point moving average smoothing.
    std::vector<double> smoothed = calls;
    for (size_t i = 1; i + 1 < calls.size(); ++i) {
        smoothed[i] = (calls[i - 1] + calls[i] + calls[i + 1]) / 3.0;
    }
    // Enforce non-increasing.
    for (size_t i = 1; i < smoothed.size(); ++i)
        smoothed[i] = std::min(smoothed[i], smoothed[i - 1]);
    // Enforce convexity: second differences must be >= 0. One forward pass of
    // local convexification (not a full QP fit, but sufficient to keep the
    // second-derivative estimate from going pathologically negative on noisy ticks).
    for (int pass = 0; pass < 3; ++pass) {
        for (size_t i = 1; i + 1 < smoothed.size(); ++i) {
            double dK_left  = strikes[i] - strikes[i - 1];
            double dK_right = strikes[i + 1] - strikes[i];
            double interp = smoothed[i - 1] + (smoothed[i + 1] - smoothed[i - 1]) * dK_left / (dK_left + dK_right);
            if (smoothed[i] > interp) smoothed[i] = interp; // pull down to convex hull if violated
        }
    }
    return smoothed;
}

} // namespace detail

// Computes the implied risk-neutral density for ONE expiry (all quotes must
// share the same dte_days). Requires >= 5 distinct call strikes to be meaningful.
inline std::vector<SurfacePoint> rnd_single_expiry(const ChainSnapshot& chain, int dte_days) {
    std::vector<double> strikes, calls;
    for (const auto& q : chain.quotes) {
        if (q.dte_days == dte_days && q.type == OptionType::Call && q.mid > 0.0) {
            strikes.push_back(q.strike);
            calls.push_back(q.mid);
        }
    }
    std::vector<SurfacePoint> out;
    if (strikes.size() < 5) return out; // not enough points for a stable 2nd derivative

    // sort by strike
    std::vector<size_t> idx(strikes.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return strikes[a] < strikes[b]; });
    std::vector<double> K, C;
    K.reserve(idx.size()); C.reserve(idx.size());
    for (size_t i : idx) { K.push_back(strikes[i]); C.push_back(calls[i]); }

    auto smoothed = detail::smooth_convex(K, C);

    // Find a representative T and r for discounting (any quote at this dte_days).
    double T = 0.0, r = 0.0;
    for (const auto& q : chain.quotes) {
        if (q.dte_days == dte_days) { T = q.t_years; r = chain.risk_free_rate; break; }
    }
    double disc = std::exp(r * T);

    // Central finite difference for the 2nd derivative on a (possibly) non-uniform grid.
    out.reserve(K.size() - 2);
    for (size_t i = 1; i + 1 < K.size(); ++i) {
        double h1 = K[i] - K[i - 1];
        double h2 = K[i + 1] - K[i];
        double d2C = 2.0 * ( smoothed[i - 1] / (h1 * (h1 + h2))
                            - smoothed[i]     / (h1 * h2)
                            + smoothed[i + 1] / (h2 * (h1 + h2)) );
        double density = std::max(0.0, disc * d2C); // clamp: density can't be negative
        out.push_back(SurfacePoint{K[i], dte_days, density});
    }
    return out;
}

// Stacks RND across every distinct expiry present in the chain into one surface.
inline Surface rnd_surface(const ChainSnapshot& chain, DteBucket bucket = DteBucket::Term) {
    Surface s;
    s.bucket = bucket;
    s.name = "RND";
    s.x_axis_label = "strike";
    s.ts = chain.ts;

    // Only expiries belonging to the requested bucket: 0/1/2DTE are exact-DTE
    // matches, Term spans every live expiry (same rule as dte::select_bucket).
    std::vector<int> expiries;
    for (const auto* qp : dte::select_bucket(chain, bucket))
        if (std::find(expiries.begin(), expiries.end(), qp->dte_days) == expiries.end())
            expiries.push_back(qp->dte_days);

    for (int dte : expiries) {
        auto pts = rnd_single_expiry(chain, dte);
        s.points.insert(s.points.end(), pts.begin(), pts.end());
    }
    return s;
}

// --- Shape-change diagnostics (Framework doc: "read for deformation, not level") ---

struct RndShapeStats {
    double mode_strike{};     // strike of max density (nearest local peak)
    double left_tail_mass{};  // density mass below spot - 2*sigma-equivalent window
    double right_tail_mass{};
    double skewness_proxy{};  // (right_tail_mass - left_tail_mass) / total, sign = skew direction
    double kurtosis_proxy{};  // excess-kurtosis approximation: tail mass (both sides) vs a normal
                               // reference share for the same window -- positive = fatter tails
                               // than normal ("more extreme moves priced than a Gaussian would
                               // imply"), not a moment-based kurtosis; documented approximation,
                               // sufficient for the PCA regime model's standardized feature use.
};

inline RndShapeStats shape_stats(const Surface& rnd, int dte_days, double spot, double window) {
    RndShapeStats st{};
    double max_d = -1.0;
    double left = 0.0, right = 0.0, center = 0.0, total = 0.0;
    for (const auto& p : rnd.points) {
        if (p.dte_days != dte_days) continue;
        total += p.iv; // field reused: density stored in .iv
        if (p.iv > max_d) { max_d = p.iv; st.mode_strike = p.x; }
        if (p.x < spot - window) left += p.iv;
        else if (p.x > spot + window) right += p.iv;
        else center += p.iv;
    }
    st.left_tail_mass = left;
    st.right_tail_mass = right;
    st.skewness_proxy = (total > 0.0) ? (right - left) / total : 0.0;
    // Reference: a normal distribution puts roughly 4.6% of its mass beyond
    // +/-2 sigma total; treat `window` as a rough 2-sigma-equivalent band and
    // compare observed combined tail share against that reference share.
    static constexpr double kNormalTwoSigmaTailShare = 0.046;
    double observed_tail_share = (total > 0.0) ? (left + right) / total : 0.0;
    st.kurtosis_proxy = observed_tail_share - kNormalTwoSigmaTailShare;
    return st;
}

} // namespace vt::model
