#pragma once
// vol_risk.h - volatility / options-market / risk module per book (T2-6). Pure functions of one ChainSnapshot (+ optional realized vol).
//   term structure (ATM IV per expiry, slope), skew (25D risk reversal, put-wing slope), IV vs realized (VRP, only when RV is supplied),
//   dealer scenario ladder (full-revaluation option MTM of the dealer book, long calls / short puts), expected-move vs realized move,
//   concentration (|gamma| HHI by strike, top-3 share, OI by expiry share). Nothing here is a prediction; all are measurements.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <vector>
#include "vt/blackscholes.h"
#include "vt/exposure.h"
namespace vt::mb {
struct TermPoint { int dte{}; double atm_iv{}; double rr25{}; };
struct ScenarioPoint { double shock_pct{}; double dvol{}; double dealer_pnl{}; };
struct VolRisk {
    bool ok = false; std::vector<TermPoint> term;
    double term_slope = 0;               // (far ATM IV - near ATM IV) in vol points per sqrt-year of tenor: >0 contango, <0 backwardation / event stress
    bool backwardated = false;
    double rr25 = 0, put_wing = 0;       // 25D RR at ref expiry; put_wing = IV(10% OTM put) - ATM IV (skew steepness)
    std::optional<double> rv, vrp;       // annualised realized vol supplied by caller; vrp = ATM IV(ref) - rv (vol points as fraction)
    double em_1d = 0;                    // 1-sigma one-day move implied by near ATM IV, fraction of spot
    std::optional<double> realized_move, em_ratio;   // |last-day move| / em_1d when supplied (>1 = realised exceeded the priced move)
    std::vector<ScenarioPoint> scenarios;            // dealer MTM $ for spot -2..+2% x IV -2/0/+2 pts (flat 7-point spot ladder at dvol=0 plus vol grid at +/-1%)
    double gamma_hhi = 0, top3_share = 0; double oi_near_share = 0;   // concentration (HHI in [1/n, 1])
    int ref_dte = 0;
};
namespace vr {
inline double atm(const std::vector<const OptionQuote*>& qs, double S) { double best = 1e18; for (auto* q : qs) if (q->iv > 0) best = std::min(best, std::fabs(q->strike - S));
    double s = 0; int n = 0; for (auto* q : qs) if (q->iv > 0 && std::fabs(q->strike - S) <= best + 1e-9) { s += q->iv; ++n; } return n ? s / n : 0; }
inline double dealer_value(const ChainSnapshot& c, double S, double dvol, bool inv) {
    double v = 0; for (auto& q : c.quotes) { if (q.open_interest <= 0 || q.iv <= 0 || q.t_years <= 0) continue; double sg = (q.type == OptionType::Call ? 1.0 : -1.0) * (inv ? -1 : 1);
        v += sg * q.open_interest * c.contract_multiplier * bs::price(q.type, S, q.strike, c.risk_free_rate, c.dividend_yield, std::max(1e-4, q.iv + dvol), q.t_years); }
    return v; }
}
inline VolRisk compute_vol_risk(const ChainSnapshot& c, std::optional<double> rv = std::nullopt, std::optional<double> last_move = std::nullopt, bool invert = false) {
    VolRisk r; if (c.spot <= 0 || c.quotes.empty()) return r; double S = c.spot;
    std::map<int, std::vector<const OptionQuote*>> by; for (auto& q : c.quotes) if (q.dte_days >= 1 && q.t_years > 0) by[q.dte_days].push_back(&q);
    if (by.empty()) return r;
    for (auto& kv : by) { TermPoint t; t.dte = kv.first; t.atm_iv = vr::atm(kv.second, S); if (t.atm_iv <= 0) continue;
        double bp = 1e18, bc = 1e18, ivp = 0, ivc = 0;
        for (auto* q : kv.second) { if (q->iv <= 0) continue; auto g = bs::greeks(q->type, S, q->strike, c.risk_free_rate, c.dividend_yield, q->iv, q->t_years);
            if (q->type == OptionType::Put && std::fabs(g.delta + 0.25) < bp) { bp = std::fabs(g.delta + 0.25); ivp = q->iv; }
            if (q->type == OptionType::Call && std::fabs(g.delta - 0.25) < bc) { bc = std::fabs(g.delta - 0.25); ivc = q->iv; } }
        t.rr25 = (ivp > 0 && ivc > 0) ? ivc - ivp : 0; r.term.push_back(t); }
    if (r.term.empty()) return r;
    auto& n0 = r.term.front(), &nf = r.term.back(); r.ref_dte = n0.dte; for (auto& t : r.term) if (t.dte >= 5) { r.ref_dte = t.dte; break; }
    double dT = std::sqrt(nf.dte / 365.0) - std::sqrt(n0.dte / 365.0); r.term_slope = dT > 1e-9 ? (nf.atm_iv - n0.atm_iv) / dT : 0; r.backwardated = nf.atm_iv < n0.atm_iv - 1e-4 && r.term.size() > 1;
    for (auto& t : r.term) if (t.dte == r.ref_dte) { r.rr25 = t.rr25; double ref = t.atm_iv;
        double bk = 1e18, ivw = 0; for (auto* q : by[t.dte]) if (q->type == OptionType::Put && q->iv > 0 && q->strike > 0) { double d = std::fabs(q->strike - 0.9 * S); if (d < bk) { bk = d; ivw = q->iv; } }
        r.put_wing = ivw > 0 ? ivw - ref : 0; if (rv && *rv > 0) { r.rv = rv; r.vrp = ref - *rv; } }
    r.em_1d = n0.atm_iv * std::sqrt(1.0 / 365.0);
    if (last_move && r.em_1d > 0) { r.realized_move = std::fabs(*last_move); r.em_ratio = std::fabs(*last_move) / r.em_1d; }
    double base = vr::dealer_value(c, S, 0, invert);
    for (double sh : {-2.0, -1.0, 0.0, 1.0, 2.0}) for (double dv : {-0.02, 0.0, 0.02}) r.scenarios.push_back({sh, dv, vr::dealer_value(c, S * (1 + sh / 100), dv, invert) - base});
    std::map<double, double> g; double tot = 0, oi_all = 0, oi_near = 0; int near = r.term.front().dte;
    for (auto& q : c.quotes) { if (q.open_interest <= 0 || q.iv <= 0 || q.t_years <= 0) continue; double gg = std::fabs(bs::greeks(q.type, S, q.strike, c.risk_free_rate, c.dividend_yield, q.iv, q.t_years).gamma * q.open_interest * c.contract_multiplier);
        g[q.strike] += gg; tot += gg; oi_all += q.open_interest; if (q.dte_days <= near) oi_near += q.open_interest; }
    if (tot > 0) { std::vector<double> v; for (auto& kv : g) v.push_back(kv.second / tot); for (double x : v) r.gamma_hhi += x * x; std::sort(v.rbegin(), v.rend()); for (size_t i = 0; i < std::min<size_t>(3, v.size()); ++i) r.top3_share += v[i]; }
    r.oi_near_share = oi_all > 0 ? oi_near / oi_all : 0; r.ok = true; return r;
}
// Caller-supplied realized vol / last-day move attach to an already-computed (cached) VolRisk without recomputation.
inline void apply_realized(VolRisk& r, std::optional<double> rv, std::optional<double> last_move) {
    if (!r.ok) { return; }
    double ref = 0; for (auto& t : r.term) if (t.dte == r.ref_dte) ref = t.atm_iv;
    if (rv && *rv > 0 && ref > 0) { r.rv = rv; r.vrp = ref - *rv; }
    if (last_move && r.em_1d > 0) { r.realized_move = std::fabs(*last_move); r.em_ratio = std::fabs(*last_move) / r.em_1d; } }
}
