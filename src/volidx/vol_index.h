#pragma once
// vol_index.h - analysis of volatility indices (VIX, VXN, ...). Pure functions over a supplied daily history; nothing is invented:
//   no/short history -> available=false (or low confidence) with the reason, never a guessed regime.
// Regime is percentile-based on the supplied window (not fixed level cut-offs), so it adapts to the vol era in the data.
// Shared by: Volatility section (C-4), Strike Lens regime (C-3), Hedge Flow, Dealer Flow sidebar, Pricing risk/distribution.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "../bridge/json_util.h"
#include "../registry/registry.h"
namespace vt::volidx {
enum class Regime { Unknown, Compressed, Normal, Elevated, Stress };
inline const char* regime_name(Regime r) { switch (r) { case Regime::Compressed: return "compressed"; case Regime::Normal: return "normal"; case Regime::Elevated: return "elevated"; case Regime::Stress: return "stress"; default: return "unknown"; } }
struct Bar { long long ts{}; double close{}; };                       // ts: epoch seconds of the session date
struct Series {
    std::string id;                                                   // "VIX" / "VXN"
    std::vector<Bar> daily;                                           // oldest -> newest; last bar may be today's live level
    std::optional<double> front, back; int front_dte = 0, back_dte = 0; // optional term points (e.g. 9d / 3m index levels) - absent => term unavailable
    std::optional<double> realized;                                   // annualised realised vol of the linked underlying (decimal), if supplied
    long long as_of{};
};
struct Analysis {
    std::string id, measures; bool available = false; std::string reason;
    double level = 0; int n = 0; long long as_of = 0;
    double pct_rank = 0, z = 0, hi = 0, lo = 0, range_pos = 0;         // percentile (0..1) / z-score / window high-low / position in range (0..1)
    std::optional<double> chg_1d, chg_5d, chg_1d_pct, chg_5d_pct;      // points / percent
    int trend = 0;                                                    // +1 rising, -1 falling, 0 flat (|5d chg| < 0.5 sd of daily changes)
    Regime regime = Regime::Unknown;
    std::string term_state = "unavailable"; std::optional<double> term_slope; // contango/backwardation; slope = (back-front)/front
    std::optional<double> iv_rv;                                      // level/100 minus realised (vol pts, decimal) when realised supplied
    double confidence = 0;                                            // 0..1, from history length and window coverage
    std::vector<std::string> books;                                   // linked books (registry vol_links)
    std::string anchor;                                               // linked futures anchor
    std::string effect;                                               // plain-language dealer-hedging consequence derived from regime + trend + term
    std::vector<std::string> notes;
};
struct Divergence { bool available = false; std::string reason; double spread = 0, spread_z = 0, ratio = 0, ratio_pct = 0; std::string state, text; };
inline double mean_(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / v.size(); }
inline double sd_(const std::vector<double>& v) { if (v.size() < 2) return 0; double m = mean_(v), s = 0; for (double x : v) s += (x - m) * (x - m); return std::sqrt(s / (v.size() - 1)); }
inline std::string ord(int n) { int m = n % 100, k = n % 10; return std::to_string(n) + ((m >= 11 && m <= 13) ? "th" : k == 1 ? "st" : k == 2 ? "nd" : k == 3 ? "rd" : "th"); }
inline std::string f1(double v, int d = 1) { std::ostringstream o; o.setf(std::ios::fixed); o.precision(d); o << v; return o.str(); }
// min_obs: below this the regime/percentile are withheld (not guessed).
inline Analysis analyze(const Series& s, const reg::Registry* = nullptr, size_t window = 252, size_t min_obs = 60) {
    Analysis a; a.id = s.id; a.as_of = s.as_of;
    if (s.daily.empty()) { a.reason = "no " + s.id + " history supplied"; return a; }
    std::vector<double> w; size_t from = s.daily.size() > window ? s.daily.size() - window : 0;
    for (size_t i = from; i < s.daily.size(); ++i) if (s.daily[i].close > 0) w.push_back(s.daily[i].close);
    a.n = (int)w.size(); a.level = w.empty() ? 0 : w.back(); if (!a.as_of) a.as_of = s.daily.back().ts;
    if (a.n < 2) { a.reason = "fewer than 2 valid observations"; return a; }
    a.available = true; a.hi = *std::max_element(w.begin(), w.end()); a.lo = *std::min_element(w.begin(), w.end());
    a.range_pos = a.hi > a.lo ? (a.level - a.lo) / (a.hi - a.lo) : 0.5;
    if ((size_t)a.n >= min_obs) {
        size_t below = 0; for (double x : w) if (x < a.level) ++below; a.pct_rank = (double)below / (a.n - 1 > 0 ? a.n - 1 : 1); if (a.pct_rank > 1) a.pct_rank = 1;
        double sd = sd_(w); a.z = sd > 0 ? (a.level - mean_(w)) / sd : 0;
        a.regime = a.pct_rank < 0.20 ? Regime::Compressed : a.pct_rank < 0.60 ? Regime::Normal : a.pct_rank < 0.85 ? Regime::Elevated : Regime::Stress;
    } else a.notes.push_back("only " + std::to_string(a.n) + " observations (< " + std::to_string(min_obs) + "): percentile, z-score and regime withheld");
    if (w.size() >= 2) { a.chg_1d = w.back() - w[w.size() - 2]; a.chg_1d_pct = *a.chg_1d / w[w.size() - 2]; }
    if (w.size() >= 6) { a.chg_5d = w.back() - w[w.size() - 6]; a.chg_5d_pct = *a.chg_5d / w[w.size() - 6];
        std::vector<double> dd; for (size_t i = 1; i < w.size(); ++i) dd.push_back(w[i] - w[i - 1]); double sdd = sd_(dd);
        a.trend = (sdd > 0 && std::fabs(*a.chg_5d) > 0.5 * sdd) ? (*a.chg_5d > 0 ? 1 : -1) : 0; }
    else a.notes.push_back("fewer than 6 observations: 5-day change and trend unavailable");
    if (s.front && s.back && *s.front > 0) { a.term_slope = (*s.back - *s.front) / *s.front; a.term_state = *a.term_slope < -0.01 ? "backwardation" : *a.term_slope > 0.01 ? "contango" : "flat"; }
    else a.notes.push_back("term structure unavailable (no front/back index levels supplied)");
    if (s.realized) a.iv_rv = a.level / 100.0 - *s.realized; else a.notes.push_back("implied-vs-realised unavailable (no realised vol supplied)");
    a.confidence = std::min(1.0, (double)a.n / (double)window) * (a.regime == Regime::Unknown ? 0.5 : 1.0);
    // dealer-hedging consequence: only statements that follow mechanically from regime / trend / term; no price targets
    std::string e;
    if (a.regime != Regime::Unknown) {
        e = s.id + " at " + f1(a.level) + " is in the " + ord((int)std::lround(a.pct_rank * 100)) + " percentile of its last " + std::to_string(a.n) + " sessions (" + regime_name(a.regime) + "). ";
        if (a.regime == Regime::Compressed) e += "Low implied vol keeps option gamma concentrated near spot and vanna small, so dealer hedging tends to dampen moves, but a vol spike from a compressed base forces the largest relative re-hedging. ";
        else if (a.regime == Regime::Normal) e += "Mid-range implied vol: gamma/vanna hedging is not extreme either way; the net-gamma sign of the book decides whether flows dampen or amplify. ";
        else if (a.regime == Regime::Elevated) e += "Elevated implied vol widens the strikes that carry gamma and enlarges vanna: falling vol makes dealers buy back hedges (supportive), rising vol makes them sell (pressure). ";
        else e += "Stress-level implied vol: vanna and vomma flows are at their largest; hedges swing with every vol change and moves can extend (negative gamma) or snap back sharply on a vol crush. ";
        if (a.trend > 0) e += "Vol is rising over 5 days (" + f1(*a.chg_5d, 2) + " pts): vanna flow leans to dealer selling of the underlying. ";
        else if (a.trend < 0) e += "Vol is falling over 5 days (" + f1(*a.chg_5d, 2) + " pts): vanna flow leans to dealer buying of the underlying. ";
    } else e = "Regime withheld: " + (a.notes.empty() ? std::string("insufficient history") : a.notes.front()) + ". ";
    if (a.term_state == "backwardation") e += "Term structure is backwardated: the market prices near-term stress above later vol.";
    else if (a.term_state == "contango") e += "Term structure is in contango: near-term vol is priced below later vol (normal, calm carry).";
    a.effect = e; return a;
}
// VXN vs VIX: the Nasdaq-100 vs S&P 500 vol premium, judged against its own history over the common dates.
inline Divergence divergence(const Series& vxn, const Series& vix, size_t window = 252, size_t min_obs = 60) {
    Divergence d; std::map<long long, double> m; for (auto& b : vix.daily) if (b.close > 0) m[b.ts] = b.close;
    std::vector<double> sp, rt; for (auto& b : vxn.daily) { auto it = m.find(b.ts); if (it != m.end() && b.close > 0) { sp.push_back(b.close - it->second); rt.push_back(b.close / it->second); } }
    if (sp.size() < min_obs) { d.reason = "fewer than " + std::to_string(min_obs) + " common sessions for VXN-VIX"; return d; }
    if (sp.size() > window) { sp.erase(sp.begin(), sp.end() - window); rt.erase(rt.begin(), rt.end() - window); }
    d.available = true; d.spread = sp.back(); d.ratio = rt.back(); double sd = sd_(sp); d.spread_z = sd > 0 ? (d.spread - mean_(sp)) / sd : 0;
    size_t below = 0; for (double x : rt) if (x < d.ratio) ++below; d.ratio_pct = (double)below / (rt.size() - 1);
    d.state = d.spread_z > 1.0 ? "tech_stress" : d.spread_z < -1.0 ? "broad_stress" : "aligned";
    d.text = "VXN - VIX = " + f1(d.spread, 2) + " pts (" + f1(d.spread_z, 1) + " sd vs its window; ratio " + f1(d.ratio, 2) + ", " + ord((int)std::lround(d.ratio_pct * 100)) + " pct). ";
    d.text += d.state == "tech_stress" ? "Nasdaq-100 options are pricing unusually more risk than the S&P 500: dealer hedging pressure is concentrated in QQQ/NDX/NQ rather than SPX/ES." :
              d.state == "broad_stress" ? "S&P 500 vol is rich relative to Nasdaq-100: stress is broad-market rather than tech-specific." : "The two indices agree: no index-specific vol dislocation.";
    return d;
}
inline std::string analysis_json(const Analysis& a) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); }; auto op = [&](const std::optional<double>& v) { return v ? n(*v) : std::string("null"); };
    o << "{\"id\":\"" << json::esc(a.id) << "\",\"available\":" << (a.available ? "true" : "false") << ",\"reason\":\"" << json::esc(a.reason) << "\",\"as_of\":" << a.as_of << ",\"level\":" << n(a.level) << ",\"n\":" << a.n
      << ",\"pct_rank\":" << n(a.pct_rank) << ",\"z\":" << n(a.z) << ",\"hi\":" << n(a.hi) << ",\"lo\":" << n(a.lo) << ",\"range_pos\":" << n(a.range_pos) << ",\"chg_1d\":" << op(a.chg_1d) << ",\"chg_5d\":" << op(a.chg_5d)
      << ",\"chg_1d_pct\":" << op(a.chg_1d_pct) << ",\"chg_5d_pct\":" << op(a.chg_5d_pct) << ",\"trend\":" << a.trend << ",\"regime\":\"" << regime_name(a.regime) << "\",\"term_state\":\"" << a.term_state << "\",\"term_slope\":" << op(a.term_slope)
      << ",\"iv_rv\":" << op(a.iv_rv) << ",\"confidence\":" << n(a.confidence) << ",\"anchor\":\"" << json::esc(a.anchor) << "\",\"books\":[";
    for (size_t i = 0; i < a.books.size(); ++i) { o << (i ? "," : "") << "\"" << json::esc(a.books[i]) << "\""; }
    o << "],\"measures\":\"" << json::esc(a.measures) << "\",\"effect\":\"" << json::esc(a.effect) << "\",\"notes\":[";
    for (size_t i = 0; i < a.notes.size(); ++i) { o << (i ? "," : "") << "\"" << json::esc(a.notes[i]) << "\""; }
    o << "]}"; return o.str();
}
inline std::string divergence_json(const Divergence& d) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); };
    o << "{\"available\":" << (d.available ? "true" : "false") << ",\"reason\":\"" << json::esc(d.reason) << "\",\"spread\":" << n(d.spread) << ",\"spread_z\":" << n(d.spread_z) << ",\"ratio\":" << n(d.ratio) << ",\"ratio_pct\":" << n(d.ratio_pct) << ",\"state\":\"" << d.state << "\",\"text\":\"" << json::esc(d.text) << "\"}"; return o.str();
}
// Payload for the UI: every registry volatility ticker analysed (missing series -> available:false), links from vol_links, plus VXN/VIX divergence when both exist.
inline std::string volatility_payload(const reg::Registry& r, const std::map<std::string, Series>& feed, const std::map<std::string, std::vector<std::string>>& links, const std::map<std::string, std::string>& measures) {
    std::ostringstream o; o << "{\"indices\":{"; bool first = true;
    for (auto& id : r.volatility) { Analysis a; auto it = feed.find(id); if (it == feed.end()) { a.id = id; a.reason = "no " + id + " feed connected"; } else a = analyze(it->second, &r);
        a.id = id; auto l = links.find(id); if (l != links.end()) a.books = l->second; auto an = r.vol_anchor.find(id); if (an != r.vol_anchor.end()) a.anchor = an->second; auto m = measures.find(id); if (m != measures.end()) a.measures = m->second;
        o << (first ? "" : ",") << "\"" << json::esc(id) << "\":" << analysis_json(a); first = false; }
    o << "},\"divergence\":"; auto x = feed.find("VXN"), v = feed.find("VIX");
    if (x != feed.end() && v != feed.end()) o << divergence_json(divergence(x->second, v->second)); else { Divergence d; d.reason = "needs both VXN and VIX history"; o << divergence_json(d); }
    o << "}"; return o.str();
}
}
