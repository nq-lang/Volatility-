#pragma once
// cross_block.h - the cross-book structural feature block for multibook_v1 (spec 8.2), plus the service that owns the per-(family, ticker view)
// history, refits on cadence (walk-forward) and publishes the model card. Features that need data the feed does not carry are listed as
// UNAVAILABLE and are never imputed (implied correlation needs component IVs; leveraged-ETF rebalance needs fund AUM; VIX slope / G-S ratio /
// WTI-Brent need a book that may not be in scope).
#include <cmath>
#include <deque>
#include <map>
#include "engine.h"
#include "multibook_model.h"
namespace vt::mb {
using linalg::Mat; using linalg::Vec;
inline const std::vector<std::string>& cross_names() {
    static const std::vector<std::string> n = {"Family net GEX sign", "Family net GEX share", "Dist. to family flip %", "Flip dispersion (agreement)", "Skew divergence", "Term-structure divergence", "Weighted OI/gamma concentration"};
    return n; }
inline std::vector<std::string> unavailable_cross() {
    return {"implied-correlation / dispersion proxy: needs component index weights and component IVs (not in the feed)", "leveraged-ETF rebalancing pressure: needs fund AUM (not in the feed)", "cross-asset ratio (VIX slope, gold/silver, WTI-Brent): needs a book/price that is not part of this scope"}; }
inline double wstd(const std::vector<std::pair<double, double>>& v) {   // weighted std of (value, weight)
    double tw = 0, m = 0; for (auto& p : v) { tw += p.second; m += p.first * p.second; } if (tw <= 0) { return 0; } m /= tw; double s = 0; for (auto& p : v) { s += p.second * (p.first - m) * (p.first - m); } return std::sqrt(s / tw); }
// One row of the cross-book block from a scope aggregate. Returns nullopt when fewer than 2 convertible, included books exist (no cross-book information).
inline std::optional<Vec> cross_features(const AggregateResult& a) {
    if (!a.lens || a.lens->anchor_px <= 0) { return std::nullopt; }
    std::vector<std::pair<double, double>> flips, rr, slope; double conc = 0, absn = 0; int nb = 0, nconv = 0;
    for (auto& w : a.scope.books) { if (!w.included) { continue; } auto it = a.books.find(w.id); if (it == a.books.end()) { continue; } ++nb; absn += std::fabs(it->second.net_gex); conc += w.weight * it->second.vol.top3_share; }
    for (auto& lb : a.lens->books) { double wt = 0; for (auto& w : a.scope.books) { if (w.id == lb.id && w.included) { wt = w.weight; } } if (!lb.convertible || wt <= 0) { continue; } ++nconv;
        for (auto& l : lb.levels) { if (l.label == "Gamma Flip" && l.fut) { flips.push_back({*l.fut, wt}); } }
        if (lb.vol.ok) { rr.push_back({lb.vol.rr25, wt}); slope.push_back({lb.vol.term_slope, wt}); } }
    if (nconv < 2 || flips.empty()) { return std::nullopt; }
    double tw = 0, mf = 0; for (auto& p : flips) { tw += p.second; mf += p.first * p.second; } mf /= tw;
    double F = a.lens->anchor_px;
    return Vec{a.raw_net_gex > 0 ? 1.0 : (a.raw_net_gex < 0 ? -1.0 : 0.0), absn > 0 ? a.raw_net_gex / absn : 0.0, (mf - F) / F * 100.0, wstd(flips) / F * 100.0, wstd(rr), wstd(slope), conc}; }
// Owns history + refit cadence. Not enough history => explicit "unavailable" status (single_v1 keeps serving); never a silently degraded model.
class MultibookModelService {
    std::map<std::string, Mat> hist_, anchor_hist_; mbm::Options opt_;
public:
    explicit MultibookModelService(mbm::Options o = {}) : opt_(o) {}
    static std::string key(const std::string& family, const std::string& ticker) { return family + "|" + ticker; }
    // Seed from a stored history (anchor 17-feature rows aligned with cross-block rows), then append live days.
    void load_history(const std::string& family, const std::string& ticker, const Mat& rows) { hist_[key(family, ticker)] = rows; }
    void append_day(const std::string& family, const std::string& ticker, const Vec& anchor17, const Vec& cross) { Vec r = anchor17; r.insert(r.end(), cross.begin(), cross.end()); hist_[key(family, ticker)].push_back(std::move(r)); }
    size_t days(const std::string& family, const std::string& ticker) const { auto it = hist_.find(key(family, ticker)); return it == hist_.end() ? 0 : it->second.size(); }
    // outcome: next-day |return| aligned to the history rows (optional; without it the single_v1 comparison is reported as unavailable)
    std::string status_json(const std::string& family, const std::string& ticker, const std::string& fit_date, const Vec& outcome = {}, bool mock = false) const {
        auto it = hist_.find(key(family, ticker)); size_t n = it == hist_.end() ? 0 : it->second.size();
        if (n < opt_.min_train + opt_.step) return "{\"variant\":\"multibook_v1\",\"available\":false,\"reason\":\"insufficient history: " + std::to_string(n) + " days, need " + std::to_string(opt_.min_train + opt_.step) + "; single_v1 remains the active model\",\"family\":\"" + json::esc(family) + "\",\"ticker_view\":\"" + json::esc(ticker) + "\"}";
        const Mat& rows = it->second; auto wf = mbm::walk_forward(rows, opt_); mbm::CardInputs ci; ci.family = family; ci.ticker_view = ticker; ci.fit_date = fit_date;
        for (size_t j = 0; j < regime::kNumFeatures; ++j) { ci.names.push_back(regime::feature_names()[j]); ci.groups.push_back("anchor"); } for (auto& s : cross_names()) { ci.names.push_back(s); ci.groups.push_back("cross_book"); } ci.pruned_unavailable = unavailable_cross();
        std::string out;
        if (outcome.size() == n) { Mat A; for (auto& r : rows) A.emplace_back(r.begin(), r.begin() + regime::kNumFeatures); auto ws = mbm::walk_forward(A, opt_); auto cmp = mbm::compare(ws, wf, outcome); out = mbm::card_json(ci, wf, &cmp); }
        else out = mbm::card_json(ci, wf, nullptr);
        if (mock) { out.insert(out.size() - 1, ",\"mock\":true"); }
        out.insert(1, "\"available\":true,"); return out; }
};
}
