#pragma once
// pricing_analytics.h - market-pricing levels per book (NOT dealer-Greek levels): expected-move bands, risk-neutral-density
// quantiles/mode and 25-delta strikes. These are the strike levels shown on the Pricing Surfaces page.
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include "../core/rnd.h"
namespace vt::mb {
struct PricingLevel { std::string label; double strike{}; double value{}; std::string unit; };   // unit: "prob" | "iv" | "sigma" | ""
struct BookPricing {
    bool ok = false; int ref_dte = 0, near_dte = 0; double atm_iv = 0, rr25 = 0, em_pct = 0;     // em_pct: 1-sigma move at ref_dte, as a fraction of spot
    std::vector<PricingLevel> levels;
};
namespace detail2 {
inline std::optional<double> atm_iv(const ChainSnapshot& c, int dte) {
    double best = 1e18, iv = 0; int n = 0;
    for (auto& q : c.quotes) if (q.dte_days == dte && q.iv > 0) best = std::min(best, std::fabs(q.strike - c.spot));
    for (auto& q : c.quotes) if (q.dte_days == dte && q.iv > 0 && std::fabs(q.strike - c.spot) <= best + 1e-9) { iv += q.iv; ++n; }
    if (!n) { return std::nullopt; }
    return iv / n;
}
// strike whose BS delta (own IV) is closest to target (calls +0.25, puts -0.25) at this expiry
inline std::optional<std::pair<double, double>> delta_strike(const ChainSnapshot& c, int dte, OptionType t, double target) {
    double bd = 1e18, K = 0, iv = 0; bool any = false;
    for (auto& q : c.quotes) if (q.dte_days == dte && q.type == t && q.iv > 0 && q.t_years > 0) {
        auto g = bs::greeks(t, c.spot, q.strike, c.risk_free_rate, c.dividend_yield, q.iv, q.t_years); double d = std::fabs(g.delta - target);
        if (d < bd) { bd = d; K = q.strike; iv = q.iv; any = true; } }
    if (!any) { return std::nullopt; }
    return std::make_pair(K, iv);
}
}
inline BookPricing compute_pricing(const ChainSnapshot& c) {
    BookPricing p; std::set<int> ex; for (auto& q : c.quotes) if (q.dte_days >= 1) ex.insert(q.dte_days);
    if (ex.empty() || c.spot <= 0) return p;
    p.near_dte = *ex.begin(); p.ref_dte = p.near_dte; for (int d : ex) if (d >= 5) { p.ref_dte = d; break; }
    if (p.ref_dte == p.near_dte && ex.size() > 1) p.ref_dte = *ex.rbegin();
    auto ivr = detail2::atm_iv(c, p.ref_dte); if (!ivr) return p;
    p.atm_iv = *ivr; double Tref = p.ref_dte / 365.0, sig = c.spot * p.atm_iv * std::sqrt(Tref); p.em_pct = sig / c.spot;
    p.levels.push_back({"ATM", c.spot, p.atm_iv, "iv"});
    p.levels.push_back({"Exp Move -1s", c.spot - sig, 1, "sigma"}); p.levels.push_back({"Exp Move +1s", c.spot + sig, 1, "sigma"});
    if (p.near_dte != p.ref_dte) { auto in = detail2::atm_iv(c, p.near_dte); if (in) { double sn = c.spot * *in * std::sqrt(p.near_dte / 365.0);
        p.levels.push_back({"Near Move -1s", c.spot - sn, 1, "sigma"}); p.levels.push_back({"Near Move +1s", c.spot + sn, 1, "sigma"}); } }
    auto rnd = model::rnd_single_expiry(c, p.ref_dte);
    if (rnd.size() >= 5) {
        double tot = 0; std::vector<double> cdf(rnd.size());
        for (size_t i = 0; i < rnd.size(); ++i) { double w = i + 1 < rnd.size() ? rnd[i + 1].x - rnd[i].x : (i ? rnd[i].x - rnd[i - 1].x : 1); tot += rnd[i].iv * w; cdf[i] = tot; }
        if (tot > 0) {
            auto q = [&](double pr) { for (size_t i = 0; i < cdf.size(); ++i) if (cdf[i] / tot >= pr) return rnd[i].x; return rnd.back().x; };
            auto mode = std::max_element(rnd.begin(), rnd.end(), [](auto& a, auto& b) { return a.iv < b.iv; });
            p.levels.push_back({"RND 5%", q(0.05), 0.05, "prob"}); p.levels.push_back({"RND Median", q(0.5), 0.5, "prob"});
            p.levels.push_back({"RND 95%", q(0.95), 0.95, "prob"}); p.levels.push_back({"RND Mode", mode->x, 0, ""});
        }
    }
    auto pc = detail2::delta_strike(c, p.ref_dte, OptionType::Put, -0.25), cc = detail2::delta_strike(c, p.ref_dte, OptionType::Call, 0.25);
    if (pc) p.levels.push_back({"25D Put", pc->first, pc->second, "iv"});
    if (cc) p.levels.push_back({"25D Call", cc->first, cc->second, "iv"});
    if (pc && cc) p.rr25 = cc->second - pc->second;
    p.ok = true; return p;
}
}
