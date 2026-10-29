#pragma once
// hedge_analytics.h - dealer hedging flows per book (T2-5): delta ladder, vanna/charm flows, 0DTE share.
// Convention (same as model::dex): dealers LONG calls / SHORT puts; hedge_delta = -sum(pos*OI*mult*delta) so hedge_delta(S0) == net(dex) exactly.
// Flows are re-priced with the existing bs::greeks (full revaluation, not a Taylor approximation), so the ladder is convex/path-correct.
// flow > 0 means dealers BUY the underlying to re-hedge, < 0 means they SELL.
#include <cmath>
#include <vector>
#include "../core/dex.h"
#include "../core/gex.h"
#include "vt/blackscholes.h"
namespace vt::mb {
struct LadderPoint { double shock_pct{}; double flow_shares{}; double flow_notional{}; };
struct HedgeAnalytics {
    bool ok = false; double hedge_delta0 = 0;                 // dealer hedge delta in underlying units at spot (== net DEX)
    std::vector<LadderPoint> ladder;                           // spot shocks -2,-1,-0.5,0,+0.5,+1,+2 %
    double vanna_up = 0, vanna_dn = 0;                         // flow (shares) for IV +1 / -1 vol point, spot unchanged
    double charm_1d = 0, charm_to_close = 0;                   // flow (shares) from one day of time decay / the rest of the session (0DTE only)
    double zero_dte_gamma_share = 0, zero_dte_oi_share = 0;    // 0DTE share of |dollar gamma| and of open interest
    double zero_dte_flow_1pct_up = 0, zero_dte_flow_1pct_dn = 0;
    double notional_per_share = 0;                             // spot (flow_notional = flow * shocked spot)
};
namespace hd {
inline double pos(const OptionQuote& q, bool invert) { double s = q.type == OptionType::Call ? 1.0 : -1.0; return invert ? -s : s; }
// dealer hedge delta (underlying units) for a re-priced chain; dte_filter < 0 = all expiries
inline double hedge_delta(const ChainSnapshot& c, double S, double dvol, double dt_years, bool invert, int dte_filter = -1) {
    double tot = 0;
    for (auto& q : c.quotes) { if (dte_filter >= 0 && q.dte_days != dte_filter) continue; if (q.open_interest <= 0 || q.iv <= 0) continue;
        double t = q.t_years - dt_years; if (t <= 1e-7) t = 1e-7;
        double d = bs::greeks(q.type, S, q.strike, c.risk_free_rate, c.dividend_yield, std::max(1e-4, q.iv + dvol), t).delta;
        tot += -pos(q, invert) * q.open_interest * c.contract_multiplier * d; }
    return tot;
}
}
inline HedgeAnalytics compute_hedge(const ChainSnapshot& c, exposure::DealerConvention conv = {}) {
    HedgeAnalytics h; if (c.spot <= 0 || c.quotes.empty()) return h; h.notional_per_share = c.spot;
    bool inv = conv.invert; double S = c.spot; h.hedge_delta0 = hd::hedge_delta(c, S, 0, 0, inv);
    for (double s : {-2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0}) { double S2 = S * (1 + s / 100); double f = hd::hedge_delta(c, S2, 0, 0, inv) - h.hedge_delta0; h.ladder.push_back({s, f, f * S2}); }
    h.vanna_up = hd::hedge_delta(c, S, +0.01, 0, inv) - h.hedge_delta0; h.vanna_dn = hd::hedge_delta(c, S, -0.01, 0, inv) - h.hedge_delta0;
    h.charm_1d = hd::hedge_delta(c, S, 0, 1.0 / 365.0, inv) - h.hedge_delta0;
    double g0 = 0, gt = 0, o0 = 0, ot = 0;
    for (auto& q : c.quotes) { if (q.open_interest <= 0 || q.iv <= 0 || q.t_years <= 0) continue;
        double g = std::fabs(bs::greeks(q.type, S, q.strike, c.risk_free_rate, c.dividend_yield, q.iv, q.t_years).gamma * q.open_interest * c.contract_multiplier * S * S * 0.01);
        gt += g; ot += q.open_interest; if (q.dte_days == 0) { g0 += g; o0 += q.open_interest; } }
    h.zero_dte_gamma_share = gt > 0 ? g0 / gt : 0; h.zero_dte_oi_share = ot > 0 ? o0 / ot : 0;
    if (o0 > 0) {
        double b = hd::hedge_delta(c, S, 0, 0, inv, 0);
        h.zero_dte_flow_1pct_up = hd::hedge_delta(c, S * 1.01, 0, 0, inv, 0) - b; h.zero_dte_flow_1pct_dn = hd::hedge_delta(c, S * 0.99, 0, 0, inv, 0) - b;
        double rem = 0; for (auto& q : c.quotes) if (q.dte_days == 0) { rem = q.t_years; break; }   // remaining session fraction already encoded in t_years
        h.charm_to_close = hd::hedge_delta(c, S, 0, rem * 0.999, inv, 0) - b;
    }
    h.ok = true; return h;
}
// Aggregate across books in anchor-equivalent contracts: flows are additive in dollars (NOT weighted); only index-convertible books join,
// because a +1% move in the anchor is only a +1% move in books that track it 1:1 (single names / leveraged / vol are excluded, flagged by the caller).
struct AggLadder { std::vector<double> shock, anchor_contracts, notional; double vanna_up_c = 0, vanna_dn_c = 0, charm_1d_c = 0, zero_dte_1pct_up_c = 0, zero_dte_1pct_dn_c = 0; int books = 0; };
inline AggLadder aggregate_hedge(const std::vector<const HedgeAnalytics*>& hs, double anchor_px, double anchor_mult) {
    AggLadder a; double den = anchor_px * anchor_mult; if (hs.empty() || den <= 0) return a;
    for (auto* h : hs) { if (!h || !h->ok) continue; if (a.shock.empty()) { for (auto& p : h->ladder) a.shock.push_back(p.shock_pct); a.notional.assign(a.shock.size(), 0); a.anchor_contracts.assign(a.shock.size(), 0); }
        for (size_t i = 0; i < h->ladder.size(); ++i) a.notional[i] += h->ladder[i].flow_notional;
        double s = h->notional_per_share; a.vanna_up_c += h->vanna_up * s / den; a.vanna_dn_c += h->vanna_dn * s / den; a.charm_1d_c += h->charm_1d * s / den;
        a.zero_dte_1pct_up_c += h->zero_dte_flow_1pct_up * s / den; a.zero_dte_1pct_dn_c += h->zero_dte_flow_1pct_dn * s / den; ++a.books; }
    for (size_t i = 0; i < a.notional.size(); ++i) a.anchor_contracts[i] = a.notional[i] / den;
    return a;
}
}
