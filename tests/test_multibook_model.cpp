#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include "../src/multibook/multibook_model.h"
#include "../src/registry/mini_json.h"
using namespace vt; using namespace vt::mbm;
struct Data { Mat rows; Vec outcome; std::vector<int> truth; };
// Regime-switching synthetic history. 17 anchor columns (archetype-structured) + 7 cross-block columns (informative or pure noise).
static Data gen(size_t n, double anchor_noise, bool cross_informative, unsigned seed) {
    std::mt19937 g(seed); std::normal_distribution<double> N(0, 1); Data d; int r = 0; const double scale[5] = {0.004, 0.014, 0.011, 0.007, 0.009};
    Mat am(5, Vec(17, 0.0)), cm(5, Vec(7, 0.0));
    for (int k = 0; k < 5; ++k) { for (int j = 0; j < 17; ++j) am[k][j] = 0.4 * N(g); for (int j = 0; j < 7; ++j) cm[k][j] = 1.6 * N(g); }
    am[0][regime::NetGEX_z] = 2; am[0][regime::FlipDistancePct] = 1; am[0][regime::Speed_z] = -1; am[1][regime::ImpliedZ_ATM] = 2; am[1][regime::RvIvZscoreTod] = 2;
    am[2][regime::NetGEX_z] = -2; am[2][regime::FlipDistancePct] = -1; am[2][regime::Speed_z] = 1.5; am[3][regime::NetGEX_z] = 0.5; am[4][regime::Speed_z] = 0.5;
    for (size_t t = 0; t < n; ++t) { if (std::uniform_real_distribution<double>(0, 1)(g) > 0.88) r = int(g() % 5);
        Vec row(24); for (int j = 0; j < 17; ++j) row[j] = am[r][j] + anchor_noise * N(g); for (int j = 0; j < 7; ++j) row[17 + j] = (cross_informative ? cm[r][j] : 0.0) + (cross_informative ? 0.5 : 1.0) * N(g);
        d.rows.push_back(row); d.truth.push_back(r); d.outcome.push_back(scale[r] * std::fabs(N(g)) + 0.001); }
    return d; }
static Mat anchor_only(const Mat& m) { Mat o; for (auto& r : m) o.emplace_back(r.begin(), r.begin() + 17); return o; }
int main() {
    Options o; o.step = 20; o.min_train = 120; o.k_grid = {4, 5};
    auto t0 = std::chrono::steady_clock::now();
    // ---- (1) walk-forward runs, labels only after warm-up, canonical names stable
    auto A = gen(300, 0.9, true, 11); auto wf = walk_forward(A.rows, o);
    for (size_t i = 0; i < 120; ++i) assert(wf.label[i] == -1);
    assert(wf.scored == 180 && wf.refits.size() == 9 && wf.last.K >= 4 && wf.last.kpc >= 2 && wf.last.kpc <= 4 && wf.last.cum_var > 0.3);
    for (size_t i = 120; i < 300; ++i) { assert(wf.label[i] >= 0 && wf.label[i] < 5); double s = 0; for (double p : wf.post[i]) s += p; assert(std::fabs(s - 1) < 1e-9); }
    assert(wf.dwell >= 1.0 && wf.flip_freq >= 0 && wf.flip_freq <= 1 && wf.ece >= 0 && wf.ece <= 1 && wf.min_cos > 0.5);
    for (size_t i = 1; i < wf.refits.size(); ++i) assert(wf.refits[i].aligned);
    // ---- (2) LEAKAGE: scramble every row from day T onward; every out-of-sample output before T is bit-identical
    { size_t T = 200; Mat B = A.rows; std::mt19937 g(5); for (size_t i = T; i < B.size(); ++i) for (auto& v : B[i]) v = 50.0 * std::normal_distribution<double>(0, 1)(g);
      auto wb = walk_forward(B, o); for (size_t i = 0; i < T; ++i) { assert(wb.label[i] == wf.label[i]); assert(wb.maxp[i] == wf.maxp[i]); for (int k = 0; k < 5; ++k) assert(wb.post[i][k] == wf.post[i][k]); }
      for (auto& r : wf.refits) if (r.t0 <= T) { bool found = false; for (auto& q : wb.refits) if (q.t0 == r.t0) { found = true; assert(q.bic == r.bic && q.kpc == r.kpc && q.K == r.K); } assert(found); }
      bool differs = false; for (size_t i = T; i < 300; ++i) if (wb.label[i] != wf.label[i]) differs = true; assert(differs);   // the perturbation is real: it DOES change later outputs
    }
    // ---- (3) alignment: component names carried across refits (first-fit mapping persists, centroids match)
    { size_t ok = 0, tot = 0; for (size_t i = 120; i < 300; ++i) { ++tot; int ti = A.truth[i]; if (wf.label[i] == ti) ++ok; } (void)tot;
      for (size_t i = 1; i < wf.refits.size(); ++i) { assert(wf.refits[i].overlap_agree > 0.5); }
      assert(wf.mean_overlap > 0.85); assert(ok > 90); }
    // ---- (4) comparison: cross block informative and anchors noisy => multibook_v1 shows a real gain
    { auto C = gen(300, 2.2, true, 23); auto m = walk_forward(C.rows, o), s = walk_forward(anchor_only(C.rows), o); auto cmp = compare(s, m, C.outcome);
      std::printf("informative: agree %.2f eta single %.3f multi %.3f delta %.3f p %.3f -> %s\n", cmp.agreement, cmp.eta_single, cmp.eta_multi, cmp.delta, cmp.p_value, cmp.verdict.c_str()); assert(cmp.days == 180 && cmp.eta_multi > cmp.eta_single && cmp.adds_information); }
    // ---- (5) comparison: cross block is pure noise => verdict must be "no demonstrated gain" (we do not ship complexity that adds nothing)
    { auto C = gen(300, 0.6, false, 31); auto m = walk_forward(C.rows, o), s = walk_forward(anchor_only(C.rows), o); auto cmp = compare(s, m, C.outcome);
      std::printf("noise block: agree %.2f eta single %.3f multi %.3f delta %.3f p %.3f -> %s\n", cmp.agreement, cmp.eta_single, cmp.eta_multi, cmp.delta, cmp.p_value, cmp.verdict.c_str()); assert(!cmp.adds_information && cmp.verdict.find("no demonstrated") == 0); }
    // ---- (6) pruning: duplicated / constant columns are dropped per fit, not fed to PCA
    { auto C = gen(200, 0.9, true, 3); for (auto& r : C.rows) { r[18] = r[17] * 1.0; r[19] = 4.2; } Fit f = fit(C.rows, nullptr, o); auto has = [&](size_t j) { return std::find(f.keep.begin(), f.keep.end(), j) != f.keep.end(); };
      assert(has(17) && !has(18) && !has(19) && std::find(f.dropped.begin(), f.dropped.end(), 19) != f.dropped.end()); }
    // ---- (7) separation from single_v1: the model never reads or writes regime_model state; fitting twice is deterministic
    { auto w2 = walk_forward(A.rows, o); for (size_t i = 0; i < 300; ++i) assert(w2.label[i] == wf.label[i]); }
    // ---- (8) model card
    { CardInputs ci; ci.family = "NQ"; ci.ticker_view = "QQQ"; ci.fit_date = "mock"; for (size_t j = 0; j < 17; ++j) { ci.names.push_back(regime::feature_names()[j]); ci.groups.push_back("anchor"); }
      for (int j = 0; j < 7; ++j) { ci.names.push_back("cross" + std::to_string(j)); ci.groups.push_back("cross_book"); } ci.pruned_unavailable = {"implied correlation / dispersion proxy (no component IV)"};
      auto C = gen(300, 2.2, true, 23); auto m = walk_forward(C.rows, o), s = walk_forward(anchor_only(C.rows), o); auto cmp = compare(s, m, C.outcome); auto j = mj::parse(card_json(ci, m, &cmp)); assert(j.at("variant").str() == "multibook_v1" && j.at("n_features").num(0) == 24 && j.at("features").a.size() == 24 && j.at("vs_single_v1").at("days").num(0) == 180 && j.at("known_limits").a.size() == 4); }
    std::printf("test_multibook_model OK (%.1fs)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); return 0;
}
