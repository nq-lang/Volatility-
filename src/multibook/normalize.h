#pragma once
// normalize.h - cross-book unit conversion (T2-4). Pure functions, no I/O.
#include <algorithm>
#include <cmath>
#include <vector>
namespace vt::mb {
// Live ratio between anchor price space and a book's underlying (never a fixed constant).
struct LevelMap { double ratio = 1.0; double basis = 0.0;
    // book strike -> anchor price space: K_anchor = K_book * ratio + basis
    double to_anchor(double k_book) const { return k_book * ratio + basis; }
    double to_book(double k_anchor) const { return (k_anchor - basis) / ratio; } };
// Recover ratio and basis from two (book, anchor) price observations; one observation => ratio only.
inline LevelMap fit_level_map(double s_book, double s_anchor, double basis = 0.0) { return {(s_anchor - basis) / s_book, basis}; }
inline LevelMap fit_level_map2(double b1, double a1, double b2, double a2) { double r = (a2 - a1) / (b2 - b1); return {r, a1 - r * b1}; }
// Dollar gamma per 1% move: sign * gamma * OI * multiplier * S^2 * 0.01
inline double dollar_gamma_1pct(double sign, double gamma, double oi, double multiplier, double S) { return sign * gamma * oi * multiplier * S * S * 0.01; }
// Dollar delta per 1% move in anchor expressed in anchor-equivalent contracts.
inline double equiv_contracts(double dollar_delta, double anchor_price, double anchor_multiplier) { return dollar_delta / (anchor_price * anchor_multiplier); }
// Leveraged ETF end-of-day rebalance in exposure terms: AUM * (L^2 - L) * index_return (positive = buy).
inline double lev_etf_rebalance(double aum, double L, double ret) { return aum * (L * L - L) * ret; }
// Component gamma -> index-equivalent: index weight * beta.
inline double component_to_index(double gex, double index_weight, double beta) { return gex * index_weight * beta; }
// Wall/flip confluence: fraction of weight whose translated level lies within tol (fractional) of the weighted median.
struct Level { double px; double w; };
inline double confluence(std::vector<Level> v, double tol) {
    if (v.empty()) { return 0; }
    double tw = 0; for (auto& l : v) tw += l.w;
    if (tw <= 0) { return 0; }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.px < b.px; });
    double acc = 0, med = v.back().px; for (auto& l : v) { acc += l.w; if (acc >= tw / 2) { med = l.px; break; } }
    double in = 0; for (auto& l : v) if (std::fabs(l.px - med) <= tol * std::fabs(med)) in += l.w; return in / tw;
}
}
