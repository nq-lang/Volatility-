#pragma once
// multibook_model.h - `multibook_v1` (T2-7): an explicit, versioned, opt-in regime variant for Multi-Book / Family scope.
// It is a SEPARATE model: its own standardization, PCA and GMM per (family, ticker view). It only *reads* the existing feature order and
// archetype labelling from regime_model.h; nothing in regime_model.h / single_v1 is modified or shared at runtime.
//   inputs      : the 17 anchor features (columns 0..16, same order as single_v1) + a cross-book block (columns 17..).
//   selection   : PCA dims by cumulative-variance target (cap 4); GMM components by forward-chained held-out log-likelihood (BIC reported).
//   validation  : walk-forward only (expanding window, refit every `step` days; day t is labelled by a model that never saw day >= t0).
//   alignment   : after each refit, components are matched to the previous fit by feature-space centroid so "Trending" stays "Trending".
//   comparison  : same days vs single_v1, effect = between-regime share of outcome variance (eta^2) with a permutation test.
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "vt/gmm.h"
#include "vt/pca.h"
#include "../core/regime_model.h"
#include "../bridge/json_util.h"
namespace vt::mbm {
using linalg::Mat; using linalg::Vec;
struct Options { size_t min_train = 120, step = 10, max_pc = 4, min_pc = 2; double var_target = 0.70, corr_prune = 0.95, val_frac = 0.25; std::vector<size_t> k_grid{3, 4, 5}; unsigned seed = 42; };
struct Fit {
    size_t n = 0, kpc = 0, K = 0; std::vector<size_t> keep, dropped;                // dropped: columns pruned before PCA (constant / collinear)
    pca::StandardizationParams std; Mat loadings; Vec eig; double total_var = 0, cum_var = 0, bic = 0, val_ll = 0;
    gmm::GmmModel gmm; std::vector<int> comp_regime;                                // component -> canonical regime id (regime::RegimeName order); -1 = unnamed
    Mat profile;                                                                    // component x ALL columns, standardized centroid (basis-free: used for alignment)
    bool aligned = false; double align_cost = 0;
};
struct Posterior { Vec pc; std::array<double, 5> p{}; int top = -1; double maxp = 0; };
inline Vec standardize_all(const Vec& r, const Fit& f) { return pca::standardize(r, f.std); }
inline Vec project(const Fit& f, const Vec& z) { Vec o(f.kpc, 0); for (size_t c = 0; c < f.kpc; ++c) for (size_t j = 0; j < f.keep.size(); ++j) o[c] += z[f.keep[j]] * f.loadings[j][c]; return o; }
inline Posterior predict(const Fit& f, const Vec& raw) {
    Posterior r; r.pc = project(f, standardize_all(raw, f)); auto resp = gmm::responsibilities(r.pc, f.gmm);
    for (size_t k = 0; k < resp.size(); ++k) if (f.comp_regime[k] >= 0 && f.comp_regime[k] < 5) r.p[f.comp_regime[k]] += resp[k];
    for (int i = 0; i < 5; ++i) if (r.p[i] > r.maxp) { r.maxp = r.p[i]; r.top = i; }
    return r;
}
namespace detail {
inline double loglik(const gmm::GmmModel& m, const Mat& X) { double ll = 0; for (auto& x : X) { double s = 0; for (auto& c : m.components) s += c.weight * gmm::gaussian_density(x, c); ll += std::log(std::max(s, 1e-300)); } return ll; }
inline double corr(const Mat& Z, size_t a, size_t b) { double s = 0; for (auto& r : Z) s += r[a] * r[b]; return s / std::max<size_t>(1, Z.size() - 1); }
}
inline Fit fit(const Mat& rows, const Fit* prev, const Options& o) {
    Fit f; f.n = rows.size(); size_t d = rows.empty() ? 0 : rows[0].size(); if (f.n < 30 || d < 3) return f;
    f.std.means = linalg::column_means(rows); f.std.stdevs = linalg::column_stdevs(rows, f.std.means);
    Mat Z; Z.reserve(f.n); for (auto& r : rows) Z.push_back(pca::standardize(r, f.std));
    for (size_t j = 0; j < d; ++j) { if (!(f.std.stdevs[j] > 1e-9)) { f.dropped.push_back(j); continue; } bool dup = false;
        for (size_t k : f.keep) if (std::fabs(detail::corr(Z, j, k)) > o.corr_prune) { dup = true; break; }
        (dup ? f.dropped : f.keep).push_back(j); }
    if (f.keep.size() < 3) { f.keep.clear(); return f; }
    Mat Zk; Zk.reserve(f.n); for (auto& z : Z) { Vec v; for (size_t j : f.keep) v.push_back(z[j]); Zk.push_back(std::move(v)); }
    auto full = linalg::jacobi_eigen(linalg::covariance(Zk, linalg::column_means(Zk))); for (double v : full.values) f.total_var += v;
    size_t kmax = std::min(o.max_pc, f.keep.size()), kp = o.min_pc; double acc = 0; Vec sorted = full.values; std::sort(sorted.rbegin(), sorted.rend());
    for (size_t i = 0; i < kmax; ++i) { acc += sorted[i]; kp = std::max(o.min_pc, i + 1); if (acc / f.total_var >= o.var_target) break; }
    kp = std::min(kp, kmax); auto top = linalg::top_k_eigen(full, kp); f.kpc = kp; f.loadings = top.vectors; f.eig = top.values; double s = 0; for (double v : f.eig) s += v; f.cum_var = f.total_var > 1e-12 ? s / f.total_var : 0;
    Mat P; P.reserve(f.n); for (auto& z : Zk) { Vec v(kp, 0); for (size_t c = 0; c < kp; ++c) for (size_t j = 0; j < z.size(); ++j) v[c] += z[j] * f.loadings[j][c]; P.push_back(std::move(v)); }
    // GMM size: forward-chained validation (fit on the first part, score the later part), time order preserved
    size_t cut = static_cast<size_t>(f.n * (1 - o.val_frac)); Mat Ptr(P.begin(), P.begin() + cut), Pva(P.begin() + cut, P.end()); double best = -1e300; f.K = o.k_grid.front();
    for (size_t K : o.k_grid) { if (cut < K * 8) continue; auto m = gmm::fit(Ptr, K, 100, 1e-5, o.seed, 3); double ll = detail::loglik(m, Pva) / std::max<size_t>(1, Pva.size()); if (ll > best) { best = ll; f.K = K; f.val_ll = ll; } }
    f.gmm = gmm::fit(P, f.K, 200, 1e-6, o.seed, 5); size_t npar = f.K * (kp + kp * (kp + 1) / 2) + f.K - 1; f.bic = -2 * f.gmm.final_log_likelihood + npar * std::log(static_cast<double>(f.n));
    f.profile.assign(f.K, Vec(d, 0.0)); std::vector<double> wsum(f.K, 0.0);
    for (size_t i = 0; i < f.n; ++i) { auto r = gmm::responsibilities(P[i], f.gmm); for (size_t k = 0; k < f.K; ++k) { wsum[k] += r[k]; for (size_t j = 0; j < d; ++j) f.profile[k][j] += r[k] * Z[i][j]; } }
    for (size_t k = 0; k < f.K; ++k) if (wsum[k] > 1e-9) for (double& v : f.profile[k]) v /= wsum[k];
    f.comp_regime.assign(f.K, -1);
    size_t da = std::min<size_t>(regime::kNumFeatures, d);
    if (prev && prev->K > 0 && prev->profile[0].size() == d) {          // alignment to the previous batch by anchor-feature centroid distance (greedy bijection)
        std::vector<bool> cu(f.K, false), pu(prev->K, false); f.aligned = true;
        for (size_t step = 0; step < std::min(f.K, prev->K); ++step) { double bd = 1e300; size_t bc = 0, bp = 0;
            for (size_t c = 0; c < f.K; ++c) { if (cu[c]) continue; for (size_t p = 0; p < prev->K; ++p) { if (pu[p]) continue; double dd = 0; for (size_t j = 0; j < da; ++j) { double x = f.profile[c][j] - prev->profile[p][j]; dd += x * x; } if (dd < bd) { bd = dd; bc = c; bp = p; } } }
            cu[bc] = pu[bp] = true; f.comp_regime[bc] = prev->comp_regime[bp]; f.align_cost += bd; }
        std::vector<bool> used(5, false); for (int r : f.comp_regime) if (r >= 0) used[r] = true;
        for (size_t c = 0; c < f.K; ++c) if (f.comp_regime[c] < 0) for (int r = 0; r < 5; ++r) if (!used[r]) { f.comp_regime[c] = r; used[r] = true; break; }
    } else if (d >= regime::kNumFeatures) {                              // first fit: names from the SAME archetype scoring single_v1 uses (read-only call)
        std::vector<Vec> prof; for (size_t k = 0; k < f.K; ++k) prof.emplace_back(f.profile[k].begin(), f.profile[k].begin() + regime::kNumFeatures);
        auto m = regime::derive_label_mapping(prof); for (size_t k = 0; k < f.K; ++k) f.comp_regime[k] = static_cast<int>(m.cluster_to_regime[k]);
    } else for (size_t k = 0; k < f.K; ++k) f.comp_regime[k] = static_cast<int>(k % 5);
    return f;
}
struct RefitInfo { size_t t0 = 0, n = 0, kpc = 0, K = 0; double cum_var = 0, bic = 0, min_cos = 1, overlap_agree = 1; size_t pruned = 0; bool aligned = false; };
struct WalkForward {
    std::vector<int> label; Vec maxp; std::vector<std::array<double, 5>> post; std::vector<int> hind; std::vector<RefitInfo> refits; Fit last; size_t first = 0;
    double dwell = 0, flip_freq = 0, ece = 0, min_cos = 1, mean_overlap = 1, mean_cum_var = 0; size_t scored = 0;
};
inline WalkForward walk_forward(const Mat& rows, const Options& o) {
    WalkForward w; size_t n = rows.size(); w.label.assign(n, -1); w.maxp.assign(n, 0); w.post.assign(n, {}); w.hind.assign(n, -1); w.first = o.min_train; if (n <= o.min_train) return w;
    Fit prev; bool have = false;
    for (size_t t0 = o.min_train; t0 < n; t0 += o.step) {
        Mat train(rows.begin(), rows.begin() + t0);                              // rows [0, t0) ONLY: no look-ahead in standardization, PCA or GMM
        Fit f = fit(train, have ? &prev : nullptr, o); if (f.K == 0) continue;
        RefitInfo ri{t0, f.n, f.kpc, f.K, f.cum_var, f.bic, 1, 1, f.dropped.size(), f.aligned};
        if (have) {                                                              // stability vs previous refit (sign-free loading cosine on shared kept columns) + label agreement on the overlap
            double mc = 1; for (size_t c = 0; c < std::min(f.kpc, prev.kpc); ++c) { double a = 0, b = 0, ab = 0; for (size_t j = 0; j < f.keep.size(); ++j) { auto it = std::find(prev.keep.begin(), prev.keep.end(), f.keep[j]); if (it == prev.keep.end()) continue; double x = f.loadings[j][c], y = prev.loadings[it - prev.keep.begin()][c]; ab += x * y; a += x * x; b += y * y; }
                mc = std::min(mc, a > 0 && b > 0 ? std::fabs(ab) / std::sqrt(a * b) : 0.0); } ri.min_cos = mc;
            size_t agree = 0, tot = 0; for (size_t i = t0 - o.step; i < t0; ++i) { auto pn = predict(f, rows[i]); ++tot; if (pn.top == w.label[i]) ++agree; if (w.label[i] >= 0) w.hind[i] = pn.top; } ri.overlap_agree = tot ? double(agree) / tot : 1; }
        for (size_t i = t0; i < std::min(n, t0 + o.step); ++i) { auto p = predict(f, rows[i]); w.label[i] = p.top; w.maxp[i] = p.maxp; w.post[i] = p.p; }
        w.refits.push_back(ri); prev = f; have = true; w.last = f;
    }
    std::vector<int> L; for (size_t i = 0; i < n; ++i) if (w.label[i] >= 0) L.push_back(w.label[i]); w.scored = L.size();
    if (L.size() > 1) { size_t flips = 0, runs = 1; for (size_t i = 1; i < L.size(); ++i) if (L[i] != L[i - 1]) { ++flips; ++runs; } w.flip_freq = double(flips) / (L.size() - 1); w.dwell = double(L.size()) / runs; }
    // calibration: does the OOS max-posterior match how often a later model (which has seen the day) agrees with the label? (5 bins, ECE)
    std::array<double, 5> bs{}, bp{}, ba{}; for (size_t i = 0; i < n; ++i) if (w.hind[i] >= 0 && w.label[i] >= 0) { int b = std::min(4, int(w.maxp[i] * 5)); bs[b] += 1; bp[b] += w.maxp[i]; ba[b] += (w.hind[i] == w.label[i]); }
    double tot = 0; for (int b = 0; b < 5; ++b) tot += bs[b]; for (int b = 0; b < 5; ++b) if (bs[b] > 0) w.ece += (bs[b] / tot) * std::fabs(bp[b] / bs[b] - ba[b] / bs[b]);
    double cv = 0; for (auto& r : w.refits) { w.min_cos = std::min(w.min_cos, r.min_cos); cv += r.cum_var; w.mean_overlap += r.overlap_agree; } if (!w.refits.empty()) { w.mean_cum_var = cv / w.refits.size(); w.mean_overlap = (w.mean_overlap - 1) / std::max<size_t>(1, w.refits.size()); }
    return w;
}
// ---- comparison against single_v1 on the same days ----
inline double eta2(const std::vector<int>& lab, const Vec& y, const std::vector<size_t>& idx) {
    std::map<int, std::pair<double, size_t>> g; double m = 0; for (size_t i : idx) { g[lab[i]].first += y[i]; g[lab[i]].second++; m += y[i]; } m /= idx.size();
    double ssb = 0, sst = 0; for (auto& kv : g) { double gm = kv.second.first / kv.second.second; ssb += kv.second.second * (gm - m) * (gm - m); } for (size_t i : idx) sst += (y[i] - m) * (y[i] - m); return sst > 0 ? ssb / sst : 0; }
struct Comparison { size_t days = 0; double agreement = 0, eta_single = 0, eta_multi = 0, delta = 0, p_value = 1; bool adds_information = false; std::string verdict; };
inline Comparison compare(const WalkForward& single, const WalkForward& multi, const Vec& outcome, unsigned seed = 7, int perms = 300) {
    Comparison c; std::vector<size_t> idx; for (size_t i = 0; i < outcome.size() && i < single.label.size() && i < multi.label.size(); ++i) if (single.label[i] >= 0 && multi.label[i] >= 0) idx.push_back(i);
    c.days = idx.size(); if (idx.size() < 30) { c.verdict = "insufficient overlapping out-of-sample days"; return c; }
    size_t ag = 0; for (size_t i : idx) ag += single.label[i] == multi.label[i]; c.agreement = double(ag) / idx.size();
    c.eta_single = eta2(single.label, outcome, idx); c.eta_multi = eta2(multi.label, outcome, idx); c.delta = c.eta_multi - c.eta_single;
    std::mt19937 g(seed); Vec y = outcome; int ge = 0; for (int p = 0; p < perms; ++p) { std::shuffle(y.begin(), y.end(), g); if (eta2(multi.label, y, idx) - eta2(single.label, y, idx) >= c.delta) ++ge; } c.p_value = double(ge + 1) / (perms + 1);
    c.adds_information = c.delta > 0.02 && c.p_value < 0.05;
    c.verdict = c.adds_information ? "multibook_v1 separates next-day outcomes better than single_v1 out of sample" : "no demonstrated out-of-sample gain over single_v1; the family block is not shown to add information";
    return c; }
// ---- model card (inputs, fit date, n, validation, known limits), served to the UI ----
struct CardInputs { std::string family, ticker_view, fit_date; std::vector<std::string> names, groups, pruned_unavailable; };
inline std::string card_json(const CardInputs& in, const WalkForward& wf, const Comparison* cmp) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); }; const Fit& f = wf.last;
    o << "{\"variant\":\"multibook_v1\",\"family\":\"" << json::esc(in.family) << "\",\"ticker_view\":\"" << json::esc(in.ticker_view) << "\",\"fit_date\":\"" << json::esc(in.fit_date) << "\",\"n_train\":" << f.n << ",\"n_features\":" << in.names.size()
      << ",\"pca_components\":" << f.kpc << ",\"gmm_components\":" << f.K << ",\"cum_explained_variance\":" << n(f.cum_var) << ",\"low_variance_warning\":" << (f.cum_var < 0.6 ? "true" : "false")
      << ",\"samples_per_feature\":" << n(in.names.empty() ? 0 : double(f.n) / in.names.size()) << ",\"bic\":" << n(f.bic) << ",\"features\":[";
    for (size_t i = 0; i < in.names.size(); ++i) { bool dropped = std::find(f.dropped.begin(), f.dropped.end(), i) != f.dropped.end(); o << (i ? "," : "") << "{\"name\":\"" << json::esc(in.names[i]) << "\",\"group\":\"" << json::esc(in.groups[i]) << "\",\"used\":" << (dropped ? "false" : "true") << "}"; }
    o << "],\"unavailable_features\":["; for (size_t i = 0; i < in.pruned_unavailable.size(); ++i) o << (i ? "," : "") << "\"" << json::esc(in.pruned_unavailable[i]) << "\"";
    o << "],\"walk_forward\":{\"scored_days\":" << wf.scored << ",\"refits\":" << wf.refits.size() << ",\"avg_dwell_days\":" << n(wf.dwell) << ",\"flip_frequency\":" << n(wf.flip_freq) << ",\"calibration_ece\":" << n(wf.ece) << ",\"min_loading_cos\":" << n(wf.min_cos) << ",\"mean_label_agreement_across_refits\":" << n(wf.mean_overlap) << "}";
    if (cmp) o << ",\"vs_single_v1\":{\"days\":" << cmp->days << ",\"label_agreement\":" << n(cmp->agreement) << ",\"eta2_single\":" << n(cmp->eta_single) << ",\"eta2_multi\":" << n(cmp->eta_multi) << ",\"delta\":" << n(cmp->delta) << ",\"p_value\":" << n(cmp->p_value) << ",\"adds_information\":" << (cmp->adds_information ? "true" : "false") << ",\"verdict\":\"" << json::esc(cmp->verdict) << "\"}";
    o << ",\"known_limits\":[\"positioning is inferred (dealers long calls / short puts assumed), not observed\",\"~230 training days is small for a block of added features; collinear features are pruned per fit, not overfit\",\"regime labels are archetype-matched to the anchor features, then carried across refits by centroid matching\",\"features that need data not in the feed are listed as unavailable and are not imputed\"]}";
    return o.str(); }
}
