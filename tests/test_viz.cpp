// Display-layer tests: t-SNE determinism, Procrustes alignment, placement, feature gradients,
// fidelity metrics, and -- the one that matters -- the model-integrity guarantee that building
// the viz layer changes NOTHING the classifier produces.
#include <cassert>
#include <iostream>
#include <random>
#include "../src/viz/regime_viz.h"
using namespace vt; using namespace vt::viz; using namespace vt::regime;

static Mat make_history(unsigned seed, size_t n) {
    std::mt19937 rng(seed); std::normal_distribution<double> g(0, 0.25);
    Mat rows;
    for (size_t i = 0; i < n; ++i) {
        Vec f(kNumFeatures); for (auto& v : f) v = g(rng);
        int a = i % 5;
        auto blk = [&](std::initializer_list<size_t> c, double x) { for (size_t k : c) f[k] += x; };
        if (a == 0) { blk({NetGEX_z, FlipDistancePct, DEX_z, Vanna_z}, 2.0); blk({ImpliedZ_ATM, ImpliedZ_25dPut, VRP_z, RvIvZscoreTod}, -1.0); blk({Speed_z, CharmPerHour_z, Color_z}, -1.0); }
        if (a == 1) blk({ImpliedZ_ATM, ImpliedZ_25dPut, VRP_z, RvIvZscoreTod}, 2.4);
        if (a == 2) { blk({NetGEX_z, FlipDistancePct, DEX_z, Vanna_z}, -2.0); blk({Speed_z, CharmPerHour_z, Color_z}, 1.2); }
        if (a == 3) { blk({NetGEX_z, FlipDistancePct, DEX_z, Vanna_z}, 0.4); blk({Speed_z, CharmPerHour_z, Color_z}, -2.4); }
        if (a == 4) blk({Speed_z, CharmPerHour_z, Color_z}, 2.4);
        rows.push_back(f);
    }
    return rows;
}
static std::string fingerprint(const RegimeModel& m, const Mat& rows) {   // everything the classifier outputs, bit-exact
    std::ostringstream o; o.precision(17);
    for (auto& r : m.pca_model.loadings) for (double v : r) o << v << ",";
    for (double v : m.pca_model.eigenvalues) o << v << ",";
    for (auto& c : m.gmm_model.components) { o << c.weight << ","; for (double v : c.mean) o << v << ","; for (auto& rr : c.covariance) for (double v : rr) o << v << ","; }
    for (auto c : m.label_mapping.cluster_to_regime) o << static_cast<int>(c);
    for (auto& r : rows) { auto l = evaluate_live(m, r); for (double v : l.pca_coords) o << v << ","; for (double p : l.regime_probability) o << p << ","; }
    return o.str();
}

int main() {
    Mat hist = make_history(3, 150);
    VizConfig cfg; cfg.tsne.perplexity = 20; cfg.tsne.iterations = 500;

    // --- model integrity: classifier outputs are bit-identical before and after the viz layer runs ---
    RegimeModel model = fit_batch(hist);
    std::string before = fingerprint(model, hist);
    VizState s1 = build_viz_state(model, hist, "SPX", cfg, nullptr);
    (void)viz_static_json(model, s1); (void)viz_live_json(model, s1, hist[0], cfg);
    std::string after = fingerprint(model, hist);
    assert(before == after);
    std::cout << "[ok] model integrity: loadings, GMM parameters, label map, PCA scores, probabilities bit-identical after viz build\n";

    // --- determinism: same input + seed -> same embedding ---
    VizState s1b = build_viz_state(model, hist, "SPX", cfg, nullptr);
    assert(s1.Y2 == s1b.Y2 && s1.Y3 == s1b.Y3);
    std::cout << "[ok] t-SNE deterministic for a fixed seed (2D and 3D)\n";

    // --- separation + fidelity metrics sane on clusters that really exist ---
    std::cout << "     trust=" << s1.trust2 << " cont=" << s1.cont2 << " purity=" << s1.purity2 << " KL=" << s1.kl2 << " perplexity=" << s1.perplexity_used << "\n";
    assert(s1.trust2 > 0.85 && s1.cont2 > 0.85 && s1.purity2 > 0.85);

    // --- alignment: refit on one extra day + a mirrored/rotated start must not flip the picture ---
    Mat hist2 = hist; hist2.push_back(make_history(99, 1)[0]);
    RegimeModel model2 = fit_batch(hist2);
    VizState s2 = build_viz_state(model2, hist2, "SPX", cfg, &s1);
    assert(s2.aligned);
    double worst = 0, span = 0;
    for (size_t i = 0; i < hist.size(); ++i) { double d = std::hypot(s1.Y2[i][0] - s2.Y2[i][0], s1.Y2[i][1] - s2.Y2[i][1]); worst = std::max(worst, d);
        span = std::max(span, std::hypot(s1.Y2[i][0], s1.Y2[i][1])); }
    // median displacement, relative to map radius
    std::vector<double> disp; for (size_t i = 0; i < hist.size(); ++i) disp.push_back(std::hypot(s1.Y2[i][0] - s2.Y2[i][0], s1.Y2[i][1] - s2.Y2[i][1]));
    std::sort(disp.begin(), disp.end());
    std::cout << "     aligned refit: median displacement / map radius = " << disp[disp.size() / 2] / span << "\n";
    assert(disp[disp.size() / 2] / span < 0.08); // unchanged days barely move across a refit
    // direct Procrustes check: a mirrored + rotated copy of an embedding aligns back almost exactly
    Mat M = s1.Y2; for (auto& r : M) { double x = r[0], y = r[1]; r[0] = -(0.8 * x - 0.6 * y) * 2.0 + 5; r[1] = (0.6 * x + 0.8 * y) * 2.0 - 3; }
    std::vector<std::pair<size_t, size_t>> id; for (size_t i = 0; i < M.size(); ++i) id.push_back({i, i});
    Mat back = procrustes_align(s1.Y2, M, id); double err = 0; for (size_t i = 0; i < M.size(); ++i) err = std::max(err, std::hypot(back[i][0] - s1.Y2[i][0], back[i][1] - s1.Y2[i][1]));
    assert(err < 1e-6);
    std::cout << "[ok] Procrustes undoes a mirror + rotation + scale + shift (max err " << err << ")\n";

    // --- live placement: deterministic, does not alter the cache, and flags an alien point ---
    VizState snapshot = s1;
    std::string a = viz_live_json(model, s1, hist[7], cfg), b = viz_live_json(model, s1, hist[7], cfg);
    assert(a == b && snapshot.Y2 == s1.Y2 && snapshot.Y3 == s1.Y3);
    Vec alien(kNumFeatures, 40.0);
    assert(viz_live_json(model, s1, alien, cfg).find("\"out_of_distribution\":true") != std::string::npos);
    assert(a.find("\"out_of_distribution\":false") != std::string::npos);
    std::cout << "[ok] live placement deterministic, cache untouched, out-of-distribution point flagged\n";

    // --- feature gradients: a feature that defines a cluster axis must be explained by the map ---
    double best = 0; for (auto& g : s1.grad2) best = std::max(best, g.r2);
    assert(best > 0.5);
    std::cout << "[ok] feature gradient R2 max = " << best << "\n";

    // --- offline perplexity sweep helper: reports trustworthiness per candidate (config constant is picked from this) ---
    Mat Xs = s1.Xstd, Dx = sq_dists(Xs);
    for (double p : {10.0, 20.0, 30.0, 40.0}) { TsneConfig c = cfg.tsne; c.perplexity = p; c.iterations = 400; auto r = tsne_fit(Xs, c, &s1.pca3);
        std::cout << "     sweep perplexity " << p << " -> trustworthiness " << trustworthiness(Dx, sq_dists(r.Y), 10) << "\n"; }
    std::cout << "ALL VIZ TESTS PASSED\n";
}
