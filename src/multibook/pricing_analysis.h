#pragma once
// pricing_analysis.h - Pricing Surfaces analysis blocks (workfile A-1..A-8). Every statement is derived from the book's own
// measured data (RND, term/skew, Greeks, hedge ladder, strike insights, session clock) and lists the inputs it rests on.
// Cross-asset factors (DXY, bonds, vol index) are LIVE readings when a feed is supplied, otherwise stated unavailable; the
// scenario composer treats a factor state as an explicit user hypothesis ("what if"), never as an observed fact.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "../bridge/json_util.h"
#include "../core/rnd.h"
namespace vt::mb {
struct Dist {
    bool ok = false; int dte = 0; double spot = 0, sigma = 0, q05 = 0, q25 = 0, q50 = 0, q75 = 0, q95 = 0, mean = 0, sd = 0, skew = 0, exkurt = 0;
    double p_below_spot = 0, p_dn1 = 0, p_up1 = 0, p_dn2 = 0, p_up2 = 0, x0 = 0, dx = 0; std::vector<double> hist;
};
inline Dist compute_dist(const ChainSnapshot& c, int dte, double atm_iv) {
    Dist d; if (c.spot <= 0 || dte <= 0 || atm_iv <= 0) return d;
    auto r = model::rnd_single_expiry(c, dte); if (r.size() < 5) return d;
    std::vector<double> w(r.size()); double tot = 0;
    for (size_t i = 0; i < r.size(); ++i) { double dx = i + 1 < r.size() ? r[i + 1].x - r[i].x : r[i].x - r[i - 1].x; w[i] = std::max(0.0, r[i].iv) * dx; tot += w[i]; }
    if (tot <= 0) return d;
    for (auto& x : w) x /= tot;
    d.dte = dte; d.spot = c.spot; d.sigma = c.spot * atm_iv * std::sqrt(dte / 365.0);
    double m1 = 0; for (size_t i = 0; i < r.size(); ++i) m1 += w[i] * r[i].x;
    double m2 = 0, m3 = 0, m4 = 0; for (size_t i = 0; i < r.size(); ++i) { double e = r[i].x - m1; m2 += w[i] * e * e; m3 += w[i] * e * e * e; m4 += w[i] * e * e * e * e; }
    d.mean = m1; d.sd = std::sqrt(m2); if (d.sd > 0) { d.skew = m3 / (m2 * d.sd); d.exkurt = m4 / (m2 * m2) - 3; }
    double cum = 0; auto q = [&](double p) { double a = 0; for (size_t i = 0; i < r.size(); ++i) { a += w[i]; if (a >= p) return r[i].x; } return r.back().x; };
    d.q05 = q(0.05); d.q25 = q(0.25); d.q50 = q(0.5); d.q75 = q(0.75); d.q95 = q(0.95);
    for (size_t i = 0; i < r.size(); ++i) { double x = r[i].x; if (x < c.spot) d.p_below_spot += w[i]; if (x < c.spot - d.sigma) d.p_dn1 += w[i]; if (x > c.spot + d.sigma) d.p_up1 += w[i];
        if (x < c.spot - 2 * d.sigma) d.p_dn2 += w[i];
        if (x > c.spot + 2 * d.sigma) d.p_up2 += w[i];
        cum += w[i]; }
    const int N = 25; d.x0 = c.spot - 3 * d.sigma; d.dx = 6 * d.sigma / N; d.hist.assign(N, 0.0);
    for (size_t i = 0; i < r.size(); ++i) { int b = (int)std::floor((r[i].x - d.x0) / d.dx); if (b >= 0 && b < N) d.hist[b] += w[i]; }
    d.ok = true; return d;
}
struct Row { std::string k, v, n; };
struct Block { std::string title, headline; bool ok = true; std::string reason; std::vector<Row> rows; std::vector<std::string> reads, inputs; };
struct Stage { std::string title, text; };
struct Trigger { std::string id, label, dir; double strike = 0, fut = 0; bool has_fut = false; std::vector<Stage> stages; std::vector<std::string> downstream, stops, window, inputs; };
struct FState { std::string state, label, dn, up; };                    // dn / up: effect text if the trigger breaks down / up
struct Factor { std::string id, label; bool live = false; std::string live_state, live_note; std::vector<FState> states; };
struct Pair { std::string a, b, text; };
struct Analysis { Block risk, dist, options, fund, intraday, weekly; std::vector<Trigger> trig; std::vector<Factor> factors; std::vector<Pair> pairs; std::string asset_class; Dist d; bool ok = false; };
// live cross-asset reading: trend -1/0/+1 over the feed's own window (price of the instrument, bonds = bond PRICE)
struct CrossIn { bool available = false; std::string id, reason; int trend = 0; double level = 0, chg = 0; };
namespace pa {
inline std::string f(double v, int p = 2) { std::ostringstream o; o.setf(std::ios::fixed); o.precision(p); o << v; return o.str(); }
inline std::string pct(double v, int p = 1) { return f(v * 100, p) + "%"; }
inline std::string px(double v) { return f(v, std::fabs(v) < 1000 ? 2 : 0); }
inline std::string sgn(double v, int p = 1) { return (v >= 0 ? "+" : "") + f(v, p); }
inline std::string money(double v) { double a = std::fabs(v); std::string s = v < 0 ? "-$" : "$"; if (a >= 1e9) return s + f(a / 1e9, 2) + "B"; if (a >= 1e6) return s + f(a / 1e6, 1) + "M"; if (a >= 1e3) return s + f(a / 1e3, 0) + "K"; return s + f(a, 0); }
inline std::string klass(const std::string& fam) {
    static const char* eq[] = {"NQ", "ES", "YM", "RTY", "MNQ", "MES", "NDX", "SPX"}; for (auto* e : eq) if (fam == e) return "equity_index";
    if (fam == "GC" || fam == "SI") return "metal";
    if (fam == "CL") return "energy";
    return "other"; }
inline void row(Block& b, std::string k, std::string v, std::string n = "") { b.rows.push_back({std::move(k), std::move(v), std::move(n)}); }
}
// ---- factor rule tables: effect of each cross-asset state on a book of the given asset class. Rule-based (stated), not fitted.
inline std::vector<Factor> factor_rules(const std::string& cls, const std::map<std::string, CrossIn>& cross) {
    std::vector<Factor> F;
    auto live = [&](Factor& f, const char* key, const char* pos, const char* neg) { auto it = cross.find(key);
        if (it == cross.end() || !it->second.available) { f.live = false; f.live_note = "no " + std::string(key) + " feed connected: the state below is a hypothesis you choose, not a reading"; return; }
        f.live = true; f.live_state = it->second.trend > 0 ? pos : it->second.trend < 0 ? neg : "flat"; f.live_note = std::string(key) + " live: " + f.live_state; };
    Factor d; d.id = "dxy"; d.label = "US dollar (DXY)"; live(d, "DXY", "rising", "falling");
    Factor b; b.id = "bonds"; b.label = "Bonds (price)"; live(b, "BONDS", "rising (yields falling)", "falling (yields rising)");
    Factor v; v.id = "vol"; v.label = "Volatility index"; live(v, "VOL", "rising", "falling");
    if (cls == "equity_index") {
        d.states = {{"up", "rising", "Dollar strength tightens financial conditions and weighs on foreign-earnings translation, so it adds selling pressure to the break.", "A rising dollar works against an upside move: rallies run into tighter conditions and lose follow-through."},
                    {"down", "falling", "A falling dollar eases conditions and can slow the break: it is a cushion, not a floor.", "A falling dollar eases conditions and supports the upside move."}};
        b.states = {{"up", "rising (yields falling)", "Rising bonds during an equity break reads as flight to safety: it confirms risk-off and does not cushion equities.", "Rising bonds with rising equities is a supportive, falling-yield backdrop for duration-sensitive indices."},
                    {"down", "falling (yields rising)", "Falling bonds (rising yields) while equities break adds a valuation squeeze on top of the options-driven selling.", "Falling bonds (rising yields) caps upside for long-duration growth names and limits the move."}};
        v.states = {{"up", "rising", "Rising volatility raises dealer vega/vanna demands: lower puts and hedges get re-priced and dealer hedging accelerates the break.", "Rising volatility during a rally is unusual: it signals hedging demand and makes upside breaks less reliable."},
                    {"down", "falling", "Falling volatility during a break suggests an orderly pullback: vanna/charm flows turn supportive and the break tends to stall.", "Falling volatility supports the rally: vanna flows turn dealers into buyers."}};
    } else if (cls == "metal") {
        d.states = {{"up", "rising", "A rising dollar is the classic headwind for gold: it adds pressure to a break lower.", "A rising dollar works against an upside move in gold."},
                    {"down", "falling", "A falling dollar cushions gold and slows a break.", "A falling dollar supports an upside move."}};
        b.states = {{"up", "rising (yields falling)", "Falling yields lower the opportunity cost of holding gold: it cushions a break.", "Falling yields support the upside move (lower real-rate opportunity cost)."},
                    {"down", "falling (yields rising)", "Rising yields raise the opportunity cost of gold and add pressure to a break.", "Rising yields cap the upside move."}};
        v.states = {{"up", "rising", "Rising equity volatility can bring safe-haven bids that offset a break, or forced liquidation that adds to it: direction is not fixed by the options data.", "Rising volatility can add safe-haven demand and support an upside move."},
                    {"down", "falling", "Falling volatility reduces safe-haven demand and adds mild pressure.", "Falling volatility reduces safe-haven demand and limits the upside."}};
    } else { for (auto* x : {&d, &b, &v}) x->states = {{"up", "up", "No rule table for this asset class: the options-only chain is shown, the factor adds nothing.", "No rule table for this asset class: the options-only chain is shown, the factor adds nothing."},
                    {"down", "down", "No rule table for this asset class: the options-only chain is shown, the factor adds nothing.", "No rule table for this asset class: the options-only chain is shown, the factor adds nothing."}}; }
    F = {d, b, v}; return F;
}
inline std::vector<Pair> pair_rules(const std::string& cls) {
    std::vector<Pair> P; if (cls != "equity_index") return P;
    P = {{"up", "up", "Rising dollar AND rising bonds while the index breaks: classic risk-off. The dollar and the bond bid both drain risk appetite, and neither cushions equities."},
         {"up", "down", "Rising dollar with falling bonds: a rates-driven tightening. It pressures long-duration index weights hardest and is the most damaging combination for valuation."},
         {"down", "up", "Falling dollar with rising bonds: easing conditions. A break in this setting is more likely to be technical/positioning and to find buyers sooner."},
         {"down", "down", "Falling dollar with falling bonds: mixed signal (inflation/reflation). Index breaks are less macro-driven; options flows dominate."}};
    return P;
}
// ---- the analysis for one lens book. Templates avoid a circular include with strike_lens.h / book_analytics.h.
template <class LB, class BA> inline Analysis analyze_book(const LB& b, const BA& a, const std::string& family, const VolIn& vi, const std::map<std::string, CrossIn>& cross, const std::vector<const LB*>& sibs) {
    using namespace pa; Analysis A; A.asset_class = klass(family); A.d = a.dist; const double S = a.spot; if (S <= 0) return A;
    const auto& I = b.insight; const auto& V = a.vol; const auto& H = a.hedge; const auto& P = a.pricing; A.ok = true;
    // ---------------- Risk Pricing (A-2)
    Block& R = A.risk; R.title = "Risk Pricing";
    if (!P.ok) { R.ok = false; R.reason = "no expiry with usable implied volatility"; }
    else {
        double em1 = V.ok ? V.em_1d : 0; double emR = P.em_pct;
        R.headline = "Options price a " + pct(emR) + " (1σ) move to " + std::to_string(P.ref_dte) + " DTE" + (em1 > 0 ? " and " + pct(em1) + " for one day" : "") + ", ATM IV " + pct(P.atm_iv) + ".";
        row(R, "Implied move to " + std::to_string(P.ref_dte) + " DTE (1σ)", "±" + px(S * emR) + " pts (" + pct(emR, 2) + ")", "ATM IV × √(DTE/365) × spot");
        if (em1 > 0) row(R, "Implied 1-day move (1σ)", "±" + px(S * em1) + " pts (" + pct(em1, 2) + ")", "near-expiry ATM IV");
        row(R, "25Δ risk reversal", sgn(V.ok ? V.rr25 * 100 : P.rr25 * 100) + " vol pts", (V.ok ? V.rr25 : P.rr25) < 0 ? "put IV above call IV: the market pays more for downside" : "call IV above put IV: the market pays more for upside");
        if (V.ok) { row(R, "Put-wing steepness", sgn(V.put_wing * 100) + " vol pts", "IV(10% OTM put) − ATM IV: how much extra is paid for tail protection");
            row(R, "Term structure", V.backwardated ? "backwardated" : "contango", "slope " + sgn(V.term_slope * 100) + " vol pts per √year; " + (V.backwardated ? "near-term risk is priced above later risk (event/stress)" : "later expiries price more risk than the near term (calm)"));
            for (auto& t : V.term) if (&t == &V.term.front() || &t == &V.term.back() || t.dte == P.ref_dte) row(R, "ATM IV " + std::to_string(t.dte) + " DTE", pct(t.atm_iv), "25Δ RR " + sgn(t.rr25 * 100) + " pts");
            if (V.vrp) row(R, "Implied − realised vol (VRP)", sgn(*V.vrp * 100) + " vol pts", *V.vrp > 0 ? "options rich versus realised" : "options cheap versus realised"); else row(R, "Implied − realised vol (VRP)", "unavailable", "no realised-volatility feed connected");
            if (V.em_ratio) row(R, "Realised / implied 1-day move", f(*V.em_ratio, 2) + "×", *V.em_ratio > 1 ? "realised exceeded the priced move" : "realised inside the priced move"); }
        if (vi.available) row(R, vi.id + " regime", vi.regime + (vi.trend > 0 ? ", rising" : vi.trend < 0 ? ", falling" : ""), "level " + f(vi.level, 2) + ", " + pct(vi.pct_rank, 0) + " percentile"); else row(R, "Volatility index regime", "unavailable", vi.reason.empty() ? "no vol index feed for this book" : vi.reason);
        double rr = V.ok ? V.rr25 : P.rr25;
        R.reads.push_back(std::string("Hedging demand: ") + (rr < -0.01 ? "the market is paying more for DOWNSIDE protection (puts over calls); a drop is the scenario being insured, so a break lower finds hedgers already positioned and dealers short convexity below." : rr > 0.01 ? "the market is paying more for UPSIDE (calls over puts), typical of squeeze or melt-up positioning; an upside break carries dealer buying." : "demand is balanced between puts and calls: no strong directional hedging bid."));
        if (V.ok && V.put_wing > 0.03) R.reads.push_back("Tail risk: the put wing is steep (" + sgn(V.put_wing * 100) + " pts for a 10% OTM put), so the market prices a fat left tail beyond what ATM IV implies.");
        if (V.ok && V.backwardated) R.reads.push_back("Backwardation: near-term IV exceeds later IV, which means the market is pricing an imminent event or stress rather than a drift in risk.");
        R.inputs = {"ATM IV and 25Δ strikes at " + std::to_string(P.ref_dte) + " DTE", "term structure of " + std::to_string(V.term.size()) + " expiries", "spot " + px(S), "chain as of " + I.as_of};
    }
    // ---------------- Distribution (A-3)
    Block& D = A.dist; D.title = "Distribution";
    const Dist& d = a.dist;
    if (!d.ok) { D.ok = false; D.reason = "risk-neutral density could not be built from this chain (too few strikes at the reference expiry)"; }
    else {
        double skewTxt = d.skew; double lo = d.q05 - S, hi = d.q95 - S;
        D.headline = "Implied outcomes at " + std::to_string(d.dte) + " DTE: 50% inside " + px(d.q25) + "–" + px(d.q75) + ", 90% inside " + px(d.q05) + "–" + px(d.q95) + ", " + (skewTxt < -0.15 ? "skewed to the downside" : skewTxt > 0.15 ? "skewed to the upside" : "roughly symmetric") + ".";
        row(D, "Median", px(d.q50), sgn(d.q50 - S, 2) + " vs spot (drift/carry)");
        row(D, "Likely range (25–75%)", px(d.q25) + " – " + px(d.q75), "half of the probability mass");
        row(D, "Extended range (5–95%)", px(d.q05) + " – " + px(d.q95), "downside " + sgn(lo, 0) + " pts, upside " + sgn(hi, 0) + " pts from spot");
        row(D, "P(finish below spot)", pct(d.p_below_spot, 0), "risk-neutral, not a forecast");
        row(D, "P(beyond −1σ / +1σ)", pct(d.p_dn1) + " / " + pct(d.p_up1), "normal reference 15.9% / 15.9%");
        row(D, "P(beyond −2σ / +2σ)", pct(d.p_dn2) + " / " + pct(d.p_up2), "normal reference 2.3% / 2.3%");
        row(D, "Skewness", sgn(d.skew, 2), d.skew < 0 ? "left tail heavier than right" : "right tail heavier than left");
        row(D, "Excess kurtosis", sgn(d.exkurt, 2), d.exkurt > 0.5 ? "fat tails: extreme moves more likely than a normal curve" : "tails close to normal");
        if (d.p_dn2 > 0.028) D.reads.push_back("Tail zone below " + px(S - 2 * d.sigma) + " carries " + pct(d.p_dn2) + " versus 2.3% for a normal curve: the market pays up for a crash scenario.");
        if (d.p_up2 > 0.028) D.reads.push_back("Tail zone above " + px(S + 2 * d.sigma) + " carries " + pct(d.p_up2) + ": the market prices a melt-up tail.");
        if (!(d.p_dn2 > 0.028) && !(d.p_up2 > 0.028)) D.reads.push_back("Neither tail is priced richer than a normal curve at ±2σ.");
        if (vi.available) D.reads.push_back(vi.id + " is " + vi.regime + (vi.trend > 0 ? " and rising" : vi.trend < 0 ? " and falling" : "") + ": " + (vi.regime == "stress" || vi.regime == "elevated" ? "tails are priced against an already-stressed backdrop, so the outer zones are more reachable than in calm markets; a falling index would shrink them." : "the volatility backdrop is calm, so the priced tails rely on a jump rather than on drift in risk."));
        D.reads.push_back("Zones: likely range " + px(d.q25) + "–" + px(d.q75) + "; tail scenarios sit outside " + px(d.q05) + " / " + px(d.q95) + ". Strikes inside the likely range are where the options structure matters day to day; outside it, only a regime change reaches them.");
        D.inputs = {"Breeden–Litzenberger density at " + std::to_string(d.dte) + " DTE", "1σ = " + px(d.sigma) + " pts from ATM IV", "normal reference is a fixed-volatility curve, not a model of this book"};
    }
    // ---------------- Options-Market View (A-4)
    Block& O = A.options; O.title = "Options-Market View";
    { double flipd = a.flip ? (*a.flip - S) / S : 0;
      O.headline = std::string("Dealers are net ") + (a.net_gex >= 0 ? "LONG" : "SHORT") + " gamma (" + money(a.net_gex) + " per 1%)" + (a.flip ? ", gamma flip " + px(*a.flip) + " (" + sgn(flipd * 100, 2) + "% from spot)." : ".");
      row(O, "Net dealer gamma", money(a.net_gex) + " / 1%", a.net_gex >= 0 ? "long: dealers sell rallies and buy dips, which dampens moves" : "short: dealers buy rallies and sell dips, which amplifies moves");
      if (a.flip) row(O, "Gamma flip", px(*a.flip), S > *a.flip ? "spot above: positive-gamma side" : "spot below: negative-gamma side");
      if (a.call_wall) row(O, "Call wall", px(*a.call_wall), "largest positive dealer exposure: resistance / pin candidate");
      if (a.put_wall) row(O, "Put wall", px(*a.put_wall), "largest negative dealer exposure: acceleration / support candidate");
      if (V.ok) { row(O, "Gamma concentration", "top-3 strikes " + pct(V.top3_share, 0) + ", HHI " + f(V.gamma_hhi, 2), V.top3_share > 0.55 ? "concentrated: a few strikes dominate hedging" : "spread out: no single strike dominates"); row(O, "Near-expiry open interest", pct(V.oi_near_share, 0), "share of OI in the nearest expiry"); }
      if (H.ok) { row(O, "Dealer hedge per 1% up/down", sgn(H.ladder.size() > 5 ? H.ladder[5].flow_notional : 0, 0) + " / " + sgn(H.ladder.size() > 1 ? H.ladder[1].flow_notional : 0, 0), "notional of hedge trades if spot moves 1% (+ = dealers buy)");
          row(O, "Vanna ±1 vol pt", sgn(H.vanna_up, 0) + " / " + sgn(H.vanna_dn, 0) + " units", "hedge flow if IV rises / falls one point");
          row(O, "Charm (one day / to close)", sgn(H.charm_1d, 0) + " / " + sgn(H.charm_to_close, 0) + " units", "flow from time passing, spot unchanged");
          row(O, "0DTE share of gamma / OI", pct(H.zero_dte_gamma_share, 0) + " / " + pct(H.zero_dte_oi_share, 0), "how much of today's hedging disappears at the close"); }
      if (a.net_gex < 0) O.reads.push_back("Negative gamma: every move forces dealers to trade WITH it, so ranges widen and levels break more easily; the call/put walls act as speed bumps only while gamma there is still positive.");
      else O.reads.push_back("Positive gamma: dealer hedging leans against moves, which compresses range; expect pinning towards the largest positive strikes and fade-able extremes until spot crosses the flip.");
      if (!a.top.empty()) { std::string t; for (size_t i = 0; i < a.top.size() && i < 3; ++i) t += (i ? ", " : "") + px(a.top[i].strike) + " (" + money(a.top[i].value) + ")"; O.reads.push_back("Hedging flow concentrates at " + t + "."); }
      O.inputs = {"dealer convention: long calls / short puts (assumed)", "full Black–Scholes revaluation of every open-interest line", "OI as of " + I.as_of}; }
    // ---------------- Fundamental View (A-5): only what the terminal holds
    Block& Fu = A.fund; Fu.title = "Fundamental View";
    { auto cr = [&](const char* k) { auto it = cross.find(k); return it != cross.end() && it->second.available ? &it->second : nullptr; };
      std::string have; int n = 0;
      auto line = [&](const char* label, const char* key, const char* up, const char* dn) { auto* c = cr(key); if (c) { ++n; row(Fu, label, std::string(c->trend > 0 ? up : c->trend < 0 ? dn : "flat") + ", level " + f(c->level, 2), "live feed"); have += std::string(label) + " "; } else row(Fu, label, "unavailable", "no feed connected: not inferred"); };
      line("US dollar (DXY)", "DXY", "rising", "falling"); line("Bonds / yields", "BONDS", "bond prices rising", "bond prices falling");
      row(Fu, "Index composition / concentration", "unavailable", "no constituent-weight feed connected");
      row(Fu, "Earnings and event calendar", "unavailable", "no calendar feed connected");
      row(Fu, "Valuation context (P/E, yield gap)", "unavailable", "no fundamentals feed connected");
      if (vi.available) { ++n; row(Fu, vi.id, vi.regime + ", level " + f(vi.level, 2), "live volatility-index feed"); } else row(Fu, "Volatility index", "unavailable", vi.reason.empty() ? "no feed connected" : vi.reason);
      row(Fu, "Option expiry calendar", std::to_string(I.swinging.expiries.size() + I.intraday.expiries.size()) + " expiries in the chain", "derived from the chain itself");
      Fu.headline = n ? "Fundamental inputs in use: " + have + (vi.available ? vi.id : std::string()) + "." : "No fundamental input is connected: nothing below is inferred, each row states what is missing.";
      Fu.reads.push_back("The terminal only draws on its own data. Rows marked unavailable are not estimated; connecting the feed adds them to the cascade rules and to this block.");
      Fu.inputs = {"live cross-asset feeds: " + std::string(n ? have : "none"), "no inference from training knowledge is used for any value"}; }
    // ---------------- Intraday Structure (A-6)
    Block& In = A.intraday; In.title = "Intraday Structure";
    if (!I.intraday.ok) { In.ok = false; In.reason = I.intraday.reason.empty() ? "no 0DTE/1DTE open interest" : I.intraday.reason; }
    else { const auto& h = I.intraday; In.headline = h.regime_headline.empty() ? h.flip_text : h.regime_headline;
        row(In, "Session", h.session_text.empty() ? "unavailable" : h.session_text, "as of " + h.as_of);
        if (h.flip) row(In, "Intraday flip", px(*h.flip), h.flip_text);
        row(In, "Expected 1σ session move", "±" + px(S * h.em_pct) + " pts", "from 0–1 DTE ATM IV");
        row(In, "Sources", h.expiries_text, "");
        int k = 0; for (auto& c : h.cards) { if (k++ >= 5) break; double dist = c.m.strike - S;
            row(In, px(c.m.strike) + " " + c.verdict, sgn(dist, 1) + " pts, " + f(c.m.sigma_dist, 2) + "σ, touch " + pct(c.m.p_touch, 0), c.mechanism + " Valid until " + c.validity_until + ". " + c.pb.style + ": " + c.pb.hold); }
        In.reads.push_back("Times that matter: the first hour carries the opening hedge adjustments; late session (after 15:00 ET) carries charm and 0DTE gamma decay, so strikes with short half-lives lose their pull before the close.");
        if (!h.vol_text.empty()) In.reads.push_back(h.vol_text);
        In.inputs = {"strike insight cards (Intraday)", "session clock (ET RTH)", "0DTE/1DTE open interest and gamma"}; }
    // ---------------- Weekly Structure (A-6)
    Block& W = A.weekly; W.title = "Weekly Structure";
    if (!I.swinging.ok) { W.ok = false; W.reason = I.swinging.reason.empty() ? "no expiry 2..45 days out" : I.swinging.reason; }
    else { const auto& h = I.swinging; W.headline = h.regime_headline.empty() ? h.flip_text : h.regime_headline;
        for (size_t i = 0; i < h.expiries.size() && i < 6; ++i) { auto& e = h.expiries[i]; row(W, e.label + " (" + std::to_string(e.dte) + " DTE)", pct(e.gamma_share, 0) + " of gamma, " + pct(e.oi_share, 0) + " of OI", "this expiry rolls off the book on that date"); }
        if (h.flip) row(W, "Multi-day flip", px(*h.flip), h.flip_text);
        row(W, "Expected 1σ move over the window", "±" + px(S * h.em_pct) + " pts", "from further-dated ATM IV");
        int k = 0; for (auto& c : h.cards) { if (k++ >= 5) break; row(W, px(c.m.strike) + " " + c.verdict, sgn(c.m.strike - S, 0) + " pts, " + f(c.m.sigma_dist, 2) + "σ, touch " + pct(c.m.p_touch, 0), c.mechanism + " Valid until " + c.validity_until + ". " + c.pb.style); }
        if (V.ok) for (auto& t : V.term) if (t.dte >= 2 && t.dte <= 30) row(W, "Skew " + std::to_string(t.dte) + " DTE", "25Δ RR " + sgn(t.rr25 * 100) + " pts", "ATM IV " + pct(t.atm_iv));
        W.reads.push_back("Gamma roll-off schedule: the share of gamma tied to each expiry shows when today's pins and walls stop existing; a wall that sits on a near expiry is a short-lived level.");
        W.reads.push_back(h.expiries.empty() ? "" : "Largest dated block: " + h.expiries.front().label + " with " + pct(h.expiries.front().gamma_share, 0) + " of swing-horizon gamma: expect pinning toward its strikes into that date and a regime shift after it.");
        W.inputs = {"strike insight cards (Swinging, 2–45 DTE)", "OI and gamma by expiry", "term structure and 25Δ skew by tenor"}; }
    // ---------------- Cross-Asset Cascades (A-7): trigger library + factor/pair rules; the page composes them.
    A.factors = factor_rules(A.asset_class, cross); A.pairs = pair_rules(A.asset_class);
    struct Cand { const typename std::decay<decltype(I.swinging.cards[0])>::type* c; std::string hz; };
    std::vector<Cand> cands; for (auto& c : I.swinging.cards) cands.push_back({&c, "Swinging"}); for (auto& c : I.intraday.cards) cands.push_back({&c, "Intraday"});
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.c->m.importance > y.c->m.importance; });
    int below = 0, above = 0; std::vector<double> seen;
    for (auto& cd : cands) { auto& c = *cd.c; bool dn = c.m.strike < S; if ((dn ? below : above) >= 2) continue; bool dup = false; for (double x : seen) if (std::fabs(x - c.m.strike) < 1e-9) dup = true; if (dup) continue;
        (dn ? below : above)++; seen.push_back(c.m.strike);
        Trigger T; T.id = std::string(dn ? "dn" : "up") + px(c.m.strike); T.dir = dn ? "dn" : "up"; T.strike = c.m.strike; T.label = std::string(dn ? "Break below " : "Break above ") + px(c.m.strike) + " (" + cd.hz + " " + c.verdict + ")";
        for (auto& lv : b.levels) if (std::fabs(lv.strike - c.m.strike) < 1e-9 && lv.fut) { T.fut = *lv.fut; T.has_fut = true; }
        if (!T.has_fut && b.convertible) { T.fut = std::round((c.m.strike * b.ratio + b.basis)); T.has_fut = true; }
        T.stages.push_back({"1 · What the strike is", c.verdict + " at " + px(c.m.strike) + ": " + c.mechanism + " It holds " + pct(c.m.gamma_share, 0) + " of horizon gamma; the risk-neutral chance of touching it is " + pct(c.m.p_touch, 0) + "."});
        { double flow = 0; if (H.ok) { const auto& L = H.ladder; double target = (c.m.strike - S) / S * 100; size_t best = 0; double bd = 1e18; for (size_t i = 0; i < L.size(); ++i) { double dd = std::fabs(L[i].shock_pct * (std::fabs(L[i].shock_pct) < 0.05 ? 100 : 1) - target); if (dd < bd) { bd = dd; best = i; } } if (!L.empty()) flow = L[best].flow_notional; }
          T.stages.push_back({"2 · What dealers do on the break", c.breach + (H.ok ? " Projected hedge flow at the nearest ladder shock: " + sgn(flow, 0) + " notional (+ = dealers buy)." : "") + (c.m.gamma < 0 ? " Gamma at this strike is negative, so hedging is trade-with: it accelerates the move." : " Gamma at this strike is positive, so hedging is trade-against: the break is absorbed until the strike's gamma is consumed.")}); }
        T.stages.push_back({"3 · Time windows", "Valid until " + c.validity_until + " (" + c.validity_limit + ")."});
        for (auto& cc : cands) { auto& o = *cc.c; if (&o == &c) continue; if (dn ? (o.m.strike < c.m.strike) : (o.m.strike > c.m.strike)) { if (T.downstream.size() < 3) { bool dd = false; for (auto& s : T.downstream) if (s.find(px(o.m.strike)) == 0) dd = true; if (!dd) T.downstream.push_back(px(o.m.strike) + " " + o.verdict + " (" + cc.hz + ", " + f(o.m.sigma_dist, 2) + "σ)"); } } }
        if (d.ok) T.downstream.push_back(std::string("RND ") + (dn ? "5% tail " + px(d.q05) : "95% tail " + px(d.q95)) + " (outer edge of the priced range)");
        T.stops = c.invalidation;
        for (auto* sb : sibs) { if (sb == &b || !sb->convertible || !T.has_fut) continue; for (auto& lv : sb->levels) if (lv.fut && std::fabs(*lv.fut - T.fut) <= 0.15 * S * (P.ok ? P.em_pct : 0.01) * (b.ratio > 0 ? b.ratio : 1)) { T.stages.push_back({"4 · Cross-book confluence", sb->id + " has " + lv.label + " " + px(lv.strike) + " at the same futures level (" + px(*lv.fut) + "): the break is also a break for that book, doubling hedging at the level."}); break; } }
        T.inputs = {"card " + px(c.m.strike) + " (" + cd.hz + ")", "hedge ladder", b.convertible ? "conversion ratio " + f(b.ratio, 4) + ", basis " + f(b.basis, 2) : "book not convertible to futures"};
        A.trig.push_back(T); }
    return A;
}
// per-book analysis JSON
inline std::string block_json(const Block& b) {
    std::ostringstream o; o << "{\"title\":\"" << json::esc(b.title) << "\",\"ok\":" << (b.ok ? "true" : "false") << ",\"reason\":\"" << json::esc(b.reason) << "\",\"headline\":\"" << json::esc(b.headline) << "\",\"rows\":[";
    for (size_t i = 0; i < b.rows.size(); ++i) o << (i ? "," : "") << "{\"k\":\"" << json::esc(b.rows[i].k) << "\",\"v\":\"" << json::esc(b.rows[i].v) << "\",\"n\":\"" << json::esc(b.rows[i].n) << "\"}";
    o << "],\"reads\":["; { bool first = true; for (auto& r : b.reads) { if (r.empty()) continue; o << (first ? "" : ",") << "\"" << json::esc(r) << "\""; first = false; } }
    o << "],\"inputs\":["; for (size_t i = 0; i < b.inputs.size(); ++i) o << (i ? "," : "") << "\"" << json::esc(b.inputs[i]) << "\""; o << "]}"; return o.str(); }
inline std::string analysis_json(const Analysis& A) {
    if (!A.ok) { return "null"; }
    std::ostringstream o; auto n = [](double v) { return json::num(v); }; auto arr = [&](const std::vector<std::string>& v) { std::string s = "["; for (size_t i = 0; i < v.size(); ++i) s += (i ? ",\"" : "\"") + json::esc(v[i]) + "\""; return s + "]"; };
    o << "{\"class\":\"" << A.asset_class << "\",\"risk\":" << block_json(A.risk) << ",\"dist\":" << block_json(A.dist) << ",\"options\":" << block_json(A.options) << ",\"fund\":" << block_json(A.fund) << ",\"intraday\":" << block_json(A.intraday) << ",\"weekly\":" << block_json(A.weekly);
    o << ",\"hist\":{\"ok\":" << (A.d.ok ? "true" : "false") << ",\"x0\":" << n(A.d.x0) << ",\"dx\":" << n(A.d.dx) << ",\"spot\":" << n(A.d.spot) << ",\"sigma\":" << n(A.d.sigma) << ",\"q\":[" << n(A.d.q05) << "," << n(A.d.q25) << "," << n(A.d.q50) << "," << n(A.d.q75) << "," << n(A.d.q95) << "],\"p\":[";
    for (size_t i = 0; i < A.d.hist.size(); ++i) { o << (i ? "," : "") << n(A.d.hist[i]); }
    o << "]},\"casc\":{\"triggers\":[";
    for (size_t i = 0; i < A.trig.size(); ++i) { auto& t = A.trig[i]; o << (i ? "," : "") << "{\"id\":\"" << json::esc(t.id) << "\",\"label\":\"" << json::esc(t.label) << "\",\"dir\":\"" << t.dir << "\",\"strike\":" << n(t.strike) << ",\"fut\":" << (t.has_fut ? n(t.fut) : "null") << ",\"stages\":[";
        for (size_t j = 0; j < t.stages.size(); ++j) o << (j ? "," : "") << "{\"t\":\"" << json::esc(t.stages[j].title) << "\",\"x\":\"" << json::esc(t.stages[j].text) << "\"}";
        o << "],\"downstream\":" << arr(t.downstream) << ",\"stops\":" << arr(t.stops) << ",\"inputs\":" << arr(t.inputs) << "}"; }
    o << "],\"factors\":["; for (size_t i = 0; i < A.factors.size(); ++i) { auto& f = A.factors[i]; o << (i ? "," : "") << "{\"id\":\"" << f.id << "\",\"label\":\"" << json::esc(f.label) << "\",\"live\":" << (f.live ? "true" : "false") << ",\"live_state\":\"" << json::esc(f.live_state) << "\",\"note\":\"" << json::esc(f.live_note) << "\",\"states\":[";
        for (size_t j = 0; j < f.states.size(); ++j) { o << (j ? "," : "") << "{\"s\":\"" << f.states[j].state << "\",\"label\":\"" << json::esc(f.states[j].label) << "\",\"dn\":\"" << json::esc(f.states[j].dn) << "\",\"up\":\"" << json::esc(f.states[j].up) << "\"}"; }
        o << "]}"; }
    o << "],\"pairs\":["; for (size_t i = 0; i < A.pairs.size(); ++i) o << (i ? "," : "") << "{\"a\":\"" << A.pairs[i].a << "\",\"b\":\"" << A.pairs[i].b << "\",\"x\":\"" << json::esc(A.pairs[i].text) << "\"}"; o << "]}}"; return o.str(); }
}
