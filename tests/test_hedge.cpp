#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/multibook/hedge_analytics.h"
using namespace vt; using namespace vt::mb;
static ChainSnapshot mk(double S, double mult, bool with0dte) {
    ChainSnapshot c; c.underlying = "X"; c.spot = S; c.contract_multiplier = mult; c.risk_free_rate = 0.04; c.dividend_yield = 0.01;
    for (int dte : {0, 1, 7, 30}) { if (dte == 0 && !with0dte) continue; for (int i = -10; i <= 10; ++i) for (auto t : {OptionType::Call, OptionType::Put}) {
        OptionQuote q{}; q.type = t; q.strike = std::round(S * (1 + 0.01 * i)); q.dte_days = dte; q.t_years = dte == 0 ? 0.3 / 365 : dte / 365.0; q.iv = 0.2 + 0.004 * std::abs(i) + (t == OptionType::Put && i < 0 ? 0.02 : 0);
        q.open_interest = 1000 * std::exp(-0.05 * i * i) * (dte == 0 ? 2 : 1); q.mid = 1; c.quotes.push_back(q); } }
    return c; }
int main() {
    auto c = mk(6000, 100, true); auto h = compute_hedge(c); assert(h.ok && h.ladder.size() == 7);
    // consistency with the existing DEX model over all expiries (Term bucket applies a term weight, so compare against a direct recompute instead)
    double manual = 0; for (auto& q : c.quotes) { auto g = bs::greeks(q.type, c.spot, q.strike, c.risk_free_rate, c.dividend_yield, q.iv, q.t_years);
        manual += (q.type == OptionType::Call ? -1.0 : 1.0) * g.delta * q.open_interest * c.contract_multiplier; }
    assert(std::fabs(h.hedge_delta0 - manual) < 1e-6 * std::fabs(manual) + 1e-6);
    assert(std::fabs(h.ladder[3].flow_shares) < 1e-9);                                  // 0% shock => no flow
    // dealers long calls / short puts, mostly symmetric: net long gamma => sell rallies, buy dips (flow opposes the move)
    { auto sm = mk(6000, 100, false); auto hs = compute_hedge(sm); auto gx = model::gex(sm, DteBucket::Term, {}, false); double net = exposure::net(gx);   // smooth chain, small (+/-0.5%) shocks: flow follows the sign of net GEX
      if (net > 0) { assert(hs.ladder[4].flow_shares < 0 && hs.ladder[2].flow_shares > 0); } else { assert(hs.ladder[4].flow_shares > 0 && hs.ladder[2].flow_shares < 0); }
      assert(std::fabs(hs.ladder[6].flow_shares) > 1.5 * std::fabs(hs.ladder[5].flow_shares)); }
    // inverting the convention flips every flow
    exposure::DealerConvention inv; inv.invert = true; auto hi = compute_hedge(c, inv);
    for (size_t i = 0; i < h.ladder.size(); ++i) assert(std::fabs(hi.ladder[i].flow_shares + h.ladder[i].flow_shares) < 1e-6 * (1 + std::fabs(h.ladder[i].flow_shares)));
    // vanna: IV up / down flows are opposite in sign (first order), charm exists, 0DTE share in (0,1)
    assert(h.vanna_up * h.vanna_dn < 0); assert(h.charm_1d != 0); assert(h.zero_dte_gamma_share > 0.05 && h.zero_dte_gamma_share < 1 && h.zero_dte_oi_share > 0);
    assert(h.zero_dte_flow_1pct_up != 0 && h.charm_to_close != 0);
    auto nz = mk(6000, 100, false); auto hn = compute_hedge(nz); assert(hn.zero_dte_gamma_share == 0 && hn.charm_to_close == 0);
    // aggregation is additive: two identical books double the anchor-equivalent contracts
    AggLadder a1 = aggregate_hedge({&h}, 6030.0, 50.0); AggLadder a2 = aggregate_hedge({&h, &h}, 6030.0, 50.0); assert(a2.books == 2);
    for (size_t i = 0; i < a1.anchor_contracts.size(); ++i) assert(std::fabs(a2.anchor_contracts[i] - 2 * a1.anchor_contracts[i]) < 1e-9);
    assert(std::fabs(a1.anchor_contracts[5] - h.ladder[5].flow_notional / (6030 * 50)) < 1e-9);
    assert(aggregate_hedge({}, 6030, 50).books == 0);
    std::puts("test_hedge OK"); return 0;
}
