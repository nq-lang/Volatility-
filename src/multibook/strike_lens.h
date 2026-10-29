#pragma once
// strike_lens.h - translate any book's strikes/levels into the anchor FUTURES price for entries (T2-4).
//   etf_ratio / index_options : strike_F = K * (cash_level / S_book) + (F - cash_level)    [live ratio + live basis]
//     where cash_level = S_cash_ref * cash_scale (NQ->NDX, ES->SPX, YM->DJX*100). No cash ref (GC/CL/SI):
//     strike_F = K * (F / S_book), basis embedded in the ratio (flagged).
//   futures_option            : strike_F = K + (F - S_book)   [S_book = that chain's underlying future; calendar offset]
//   equity/leveraged/vol      : not convertible (single-name / path-dependent / different price space).
#include <cmath>
#include <sstream>
#include "../registry/registry.h"
#include "../bridge/json_util.h"
#include "book_analytics.h"
namespace vt::mb {
enum class LensKind { Ratio, FuturesOption, None };
struct LensLevel { std::string label; double strike{}; double value{}; std::optional<double> fut; };
struct LensBook { std::string id; LensKind kind = LensKind::None; bool convertible = false; double spot{}, ratio = 1, basis = 0; std::string note; std::vector<LensLevel> levels; std::vector<LensLevel> pricing; Insight insight; Analysis an; VolRisk vol; double atm_iv = 0, rr25 = 0, em_pct = 0; int ref_dte = 0; };
struct Lens { std::string epoch; std::string anchor; double anchor_px{}; double tick{}; std::string cash_ref; std::vector<LensBook> books; };
// epoch: deterministic id of one model state (scope + snapshot ts). Every channel of the same state carries it; the UI renders chart and sections only when they match.
inline std::string epoch_of(const std::string& key) { unsigned long long h = 1469598103934665603ULL; for (unsigned char c : key) { h ^= c; h *= 1099511628211ULL; } char b[20]; std::snprintf(b, sizeof b, "%016llx", h); return b; }
inline LensKind kind_of(const reg::SymbolInfo* s) {
    if (!s || s->level == "none") return LensKind::None;   // miners / energy equities: beta proxies, no linear price mapping
    if (s->kind == "etf_options" || s->kind == "index_options") return LensKind::Ratio;
    if (s->kind == "futures_options") return LensKind::FuturesOption;
    return LensKind::None;
}
inline double round_tick(double px, double tick) { return tick > 0 ? std::round(px / tick) * tick : px; }
inline double to_futures(const LensBook& b, double k, double tick) { return round_tick(k * b.ratio + b.basis, tick); }
// vols: optional live volatility-index regime per book id (VIX for S&P books, VXN for Nasdaq books); absent => stated unavailable in every card.
inline Lens build_lens(const reg::Registry& r, const reg::Family& fam, double anchor_px, const std::map<std::string, BookAnalytics>& books, const std::map<std::string, VolIn>* vols = nullptr, const std::map<std::string, CrossIn>* cross = nullptr) {
    Lens L; L.anchor = fam.anchor; L.anchor_px = anchor_px; L.tick = fam.tick; L.cash_ref = fam.cash_ref;
    std::optional<double> cash_level; auto cr = books.find(fam.cash_ref);
    if (!fam.cash_ref.empty() && cr != books.end() && cr->second.spot > 0) cash_level = cr->second.spot * fam.cash_scale;
    for (auto& bd : fam.books) {
        auto it = books.find(bd.id); if (it == books.end()) continue;
        const auto& a = it->second; LensBook lb; lb.id = bd.id; lb.spot = a.spot; lb.kind = kind_of(r.sym(bd.id));
        if (anchor_px <= 0 || a.spot <= 0) { lb.note = "no live anchor or book price"; }
        else if (lb.kind == LensKind::Ratio) {
            lb.convertible = true;
            if (cash_level) { lb.ratio = *cash_level / a.spot; lb.basis = anchor_px - *cash_level; lb.note = "live ratio to " + fam.cash_ref + ", live basis to " + fam.anchor; }
            else { lb.ratio = anchor_px / a.spot; lb.basis = 0; lb.note = "ratio only: no cash index for " + fam.anchor + ", basis embedded in ratio"; }
        } else if (lb.kind == LensKind::FuturesOption) {
            lb.convertible = true; lb.ratio = 1; lb.basis = anchor_px - a.spot; lb.note = "futures option: offset between the option's underlying month and " + fam.anchor + " (calendar spread)";
        } else lb.note = "not convertible: single-name, leveraged or vol product (index-equivalent exposure via beta only)";
        auto add = [&](const char* lab, double k, double v) { LensLevel l{lab, k, v, std::nullopt}; if (lb.convertible) l.fut = to_futures(lb, k, fam.tick); lb.levels.push_back(l); };
        if (a.flip) add("Gamma Flip", *a.flip, 0);
        if (a.call_wall) for (auto& t : a.top) if (t.strike == *a.call_wall) add("Call Wall", t.strike, t.value);
        if (a.put_wall) for (auto& t : a.top) if (t.strike == *a.put_wall) add("Put Wall", t.strike, t.value);
        for (auto& t : a.top) { if ((a.call_wall && t.strike == *a.call_wall) || (a.put_wall && t.strike == *a.put_wall)) continue; add("Top |GEX|", t.strike, t.value); }
        for (auto& pl : a.pricing.levels) { LensLevel l{pl.label, pl.strike, pl.value, std::nullopt}; if (lb.convertible) l.fut = to_futures(lb, pl.strike, fam.tick); lb.pricing.push_back(l); }
        lb.atm_iv = a.pricing.atm_iv; lb.rr25 = a.pricing.rr25; lb.em_pct = a.pricing.em_pct; lb.ref_dte = a.pricing.ref_dte; lb.vol = a.vol; { VolIn v; if (vols) { auto vi = vols->find(bd.id); if (vi != vols->end()) v = vi->second; } lb.insight = finalize(a.insight, v); }
        L.books.push_back(lb);
    }
    { std::vector<const LensBook*> sib; for (auto& x : L.books) sib.push_back(&x); static const std::map<std::string, CrossIn> none;
      for (auto& x : L.books) { VolIn v; if (vols) { auto vi = vols->find(x.id); if (vi != vols->end()) v = vi->second; } auto it = books.find(x.id); if (it != books.end()) x.an = analyze_book(x, it->second, fam.id, v, cross ? *cross : none, sib); } }
    return L;
}
inline std::string vol_json(const VolRisk& v) {
    if (!v.ok) { return "null"; }
    std::ostringstream o; auto n = [](double x) { return json::num(x); }; auto op = [&](const std::optional<double>& x) { return x ? n(*x) : std::string("null"); };
    o << "{\"term\":["; for (size_t i = 0; i < v.term.size(); ++i) o << (i ? "," : "") << "{\"dte\":" << v.term[i].dte << ",\"iv\":" << n(v.term[i].atm_iv) << ",\"rr\":" << n(v.term[i].rr25) << "}";
    o << "],\"slope\":" << n(v.term_slope) << ",\"backwardated\":" << (v.backwardated ? "true" : "false") << ",\"rr25\":" << n(v.rr25) << ",\"put_wing\":" << n(v.put_wing) << ",\"rv\":" << op(v.rv) << ",\"vrp\":" << op(v.vrp)
      << ",\"em_1d\":" << n(v.em_1d) << ",\"em_ratio\":" << op(v.em_ratio) << ",\"hhi\":" << n(v.gamma_hhi) << ",\"top3\":" << n(v.top3_share) << ",\"near_oi\":" << n(v.oi_near_share) << ",\"scen\":[";
    for (size_t i = 0; i < v.scenarios.size(); ++i) o << (i ? "," : "") << "[" << n(v.scenarios[i].shock_pct) << "," << n(v.scenarios[i].dvol) << "," << n(v.scenarios[i].dealer_pnl) << "]";
    o << "]}"; return o.str(); }
inline std::string lens_json(const Lens& L) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); };
    o << "{\"epoch\":\"" << L.epoch << "\",\"anchor\":\"" << json::esc(L.anchor) << "\",\"anchor_px\":" << n(L.anchor_px) << ",\"tick\":" << n(L.tick) << ",\"cash_ref\":\"" << json::esc(L.cash_ref) << "\",\"books\":[";
    for (size_t i = 0; i < L.books.size(); ++i) { auto& b = L.books[i]; if (i) o << ",";
        o << "{\"id\":\"" << json::esc(b.id) << "\",\"convertible\":" << (b.convertible ? "true" : "false") << ",\"spot\":" << n(b.spot) << ",\"ratio\":" << n(b.ratio) << ",\"basis\":" << n(b.basis)
          << ",\"note\":\"" << json::esc(b.note) << "\",\"levels\":[";
        for (size_t j = 0; j < b.levels.size(); ++j) { auto& l = b.levels[j]; if (j) o << ",";
            o << "{\"label\":\"" << l.label << "\",\"strike\":" << n(l.strike) << ",\"value\":" << n(l.value) << ",\"fut\":" << (l.fut ? n(*l.fut) : "null") << "}"; }
        o << "],\"pricing\":[";
        for (size_t j = 0; j < b.pricing.size(); ++j) { auto& l = b.pricing[j]; if (j) o << ",";
            o << "{\"label\":\"" << l.label << "\",\"strike\":" << n(l.strike) << ",\"value\":" << n(l.value) << ",\"fut\":" << (l.fut ? n(*l.fut) : "null") << "}"; }
        o << "],\"atm_iv\":" << n(b.atm_iv) << ",\"rr25\":" << n(b.rr25) << ",\"em_pct\":" << n(b.em_pct) << ",\"ref_dte\":" << b.ref_dte << ",\"vol\":" << vol_json(b.vol) << ",\"insight\":" << insight_json(b.insight) << ",\"an\":" << analysis_json(b.an) << "}"; }
    o << "]}"; return o.str();
}
}
