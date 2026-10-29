#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/multibook/vol_risk.h"
using namespace vt; using namespace vt::mb;
static ChainSnapshot mk(double S, double base, double slope, double rr) {
    ChainSnapshot c; c.spot = S; c.contract_multiplier = 100; c.risk_free_rate = 0.04;
    for (int dte : {1, 7, 30, 90}) for (int i = -15; i <= 15; ++i) for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = std::round(S * (1 + 0.01 * i)); q.dte_days = dte; q.t_years = dte / 365.0;
        q.iv = base + slope * std::sqrt(dte / 365.0) - rr * 0.01 * i; q.open_interest = 1000 * std::exp(-0.03 * i * i); q.mid = 1; c.quotes.push_back(q); }
    return c; }
int main() {
    auto cg = mk(6000, 0.15, 0.10, 0.5); auto v = compute_vol_risk(cg, 0.12, 0.004); assert(v.ok && v.term.size() == 4);
    assert(v.term_slope > 0 && !v.backwardated);                                       // contango
    assert(v.term.back().atm_iv > v.term.front().atm_iv);
    { double ref = 0; for (auto& t : v.term) if (t.dte == v.ref_dte) ref = t.atm_iv; assert(v.vrp && std::fabs(*v.vrp - (ref - 0.12)) < 1e-12 && v.ref_dte >= 5); }
    auto bk = compute_vol_risk(mk(6000, 0.30, -0.15, 0.5)); assert(bk.backwardated && bk.term_slope < 0);          // inverted curve
    assert(!compute_vol_risk(cg).vrp && !compute_vol_risk(cg).em_ratio);                // no RV supplied => no VRP, never invented
    assert(std::fabs(*v.em_ratio - 0.004 / v.em_1d) < 1e-12 && v.em_1d > 0.005 && v.em_1d < 0.02);
    assert(v.rr25 < 0 && v.put_wing > 0);                                              // downside skew: puts richer than calls
    auto flat = compute_vol_risk(mk(6000, 0.2, 0, 0)); assert(std::fabs(flat.rr25) < 0.01 && std::fabs(flat.term_slope) < 1e-9);
    assert(v.scenarios.size() == 15); for (auto& s : v.scenarios) if (s.shock_pct == 0 && s.dvol == 0) assert(std::fabs(s.dealer_pnl) < 1e-6);
    auto inv = compute_vol_risk(cg, std::nullopt, std::nullopt, true); for (size_t i = 0; i < v.scenarios.size(); ++i) assert(std::fabs(inv.scenarios[i].dealer_pnl + v.scenarios[i].dealer_pnl) < 1e-6 * (1 + std::fabs(v.scenarios[i].dealer_pnl)));
    assert(v.gamma_hhi > 0 && v.gamma_hhi < 1 && v.top3_share > 0 && v.top3_share <= 1 && v.oi_near_share > 0 && v.oi_near_share < 1);
    assert(!compute_vol_risk(ChainSnapshot{}).ok);
    std::puts("test_volrisk OK"); return 0;
}
