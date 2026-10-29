#pragma once
// book_analytics.h - per-book analytics computed ONCE per (book_id, snapshot_ts) and cached (T2-2).
// Single View, Multi-Book and Family all read the same cached object, so a book's numbers are identical
// wherever it is viewed (consistency invariant). Uses the existing model:: functions unchanged.
#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include "../core/gex.h"
#include "../core/gamma_flip.h"
#include "pricing_analytics.h"
#include "hedge_analytics.h"
#include "vol_risk.h"
#include "strike_insight.h"
#include "pricing_analysis.h"
namespace vt::mb {
struct StrikeLevel { double strike{}; double value{}; };
struct BookAnalytics {
    std::string id, ts_key; double spot{}, multiplier{};
    double net_gex{}; std::optional<double> flip;       // flip = zero-gamma spot level
    std::optional<double> call_wall, put_wall;
    std::vector<StrikeLevel> top;                        // top strikes by |GEX|, descending
    double oi_notional{};                                // sum(OI) * multiplier * spot, liquidity proxy
    VolRisk vol;                                         // term structure, skew, scenarios, concentration (T2-6)
    InsightMetrics insight;                              // Intraday / Swinging Greek metrics per strike (vol-feed independent, cacheable)
    HedgeAnalytics hedge;                                // dealer hedging flows (delta ladder, vanna, charm, 0DTE)
    Dist dist;                                           // risk-neutral distribution moments/histogram at the reference expiry
    BookPricing pricing;                                 // market-pricing levels (Pricing Surfaces page)
};
inline std::string ts_key(const ChainSnapshot& c) { return std::to_string(c.ts.time_since_epoch().count()); }
inline BookAnalytics compute_book(const std::string& id, const ChainSnapshot& chain, exposure::DealerConvention conv = {}, bool vol_proxy = false) {
    BookAnalytics b; b.id = id; b.ts_key = ts_key(chain); b.spot = chain.spot; b.multiplier = chain.contract_multiplier;
    auto curve = model::gex(chain, DteBucket::Term, conv, vol_proxy);
    b.net_gex = exposure::net(curve);
    std::map<double, double> by; for (auto& p : curve.points) by[p.strike] += p.value;
    for (auto& kv : by) b.top.push_back({kv.first, kv.second});
    if (!b.top.empty()) {
        auto mx = std::max_element(b.top.begin(), b.top.end(), [](auto& a, auto& c) { return a.value < c.value; });
        auto mn = std::min_element(b.top.begin(), b.top.end(), [](auto& a, auto& c) { return a.value < c.value; });
        if (mx->value > 0) b.call_wall = mx->strike;
        if (mn->value < 0) b.put_wall = mn->strike;
        std::sort(b.top.begin(), b.top.end(), [](auto& a, auto& c) { return std::fabs(a.value) > std::fabs(c.value); });
        if (b.top.size() > 5) b.top.resize(5);
    }
    b.flip = model::solve_gamma_flip(chain, DteBucket::Term, 0.10, 41, conv, vol_proxy).flip_level;
    b.pricing = compute_pricing(chain); if (b.pricing.ok) b.dist = compute_dist(chain, b.pricing.ref_dte, b.pricing.atm_iv); b.hedge = compute_hedge(chain, conv); b.insight = compute_insight(chain, conv); b.vol = compute_vol_risk(chain, std::nullopt, std::nullopt, conv.invert);
    double oi = 0; for (auto& q : chain.quotes) oi += q.open_interest; b.oi_notional = oi * chain.contract_multiplier * chain.spot;
    return b;
}
class BookCache {
    std::map<std::pair<std::string, std::string>, BookAnalytics> m_; std::mutex mu_;
public:
    int hits = 0, misses = 0;
    template <class F> const BookAnalytics& get_or_compute(const std::string& id, const std::string& ts, F&& fn) {
        std::lock_guard<std::mutex> l(mu_); auto k = std::make_pair(id, ts); auto it = m_.find(k);
        if (it != m_.end()) { ++hits; return it->second; }
        ++misses; return m_.emplace(k, fn()).first->second; }
    void evict_before(const std::string& ts) { std::lock_guard<std::mutex> l(mu_); for (auto it = m_.begin(); it != m_.end();) it = it->first.second < ts ? m_.erase(it) : std::next(it); }
    size_t size() { std::lock_guard<std::mutex> l(mu_); return m_.size(); }
};
}
