#pragma once
// strike_insight.h - the shared Greek / time / regime logic behind the Strike Lens cards (workfile B1-6/B1-7, §7).
// Two horizons from the SAME chain: Intraday (0-1 DTE) and Swinging (swing_min_dte..swing_max_dte). For each structurally important
// strike it measures, by full Black-Scholes revaluation (never a Taylor shortcut), the dealer-signed
//   gamma  ($ per 1% move)            dealer stabilising(+)/amplifying(-)
//   vanna  (shares per +1 vol pt)     dealer hedge flow, + = dealers BUY the underlying
//   charm  (shares per day / to close) dealer hedge flow from time passing alone
//   vomma  ($ vega-per-pt change per +1 vol pt)
//   color  ($/1% gamma change per window: 1 h intraday, 1 day swinging) fade (-) / build (+) of the strike's gamma
// then (finalize) turns them into a verdict, cross-Greek agreement, validity window, invalidation conditions and regime context.
// Convention (same as model::dex/gex): dealers LONG calls / SHORT puts - ASSUMED, not observed. Every conclusion that rests on it says so.
// Metrics depend only on the chain (cacheable per snapshot); finalize adds the live vol regime (VIX/VXN) without recomputation.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include "../core/gex.h"
#include "../core/session.h"
#include "../bridge/json_util.h"
#include "hedge_analytics.h"
namespace vt::mb {
struct InsightParams {
    int intraday_max_dte = 1, swing_min_dte = 2, swing_max_dte = 45;   // expiries feeding each horizon (swing_min_dte=2 so no expiry is orphaned between the tabs)
    int top_n = 6; double min_importance = 0.15;                        // strike must carry >=15% of the horizon's peak |gamma|
    double near_frac = 0.35;                                            // "at the level": within 0.35 x the horizon's 1-sigma move
    double fade_thresh = 0.25;                                          // gamma lost per day (fraction) above which a level is "fading"
    double breach_frac = 0.25;                                          // acceptance beyond the level: 0.25 x 1-sigma move
    double vomma_sens = 0.05;                                           // |vomma/vega| per vol pt above which the level is vol-shock sensitive
};
struct ExpiryInfo { int dte = 0; std::string label; int contracts = 0; double oi_share = 0, gamma_share = 0; };
struct StrikeMetrics {
    double strike = 0, dist_pct = 0, gamma = 0, vanna_up = 0, vanna_dn = 0, charm_day = 0, charm_close = 0, vomma = 0, vega = 0, color = 0, importance = 0, oi = 0;
    double delta_sh = 0, theta_usd = 0, speed = 0, iv_k = 0, skew = 0, p_beyond = 0, p_touch = 0, sigma_dist = 0, gamma_share = 0;   // delta: dealer hedge shares; theta: dealer $/day; speed: gamma change for +1% spot; skew: IV(K)-ATM
    double color_win_min = 1440; bool has_close = false; double half_life_min = -1, iv_flip_pts = 0; int dom_dte = 0; double oi_share = 0; int contracts = 0;
};
struct HorizonMetrics {
    std::string name; bool ok = false; std::string reason; int dte_lo = 0, dte_hi = 0, min_dte = 0;
    std::vector<ExpiryInfo> expiries; double net_gamma = 0, em_pct = 0, atm_iv = 0; std::optional<double> flip;
    std::vector<StrikeMetrics> cards; std::map<double, double> gamma_by_strike; double peak = 0, t_prob = 0, gamma_total = 0; std::vector<double> sig_strikes;   // t_prob: horizon time (years) used for probabilities; sig_strikes: every structurally important strike
};
struct InsightMetrics { double spot = 0; sess::Session ses; HorizonMetrics intraday, swinging; };
namespace si {
using Q = const OptionQuote*;
struct Ctx { const ChainSnapshot* c; bool inv; };
inline double gam(const Ctx& x, const std::vector<Q>& qs, double S, double dvol, double dt) {
    double tot = 0; for (auto* q : qs) { double t = q->t_years - dt; if (t <= 1e-7) continue;                   // expired contracts carry no gamma
        double g = bs::greeks(q->type, S, q->strike, x.c->risk_free_rate, x.c->dividend_yield, std::max(1e-4, q->iv + dvol), t).gamma;
        tot += hd::pos(*q, x.inv) * g * q->open_interest * x.c->contract_multiplier * S * S * 0.01; }
    return tot; }
inline double flow(const Ctx& x, const std::vector<Q>& qs, double S, double dvol, double dt) {                  // hedge-delta change vs now; + = dealers buy
    double a = 0, b = 0; for (auto* q : qs) { auto pos = hd::pos(*q, x.inv); double m = -pos * q->open_interest * x.c->contract_multiplier;
        double t1 = std::max(1e-7, q->t_years - dt);
        b += m * bs::greeks(q->type, S, q->strike, x.c->risk_free_rate, x.c->dividend_yield, std::max(1e-4, q->iv + dvol), t1).delta;
        a += m * bs::greeks(q->type, S, q->strike, x.c->risk_free_rate, x.c->dividend_yield, q->iv, std::max(1e-7, q->t_years)).delta; }
    return b - a; }
inline double delta_sh(const Ctx& x, const std::vector<Q>& qs, double S) { double t = 0; for (auto* q : qs) t += -hd::pos(*q, x.inv) * q->open_interest * x.c->contract_multiplier * bs::greeks(q->type, S, q->strike, x.c->risk_free_rate, x.c->dividend_yield, q->iv, std::max(1e-7, q->t_years)).delta; return t; }
inline double theta_usd(const Ctx& x, const std::vector<Q>& qs, double S) { double t = 0; for (auto* q : qs) t += hd::pos(*q, x.inv) * q->open_interest * x.c->contract_multiplier * bs::greeks(q->type, S, q->strike, x.c->risk_free_rate, x.c->dividend_yield, q->iv, std::max(1e-7, q->t_years)).theta; return t; }
inline double vega_pt(const Ctx& x, const std::vector<Q>& qs, double S, double dvol) {                          // dealer vega $ per vol point
    double tot = 0; for (auto* q : qs) { if (q->t_years <= 1e-7) continue; tot += hd::pos(*q, x.inv) * q->open_interest * x.c->contract_multiplier * 0.01 *
        bs::greeks(q->type, S, q->strike, x.c->risk_free_rate, x.c->dividend_yield, std::max(1e-4, q->iv + dvol), q->t_years).vega; }
    return tot; }
}
inline HorizonMetrics compute_horizon(const ChainSnapshot& c, const sess::Session& ses, exposure::DealerConvention conv, const InsightParams& P, const char* name, int lo, int hi) {
    HorizonMetrics H; H.name = name; H.dte_lo = lo; H.dte_hi = hi; si::Ctx x{&c, conv.invert}; const double S = c.spot;
    std::map<double, std::vector<si::Q>> by_k; std::vector<si::Q> all; std::map<int, std::vector<si::Q>> by_e;
    for (auto& q : c.quotes) { if (q.dte_days < lo || q.dte_days > hi || q.open_interest <= 0 || q.iv <= 0 || q.t_years <= 0) continue; by_k[q.strike].push_back(&q); all.push_back(&q); by_e[q.dte_days].push_back(&q); }
    if (all.empty() || S <= 0) { H.reason = std::string("no ") + (lo == 0 ? "0DTE/1DTE" : std::to_string(lo) + "-" + std::to_string(hi) + " DTE") + " expiries with open interest in this chain"; return H; }
    H.ok = true; H.min_dte = by_e.begin()->first; double oi_tot = 0, g_tot = 0; for (auto* q : all) oi_tot += q->open_interest;
    for (auto& e : by_e) { ExpiryInfo ei; ei.dte = e.first; ei.label = sess::expiry_label(ses, e.first); ei.contracts = (int)e.second.size(); double oi = 0, ga = 0;
        for (auto* q : e.second) { oi += q->open_interest; ga += std::fabs(bs::greeks(q->type, S, q->strike, c.risk_free_rate, c.dividend_yield, q->iv, q->t_years).gamma * q->open_interest * c.contract_multiplier * S * S * 0.01); }
        ei.oi_share = oi_tot > 0 ? oi / oi_tot : 0; ei.gamma_share = ga; g_tot += ga; H.expiries.push_back(ei); }
    for (auto& e : H.expiries) e.gamma_share = g_tot > 0 ? e.gamma_share / g_tot : 0;
    // horizon 1-sigma move from the ATM IV of the first expiry in the horizon
    { double best = 1e18, iv = 0; int n = 0; for (auto* q : by_e.begin()->second) best = std::min(best, std::fabs(q->strike - S));
      for (auto* q : by_e.begin()->second) if (std::fabs(q->strike - S) <= best + 1e-9) { iv += q->iv; ++n; }
      H.atm_iv = n ? iv / n : 0; H.em_pct = H.atm_iv * std::sqrt(std::max(1, H.min_dte) / 365.0); }
    H.t_prob = (lo == 0 && ses.rth) ? std::max(ses.to_close, 5) / 525600.0 : std::max(1, H.min_dte) / 365.0;
    H.net_gamma = si::gam(x, all, S, 0, 0);
    // flip: nearest sign change of the horizon's net gamma over a +-6% spot grid
    { double prev = 0, prevS = 0; bool have = false; double bestd = 1e18;
      for (int i = -24; i <= 24; ++i) { double S2 = S * (1 + 0.0025 * i), g = si::gam(x, all, S2, 0, 0);
          if (have && ((prev < 0 && g > 0) || (prev > 0 && g < 0))) { double z = prevS + (S2 - prevS) * (0 - prev) / (g - prev); if (std::fabs(z - S) < bestd) { bestd = std::fabs(z - S); H.flip = z; } }
          prev = g; prevS = S2; have = true; } }
    for (auto& kv : by_k) { double g = si::gam(x, kv.second, S, 0, 0); H.gamma_by_strike[kv.first] = g; H.peak = std::max(H.peak, std::fabs(g)); }
    for (auto& kv : H.gamma_by_strike) H.gamma_total += std::fabs(kv.second);
    for (auto& kv : H.gamma_by_strike) if (H.peak > 0 && std::fabs(kv.second) >= P.min_importance * H.peak) H.sig_strikes.push_back(kv.first);
    std::vector<std::pair<double, double>> rank; for (auto& kv : H.gamma_by_strike) rank.push_back({std::fabs(kv.second), kv.first});
    std::sort(rank.rbegin(), rank.rend());
    for (auto& rk : rank) { if ((int)H.cards.size() >= P.top_n) break; double imp = H.peak > 0 ? rk.first / H.peak : 0; if (imp < P.min_importance) break;
        double K = rk.second; auto& qs = by_k[K]; StrikeMetrics m; m.strike = K; m.dist_pct = (K - S) / S * 100; m.importance = imp; m.gamma = H.gamma_by_strike[K];
        m.vanna_up = si::flow(x, qs, S, +0.01, 0); m.vanna_dn = si::flow(x, qs, S, -0.01, 0); m.charm_day = si::flow(x, qs, S, 0, 1.0 / 365.0);
        if (ses.rth) { m.has_close = true; m.charm_close = si::flow(x, qs, S, 0, ses.to_close / 525600.0); }
        m.vega = si::vega_pt(x, qs, S, 0); m.vomma = si::vega_pt(x, qs, S, +0.01) - m.vega; m.color_win_min = lo == 0 ? 60.0 : 1440.0; m.color = si::gam(x, qs, S, 0, m.color_win_min / 525600.0) - m.gamma;   // fade window: 1 h intraday, 1 day swinging
        m.delta_sh = si::delta_sh(x, qs, S); m.theta_usd = si::theta_usd(x, qs, S); m.speed = si::gam(x, qs, S * 1.01, 0, 0) - m.gamma; m.gamma_share = H.gamma_total > 0 ? std::fabs(m.gamma) / H.gamma_total : 0;
        { double iv = 0; for (auto* q : qs) iv += q->iv; iv /= qs.size(); m.iv_k = iv; m.skew = iv - H.atm_iv; double sg = std::max(1e-4, H.atm_iv) * std::sqrt(H.t_prob); m.sigma_dist = std::fabs(std::log(K / S)) / sg;
          m.p_beyond = bs::norm_cdf(-m.sigma_dist); m.p_touch = std::min(1.0, 2 * m.p_beyond); }   // risk-neutral, lognormal at ATM vol; touch ~ 2 x finish-beyond (reflection principle, approximate)
        double oi = 0; std::map<int, double> ge; for (auto* q : qs) { oi += q->open_interest; ge[q->dte_days] += std::fabs(si::gam(x, {q}, S, 0, 0)); } m.oi = oi; m.oi_share = oi_tot > 0 ? oi / oi_tot : 0; m.contracts = (int)qs.size();
        double bg = -1; for (auto& e : ge) if (e.second > bg) { bg = e.second; m.dom_dte = e.first; }
        for (double mins : {15.0, 30.0, 60.0, 120.0, 180.0, 240.0, 390.0, 1440.0, 2880.0, 4320.0, 7200.0, 10080.0, 14400.0, 20160.0, 30240.0}) {
            if (std::fabs(si::gam(x, qs, S, 0, mins / 525600.0)) < 0.5 * std::fabs(m.gamma)) { m.half_life_min = mins; break; } }
        for (int i = 1; i <= 10 && m.iv_flip_pts == 0; ++i) for (int sg : {1, -1}) { double g2 = si::gam(x, qs, S, sg * 0.01 * i, 0);
            if (g2 * m.gamma <= 0 || std::fabs(g2) < 0.5 * std::fabs(m.gamma)) { m.iv_flip_pts = sg * i; break; } }
        H.cards.push_back(m); }
    return H;
}
inline InsightMetrics compute_insight(const ChainSnapshot& c, exposure::DealerConvention conv = {}, const InsightParams& P = {}) {
    InsightMetrics M; M.spot = c.spot; M.ses = sess::from_ts(c.ts);
    M.intraday = compute_horizon(c, M.ses, conv, P, "Intraday", 0, P.intraday_max_dte);
    M.swinging = compute_horizon(c, M.ses, conv, P, "Swinging", P.swing_min_dte, P.swing_max_dte);
    return M;
}
// ---------------------------------------------------------------- finalize: verdicts, agreement, validity, invalidation, regime
struct VolIn { bool available = false; std::string id, regime = "unknown", reason; int trend = 0; double level = 0, pct_rank = 0, chg_1d = 0; };
struct Vote { std::string greek, text; int v = 0; };       // v: +1 supports the verdict, -1 conflicts, 0 no vote (stated why)
struct Playbook { std::string style, hold, entry, stop, target, hold_potential, speed; double entry_px = 0, stop_px = 0, target_px = 0, rr = 0, p_target = 0, capture_pts = 0, capture_pct = 0; bool has_target = false; std::vector<std::string> edge; };
struct Card {
    Playbook pb;
    StrikeMetrics m; std::string verdict, header, mechanism, approach, breach, validity_until, validity_limit, regime_note, conviction, confidence, as_of;
    bool contested = false; int agree = 0, conflict = 0; std::vector<Vote> votes; std::vector<std::string> invalidation;
};
struct Horizon {
    std::string name, as_of, regime_headline, expiries_text; bool ok = false; std::string reason; std::vector<ExpiryInfo> expiries;
    double net_gamma = 0, em_pct = 0; std::optional<double> flip; std::string flip_text, vol_text, session_text, assumption; std::vector<Card> cards;
};
struct Insight { Horizon intraday, swinging; std::vector<std::string> reconcile; double spot = 0; std::string as_of; };
namespace sf {
inline std::string money(double v) { std::ostringstream o; o.setf(std::ios::fixed); o.precision(std::fabs(v) < 100 ? 2 : 0); o << "$" << v; return o.str(); }
inline std::string abbr(double v) { double a = std::fabs(v); std::ostringstream o; o.setf(std::ios::fixed); const char* sg = v < 0 ? "-" : "+";
    if (a >= 1e9) { o.precision(2); o << sg << "$" << a / 1e9 << "B"; } else if (a >= 1e6) { o.precision(1); o << sg << "$" << a / 1e6 << "M"; } else if (a >= 1e3) { o.precision(0); o << sg << "$" << a / 1e3 << "K"; } else { o.precision(0); o << sg << "$" << a; } return o.str(); }
inline std::string sh(double v) { double a = std::fabs(v); std::ostringstream o; o.setf(std::ios::fixed); const char* sg = v < 0 ? "-" : "+"; if (a >= 1e6) { o.precision(2); o << sg << a / 1e6 << "M"; } else if (a >= 1e3) { o.precision(1); o << sg << a / 1e3 << "K"; } else { o.precision(0); o << sg << a; } return o.str() + " sh"; }
inline std::string dur(double mins) { std::ostringstream o; if (mins < 90) o << (int)std::lround(mins) << " min"; else if (mins < 2880) { o.setf(std::ios::fixed); o.precision(1); o << mins / 60 << " h"; } else o << (int)std::lround(mins / 1440) << " d"; return o.str(); }
inline std::string when(const sess::Session& now, double add_min) {
    auto e = sess::from_utc(now.utc + (long long)(add_min * 60)); if (e.year == now.year && e.month == now.month && e.day == now.day) return sess::hhmm(e.et_min) + " ET";
    return std::string(sess::dow_name(e.weekday)) + " " + std::to_string(e.day) + " " + sess::mon_name(e.month) + " " + sess::hhmm(e.et_min) + " ET"; }
}
inline Card build_card(const StrikeMetrics& m, const HorizonMetrics& H, const sess::Session& ses, double S, const VolIn& V, const InsightParams& P) {
    Card c; c.m = m; c.as_of = ses.as_of; const double G = m.gamma, sgG = G >= 0 ? 1.0 : -1.0, em = H.em_pct * S, band = P.near_frac * em; const double dirK = m.strike > S ? 1.0 : m.strike < S ? -1.0 : 0.0;
    const bool near = std::fabs(m.strike - S) <= band; const std::string win = m.color_win_min < 1440 ? "hour" : "day"; const double fade = std::fabs(G) > 0 ? -m.color * sgG / std::fabs(G) : 0;   // + = losing gamma per day
    enum { MAGNET, SUP, RES, ACC, PIV, WEAK } v;
    if (G > 0) v = fade >= P.fade_thresh ? WEAK : near ? MAGNET : (dirK > 0 ? RES : SUP); else v = near ? PIV : ACC;
    static const char* nm[] = {"Magnet", "Support", "Resistance", "Acceleration", "Pivot", "Weak"}; c.verdict = nm[v];
    // ---- mechanism (plain language; sign convention stated)
    c.mechanism = G > 0 ? "Dealers (assumed long calls / short puts) are net LONG gamma at " + sf::money(m.strike) + " (" + sf::abbr(G) + " per 1%): they sell rallies and buy dips around it, which damps moves."
                        : "Dealers (assumed long calls / short puts) are net SHORT gamma at " + sf::money(m.strike) + " (" + sf::abbr(G) + " per 1%): they must sell into declines and buy into rallies, which amplifies moves.";
    switch (v) {
        case MAGNET: c.approach = "Price is within " + sf::money(band) + " (0.35 x 1-sigma move) of the strike: expect it to be drawn toward and pin near " + sf::money(m.strike) + " while this gamma persists."; c.breach = "A move beyond the band is met by hedging back toward the strike until the gamma fades or expires."; break;
        case SUP: c.approach = "A dip into " + sf::money(m.strike) + " meets dealer buying as hedges unwind: expect a bounce."; c.breach = "Acceptance below it (especially as gamma fades) opens the way to the next structural strike below."; break;
        case RES: c.approach = "A rally into " + sf::money(m.strike) + " meets dealer selling: expect rejection."; c.breach = "Acceptance above it (especially as gamma fades) opens the way to the next structural strike above."; break;
        case ACC: c.approach = "Approaching " + sf::money(m.strike) + " dealers are on the wrong side of gamma: moves into it tend to extend, not stall."; c.breach = "A break forces dealers to chase: expect faster, extended movement in the direction of the break."; break;
        case PIV: c.approach = "Price is at a short-gamma strike: hedging flips from damping to chasing around " + sf::money(m.strike) + "."; c.breach = "Expect fast two-way moves; the side that breaks first sets direction."; break;
        default: c.approach = "The strike still holds positive gamma, but it is losing about " + std::to_string((int)std::lround(fade * 100)) + "% of it per " + win + " (color)."; c.breach = "The level is likely to give way as time passes; do not rely on it for rejection."; }
    // ---- votes
    auto toward = [&](double flow) { return flow != 0 && dirK != 0 && ((flow > 0) == (dirK > 0)); };
    auto sup_if_toward = [&](bool tw) { return (v == SUP || v == RES) ? (tw ? -1 : 1) : (tw ? 1 : -1); };
    c.votes.push_back({"Gamma", (G > 0 ? "positive: stabilising" : "negative: amplifying") + std::string(" (") + sf::abbr(G) + "/1%)", G > 0 ? (v == ACC || v == PIV ? -1 : 1) : (v == ACC || v == PIV ? 1 : -1)});
    { Vote x{"Color", "", 0}; if (std::fabs(fade) < 0.05) x.text = "gamma roughly stable (" + sf::abbr(m.color) + "/1% per " + win + ")"; else if (fade > 0) { x.text = "gamma fading " + std::to_string((int)std::lround(fade * 100)) + "% per " + win + " (" + sf::abbr(m.color) + "/1%)"; x.v = (v == ACC || v == PIV || v == WEAK) ? 1 : -1; }
      else { x.text = "gamma building " + std::to_string((int)std::lround(-fade * 100)) + "% per " + win; x.v = (v == ACC || v == PIV || v == WEAK) ? -1 : 1; } c.votes.push_back(x); }
    { Vote x{"Charm", "", 0}; if (dirK == 0 || m.charm_day == 0) x.text = "no directional time-decay flow"; else { bool tw = toward(m.charm_day); x.v = sup_if_toward(tw);
        x.text = "time alone makes dealers " + std::string(m.charm_day > 0 ? "buy " : "sell ") + sf::sh(std::fabs(m.charm_day)).substr(1) + "/day, " + (tw ? "toward" : "away from") + " the strike" + (m.has_close ? "; to the close " + sf::sh(m.charm_close) : ""); } c.votes.push_back(x); }
    { Vote x{"Vanna", "", 0}; if (V.available && V.trend != 0 && dirK != 0) { double fl = V.trend > 0 ? m.vanna_up : m.vanna_dn; bool tw = toward(fl); x.v = fl == 0 ? 0 : sup_if_toward(tw);
        x.text = V.id + " is " + (V.trend > 0 ? "rising" : "falling") + ": a 1-pt vol move makes dealers " + (fl > 0 ? "buy " : "sell ") + sf::sh(std::fabs(fl)).substr(1) + ", " + (tw ? "toward" : "away from") + " the strike"; }
      else { x.text = "conditional: " + sf::sh(m.vanna_up) + " if vol +1 pt / " + sf::sh(m.vanna_dn) + " if vol -1 pt" + (V.available ? " (vol direction flat)" : " (no vol index feed: direction unknown)"); }
      c.votes.push_back(x); }
    { Vote x{"Vomma", "", 0}; double r = std::fabs(m.vega) > 0 ? m.vomma / std::fabs(m.vega) : 0; x.text = "vega changes " + sf::abbr(m.vomma) + " per +1 vol pt (" + std::to_string((int)std::lround(r * 100)) + "% of " + sf::abbr(m.vega) + ")" + (std::fabs(r) > P.vomma_sens ? ": vol-shock sensitive, conviction depends on vol staying put" : ": low sensitivity"); c.votes.push_back(x); }
    for (auto& x : c.votes) { if (x.v > 0) ++c.agree; else if (x.v < 0) ++c.conflict; }
    int n = c.agree + c.conflict; c.contested = n >= 3 && c.conflict >= c.agree;
    c.conviction = n < 2 ? "low (too few Greeks vote)" : c.contested ? "low: Greeks conflict (" + std::to_string(c.agree) + " agree / " + std::to_string(c.conflict) + " conflict)" : c.conflict == 0 ? "high: all voting Greeks agree" : c.agree > c.conflict ? "medium: " + std::to_string(c.agree) + " agree / " + std::to_string(c.conflict) + " conflict" : "low: split";
    c.confidence = (m.oi_share < 0.02 || m.contracts < 2) ? "low (thin open interest at this strike)" : c.contested ? "low" : (c.conflict == 0 && n >= 3) ? "high" : "medium";
    c.header = sf::money(m.strike) + "  " + sf::abbr(G) + "/1%  " + c.verdict + (c.contested ? " (contested)" : "");
    // ---- validity from data: min(gamma half-life, expiry of the expiry that carries the strike's gamma)
    double to_exp = (ses.rth ? ses.to_close : std::max(0, sess::CLOSE_MIN - ses.et_min)) + m.dom_dte * 1440.0, until = to_exp; c.validity_limit = "expiry of " + sess::expiry_label(ses, m.dom_dte);
    if (m.half_life_min > 0 && m.half_life_min < to_exp) { until = m.half_life_min; c.validity_limit = "gamma loses half its size in " + sf::dur(m.half_life_min); }
    c.validity_until = sf::when(ses, until);
    // ---- invalidation
    double d = P.breach_frac * em;
    if (near) c.invalidation.push_back("Price accepted beyond " + sf::money(m.strike - d) + " or " + sf::money(m.strike + d) + " (+-0.25 x 1-sigma move)."); else if (dirK > 0) c.invalidation.push_back("Price accepted above " + sf::money(m.strike + d) + " (0.25 x 1-sigma move past the strike)."); else c.invalidation.push_back("Price accepted below " + sf::money(m.strike - d) + " (0.25 x 1-sigma move past the strike).");
    if (m.iv_flip_pts != 0) c.invalidation.push_back("An IV move of " + std::string(m.iv_flip_pts > 0 ? "+" : "") + std::to_string((int)m.iv_flip_pts) + " vol pt" + (std::fabs(m.iv_flip_pts) > 1 ? "s" : "") + " halves or flips the gamma here."); else c.invalidation.push_back("No IV move within +-10 vol pts halves or flips this gamma.");
    c.invalidation.push_back("Time: after " + c.validity_until + " (" + c.validity_limit + ") this reading no longer holds; re-derive on the next snapshot.");
    if (v == WEAK || fade >= P.fade_thresh) c.invalidation.push_back("Gamma decay continues: at about " + std::to_string((int)std::lround(fade * 100)) + "% per " + win + " the level is gone within " + (m.half_life_min > 0 ? sf::dur(m.half_life_min) : std::string("the horizon")) + ".");
    // ---- regime note
    std::string rn = H.net_gamma >= 0 ? "Horizon net gamma is POSITIVE (" + sf::abbr(H.net_gamma) + "): dealers damp moves overall, so level-holding behaviour is favoured." : "Horizon net gamma is NEGATIVE (" + sf::abbr(H.net_gamma) + "): dealers amplify moves overall, so levels give way more easily.";
    if (H.flip) rn += " Gamma flip " + sf::money(*H.flip) + " (" + (*H.flip > S ? "+" : "") + std::to_string((int)std::lround((*H.flip - S) / S * 10000) / 100.0).substr(0, 5) + "% from spot).";
    if (V.available && V.regime != "unknown") rn += " " + V.id + " regime: " + V.regime + (V.regime == "elevated" || V.regime == "stress" ? " - zones widen, vanna/vomma flows are larger." : V.regime == "compressed" ? " - a vol spike would force outsized re-hedging." : "."); else rn += V.available ? " Vol regime withheld (short history)." : " Vol regime unavailable (no volatility feed).";
    if (H.min_dte == 0 && ses.rth) rn += " 0DTE expires in " + sf::dur(ses.to_close) + "; session phase: " + sess::phase_name(ses.phase) + ".";
    c.regime_note = rn;
    // ---------------- trade playbook: derived from the Greek profile + risk-neutral distribution; a framing of how the level tends to trade, not a signal
    Playbook& pb = c.pb; const double sig = std::max(1e-4, H.atm_iv) * std::sqrt(H.t_prob) * S;   // 1-sigma move in points over the probability horizon
    auto next_beyond = [&](double from, double dir) -> std::optional<double> { std::optional<double> b; for (double k : H.sig_strikes) { if (dir > 0 && k > from + 1e-9 && (!b || k < *b)) b = k; if (dir < 0 && k < from - 1e-9 && (!b || k > *b)) b = k; } return b; };
    const bool breakout = (v == ACC || v == PIV || v == WEAK), reject = (v == SUP || v == RES);
    double dir = 0, entry = m.strike;
    if (breakout) dir = (v == PIV) ? (m.charm_day != 0 ? (m.charm_day > 0 ? 1 : -1) : 1) : (dirK != 0 ? dirK : (m.charm_day >= 0 ? 1 : -1));
    else if (reject) dir = (v == RES) ? -1 : 1;
    else { entry = S; dir = m.strike > S ? 1 : -1; }
    std::optional<double> nb = breakout ? next_beyond(m.strike, dir) : reject ? next_beyond(m.strike, dir) : std::optional<double>(m.strike);
    pb.entry_px = entry; pb.has_target = true; pb.target_px = nb ? *nb : (breakout ? m.strike + dir * em : m.strike + dir * 0.5 * em);
    pb.stop_px = (breakout || reject) ? m.strike - dir * d : S - dir * d; double sd = std::max(d, 1e-9);
    pb.capture_pts = std::fabs(pb.target_px - entry); pb.capture_pct = pb.capture_pts / S * 100; pb.rr = pb.capture_pts / sd;
    pb.p_target = sig > 0 ? std::min(1.0, 2 * bs::norm_cdf(-pb.capture_pts / sig)) : 0;
    const bool low_conf = c.confidence.rfind("low", 0) == 0, win_intraday = H.dte_lo == 0;
    int fast = (m.importance >= 0.6) + near + (fade < 0) + (c.votes[2].v > 0) + (c.votes[3].v > 0);
    std::string tgt_src = nb && !(v == MAGNET) ? "the next structural strike" : (v == MAGNET ? "the strike itself" : "a projection (no further structural strike): " + std::string(breakout ? "1" : "0.5") + " x 1-sigma move");
    if (c.contested || low_conf) { pb.style = "Stand aside / wait for confirmation"; pb.hold = "No hold until the Greeks agree"; pb.entry = "Greeks conflict or data is thin: let price prove the level (acceptance beyond it, or a clean rejection) before committing; if trading, reduce size."; pb.speed = "n/a"; }
    else if (breakout) { pb.style = v == PIV ? "Momentum entry: trade the side that breaks first" : v == WEAK ? "Momentum entry through a weak level (do not fade it)" : "Momentum entry: break and hold";
        pb.entry = std::string("Enter after price is ACCEPTED ") + (dir > 0 ? "above " : "below ") + sf::money(m.strike + dir * d) + " (0.25 x 1-sigma beyond the level), not on the first tick: dealers chase the break, so follow-through is the edge."; pb.speed = "fast follow-through once accepted";
        pb.hold = win_intraday ? "Hold while price stays beyond the level, no later than " + c.validity_until : "Hold multi-day toward the target; reassess if the level's validity ends (" + c.validity_until + ")"; }
    else if (reject) { bool fastr = fast >= 3; pb.style = fastr ? "Fast rejection: fade the first touch" : "Slow rejection: wait for the retest";
        pb.entry = fastr ? std::string("Fade the first touch of ") + sf::money(m.strike) + ": gamma is strong and close, so the reaction tends to be immediate; waiting risks no retest." : std::string("Wait for a failed push through ") + sf::money(m.strike) + " and a holding retest, then fade; the reaction tends to grind, so patience beats front-running.";
        pb.speed = fastr ? "fast (sharp reversal)" : "slow (grind / retest)"; pb.hold = fastr ? "Scalp: take profit into the first reaction; hold only while price keeps moving away" : (win_intraday ? "Hold toward the target with a time stop at " + c.validity_until : "Hold toward the target over days; time stop at " + c.validity_until); }
    else { pb.style = "Pin / mean reversion toward the strike"; pb.entry = "Trade toward " + sf::money(m.strike) + " from a deviation and exit AT it; do not chase breakouts away from it while this gamma persists."; pb.speed = "slow (pin)"; pb.hold = "Short: the pin pays little beyond the strike; exit by " + c.validity_until; }
    pb.stop = "Stop " + sf::money(pb.stop_px) + " (" + sf::money(d) + " = 0.25 x 1-sigma beyond the level): the same price that invalidates the reading.";
    pb.target = "Target " + sf::money(pb.target_px) + " (" + tgt_src + "): +" + sf::money(pb.capture_pts) + " = " + std::to_string((int)std::lround(pb.capture_pct * 100) / 100.0).substr(0, 4) + "% for a " + std::to_string((int)std::lround(pb.rr * 10) / 10.0).substr(0, 3) + "R risk/reward; risk-neutral chance of reaching it within the horizon about " + std::to_string((int)std::lround(pb.p_target * 100)) + "%.";
    pb.hold_potential = (c.contested || low_conf) ? "low (unconfirmed)" : (pb.rr >= 2 && pb.p_target >= 0.25) ? "high" : pb.rr >= 1 ? "medium" : "low";
    // own insights: concentration, distribution, skew, speed, regime filter
    pb.edge.push_back("Pin strength: this strike holds " + std::to_string((int)std::lround(m.gamma_share * 100)) + "% of the horizon's gamma" + (m.gamma_share >= 0.15 ? " (a dominant strike)." : "."));
    pb.edge.push_back("Distribution: " + std::to_string((int)std::lround(m.sigma_dist * 100) / 100.0).substr(0, 4) + " sigma away; risk-neutral chance of finishing beyond it " + std::to_string((int)std::lround(m.p_beyond * 100)) + "%, of touching it about " + std::to_string((int)std::lround(m.p_touch * 100)) + "% (approximation, at ATM vol).");
    if (std::fabs(m.skew) >= 0.005) pb.edge.push_back("Risk pricing: IV at this strike is " + std::to_string(std::fabs(m.skew) * 100).substr(0, 4) + " vol pts " + (m.skew > 0 ? "above" : "below") + " ATM: " + (m.skew > 0 && m.strike < S ? "downside protection is bid here (hedging demand)." : m.skew > 0 ? "upside is priced richer here." : "the market is paying less for this wing."));
    pb.edge.push_back("Speed: gamma changes by " + sf::abbr(m.speed) + " for a +1% move, so the level " + ((m.speed > 0) == (m.gamma > 0) ? "strengthens as price rises and weakens as it falls." : "weakens as price rises and strengthens as it falls."));
    pb.edge.push_back("Delta / theta: dealers' hedge here is " + sf::sh(m.delta_sh) + " (+ = long underlying) and their option theta is " + sf::abbr(m.theta_usd) + " per day.");
    pb.edge.push_back(H.net_gamma < 0 ? "Regime filter: negative net gamma favours momentum / breakout entries over fades." : "Regime filter: positive net gamma favours fades and mean reversion over chasing breaks.");
    if (V.available && (V.regime == "elevated" || V.regime == "stress")) pb.edge.push_back(V.id + " " + V.regime + ": expect noisier prints through the level; size down or use at least 1.5 x the stop distance.");
    if (V.available && V.regime == "compressed") pb.edge.push_back(V.id + " compressed: a vol expansion would force outsized re-hedging; breakouts can be sharper than the numbers imply.");
    if (m.has_close && ses.to_close <= 90) pb.edge.push_back("Into the close: charm will push dealers " + std::string(m.charm_close > 0 ? "to buy " : "to sell ") + sf::sh(std::fabs(m.charm_close)).substr(1) + " by 16:00; expect that drift to dominate in the final hour.");
    return c;
}
inline Horizon finalize_horizon(const HorizonMetrics& H, const sess::Session& ses, double S, const VolIn& V, const InsightParams& P) {
    Horizon o; o.name = H.name; o.ok = H.ok; o.reason = H.reason; o.expiries = H.expiries; o.net_gamma = H.net_gamma; o.em_pct = H.em_pct; o.flip = H.flip; o.as_of = ses.as_of; o.assumption = "Dealers assumed long calls / short puts (not observed).";
    if (!H.ok) return o;
    std::string ex; for (size_t i = 0; i < H.expiries.size(); ++i) { auto& e = H.expiries[i]; ex += (i ? ", " : "") + std::to_string(e.dte) + "DTE " + e.label + " (" + std::to_string((int)std::lround(e.oi_share * 100)) + "% OI)"; } o.expiries_text = ex;
    o.flip_text = H.flip ? "gamma flip " + sf::money(*H.flip) : "no gamma flip within +-6%";
    o.vol_text = V.available ? V.id + " " + (V.regime == "unknown" ? "regime withheld" : V.regime) + (V.trend > 0 ? ", rising" : V.trend < 0 ? ", falling" : ", flat") : "vol index unavailable";
    o.session_text = std::string(sess::phase_name(ses.phase)) + (ses.rth ? ", " + sf::dur(ses.to_close) + " to close" : "");
    o.regime_headline = (H.net_gamma >= 0 ? "Positive-gamma " : "Negative-gamma ") + std::string("regime (") + sf::abbr(H.net_gamma) + "/1%), 1-sigma " + std::to_string((int)std::lround(H.em_pct * 10000) / 100.0).substr(0, 4) + "%, " + o.flip_text + ", " + o.vol_text + ", " + o.session_text + ".";
    for (auto& m : H.cards) { o.cards.push_back(build_card(m, H, ses, S, V, P)); }
    return o;
}
inline Insight finalize(const InsightMetrics& M, const VolIn& V = {}, const InsightParams& P = {}) {
    Insight I; I.spot = M.spot; I.as_of = M.ses.as_of; I.intraday = finalize_horizon(M.intraday, M.ses, M.spot, V, P); I.swinging = finalize_horizon(M.swinging, M.ses, M.spot, V, P);
    // consistency between tabs: a strike important in one horizon whose gamma has the opposite sign in the other must be explained
    auto cmp = [&](const HorizonMetrics& A, const HorizonMetrics& B) { if (!A.ok || !B.ok) return;
        for (auto& m : A.cards) { auto it = B.gamma_by_strike.find(m.strike); if (it == B.gamma_by_strike.end() || B.peak <= 0 || std::fabs(it->second) < P.min_importance * B.peak) continue;
            if ((m.gamma > 0) != (it->second > 0)) I.reconcile.push_back(sf::money(m.strike) + ": " + A.name + " gamma " + sf::abbr(m.gamma) + " (" + (m.gamma > 0 ? "stabilising" : "amplifying") + ") vs " + B.name + " " + sf::abbr(it->second) + " (" + (it->second > 0 ? "stabilising" : "amplifying") + "). The horizons differ because " + A.name + " positioning expires first (" + (A.name == "Intraday" ? "0-1DTE" : "further-dated") + "); after it expires only " + B.name + " hedging remains."); } };
    cmp(M.intraday, M.swinging); cmp(M.swinging, M.intraday); return I;
}
inline std::string insight_json(const Insight& I) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); }; auto s = [](const std::string& t) { return "\"" + json::esc(t) + "\""; };
    auto horizon = [&](const Horizon& H) { o << "{\"name\":" << s(H.name) << ",\"ok\":" << (H.ok ? "true" : "false") << ",\"reason\":" << s(H.reason) << ",\"as_of\":" << s(H.as_of) << ",\"headline\":" << s(H.regime_headline) << ",\"expiries_text\":" << s(H.expiries_text)
        << ",\"net_gamma\":" << n(H.net_gamma) << ",\"em_pct\":" << n(H.em_pct) << ",\"flip\":" << (H.flip ? n(*H.flip) : "null") << ",\"assumption\":" << s(H.assumption) << ",\"expiries\":[";
        for (size_t i = 0; i < H.expiries.size(); ++i) { auto& e = H.expiries[i]; o << (i ? "," : "") << "{\"dte\":" << e.dte << ",\"label\":" << s(e.label) << ",\"contracts\":" << e.contracts << ",\"oi_share\":" << n(e.oi_share) << ",\"gamma_share\":" << n(e.gamma_share) << "}"; }
        o << "],\"cards\":[";
        for (size_t i = 0; i < H.cards.size(); ++i) { auto& c = H.cards[i]; auto& m = c.m; o << (i ? "," : "") << "{\"strike\":" << n(m.strike) << ",\"dist_pct\":" << n(m.dist_pct) << ",\"gamma\":" << n(m.gamma) << ",\"vanna_up\":" << n(m.vanna_up) << ",\"vanna_dn\":" << n(m.vanna_dn) << ",\"charm_day\":" << n(m.charm_day)
            << ",\"charm_close\":" << (m.has_close ? n(m.charm_close) : "null") << ",\"delta_sh\":" << n(m.delta_sh) << ",\"theta\":" << n(m.theta_usd) << ",\"speed\":" << n(m.speed) << ",\"iv_k\":" << n(m.iv_k) << ",\"skew\":" << n(m.skew) << ",\"p_beyond\":" << n(m.p_beyond) << ",\"p_touch\":" << n(m.p_touch) << ",\"sigma_dist\":" << n(m.sigma_dist) << ",\"gamma_share\":" << n(m.gamma_share) << ",\"vomma\":" << n(m.vomma) << ",\"vega\":" << n(m.vega) << ",\"color\":" << n(m.color) << ",\"importance\":" << n(m.importance) << ",\"verdict\":" << s(c.verdict) << ",\"contested\":" << (c.contested ? "true" : "false")
            << ",\"header\":" << s(c.header) << ",\"mechanism\":" << s(c.mechanism) << ",\"approach\":" << s(c.approach) << ",\"breach\":" << s(c.breach) << ",\"valid_until\":" << s(c.validity_until) << ",\"valid_limit\":" << s(c.validity_limit)
            << ",\"regime\":" << s(c.regime_note) << ",\"conviction\":" << s(c.conviction) << ",\"confidence\":" << s(c.confidence) << ",\"as_of\":" << s(c.as_of) << ",\"pb\":{\"style\":" << s(c.pb.style) << ",\"hold\":" << s(c.pb.hold) << ",\"entry\":" << s(c.pb.entry) << ",\"stop\":" << s(c.pb.stop) << ",\"target\":" << s(c.pb.target) << ",\"potential\":" << s(c.pb.hold_potential) << ",\"speed\":" << s(c.pb.speed) << ",\"entry_px\":" << n(c.pb.entry_px) << ",\"stop_px\":" << n(c.pb.stop_px) << ",\"target_px\":" << n(c.pb.target_px) << ",\"rr\":" << n(c.pb.rr) << ",\"p_target\":" << n(c.pb.p_target) << ",\"capture_pts\":" << n(c.pb.capture_pts) << ",\"capture_pct\":" << n(c.pb.capture_pct) << ",\"edge\":["; for (size_t j = 0; j < c.pb.edge.size(); ++j) { o << (j ? "," : "") << s(c.pb.edge[j]); } o << "]}"
            << ",\"agree\":" << c.agree << ",\"conflict\":" << c.conflict << ",\"votes\":[";
            for (size_t j = 0; j < c.votes.size(); ++j) o << (j ? "," : "") << "{\"greek\":" << s(c.votes[j].greek) << ",\"v\":" << c.votes[j].v << ",\"text\":" << s(c.votes[j].text) << "}";
            o << "],\"invalidation\":["; for (size_t j = 0; j < c.invalidation.size(); ++j) o << (j ? "," : "") << s(c.invalidation[j]); o << "]}"; }
        o << "]}"; };
    o << "{\"spot\":" << n(I.spot) << ",\"as_of\":" << s(I.as_of) << ",\"intraday\":"; horizon(I.intraday); o << ",\"swinging\":"; horizon(I.swinging); o << ",\"reconcile\":[";
    for (size_t i = 0; i < I.reconcile.size(); ++i) { o << (i ? "," : "") << s(I.reconcile[i]); }
    o << "]}"; return o.str();
}
}
