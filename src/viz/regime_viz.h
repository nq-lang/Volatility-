#pragma once
// regime_viz.h -- builds the display-only payloads for the PCA 2D / t-SNE views.
// READ-ONLY with respect to the regime model: every accessor below takes a const reference and
// nothing here writes back into RegimeModel, GmmModel, PcaModel or the service.
#include <sstream>
#include <string>
#include <vector>
#include "embedding.h"
#include "../core/regime_model.h"
#include "../bridge/json_util.h"

namespace vt::viz {


struct VizConfig {
    TsneConfig tsne;                 // perplexity etc: config constants chosen from the offline sweep
    size_t metric_k = 10;            // k for trustworthiness / continuity
    size_t place_k = 10;             // k for live kNN placement
    bool enabled = true;
};

// Everything the live placement needs, cached per ticker and versioned by batch id.
struct VizState {
    std::string ticker, batch_id;
    Mat raw_rows;                    // history rows as fitted (identity key for alignment next batch)
    Mat Xstd;                        // standardized 17D matrix (exactly what PCA saw)
    Mat pca3;                        // PCA scores (read from the model)
    Mat posterior;                   // n x 5 (RegimeName order), read from the model
    std::vector<int> dominant;
    Mat Y2, Y3;                      // aligned embeddings
    double kl2 = 0, kl3 = 0, trust2 = 0, cont2 = 0, purity2 = 0, trust3 = 0, cont3 = 0, purity3 = 0;
    double perplexity_used = 0;
    std::vector<FeatureGradient> grad2, grad3;
    double knn_median = 0, knn_p95 = 0;
    bool aligned = false;
    int batch_no = 0;
};

inline VizState build_viz_state(const regime::RegimeModel& model, const Mat& history_raw, const std::string& ticker,
                                const VizConfig& cfg, const VizState* prev) {
    VizState s; s.ticker = ticker; s.raw_rows = history_raw;
    const size_t n = history_raw.size();
    s.batch_no = prev ? prev->batch_no + 1 : 1;
    s.batch_id = ticker + "-batch-" + std::to_string(s.batch_no) + "-n" + std::to_string(n);
    std::vector<int> label(n);
    for (size_t i = 0; i < n; ++i) {
        Vec z = pca::standardize(history_raw[i], model.pca_model.standardization);
        s.pca3.push_back(model.pca_model.project(z));
        s.Xstd.push_back(std::move(z));
        auto live = regime::evaluate_live(model, history_raw[i]);
        s.posterior.emplace_back(live.regime_probability.begin(), live.regime_probability.end());
        label[i] = static_cast<int>(regime::argmax_regime(live.regime_probability));
    }
    s.dominant = label;
    TsneConfig c2 = cfg.tsne; c2.dims = 2;
    TsneConfig c3 = cfg.tsne; c3.dims = 3;
    // identical raw rows in the previous batch identify "the same trading day" (history rows carry no dates)
    std::vector<std::pair<size_t, size_t>> pairs;   // {row in prev, row in new}
    if (prev && !prev->Y2.empty())
        for (size_t b = 0; b < n; ++b) for (size_t a = 0; a < prev->raw_rows.size(); ++a)
            if (prev->raw_rows[a] == history_raw[b]) { pairs.emplace_back(a, b); break; }
    const bool warm = pairs.size() >= 5;
    Mat init2 = s.pca3, init3 = s.pca3;
    if (warm) {
        // Temporal stability: start from the previous batch's layout (matched days keep their place, new days
        // are placed among their 17D neighbours) instead of a fresh PCA init, and skip early exaggeration. The
        // layout can then only refine, not flip or re-arrange its clusters. Still deterministic.
        Mat Xc, Y2c, Y3c; std::vector<char> have(n, 0);
        for (auto& [a, b] : pairs) { have[b] = 1; Xc.push_back(s.Xstd[b]); Y2c.push_back(prev->Y2[a]); Y3c.push_back(prev->Y3[a]); }
        for (size_t b = 0; b < n; ++b) {
            if (have[b]) continue;
            init2[b] = knn_place(Xc, Y2c, s.Xstd[b], cfg.place_k, 0).coords;
            init3[b] = knn_place(Xc, Y3c, s.Xstd[b], cfg.place_k, 0).coords;
        }
        for (auto& [a, b] : pairs) { init2[b] = prev->Y2[a]; init3[b] = prev->Y3[a]; }
        c2.warm_start = c3.warm_start = true;
    }
    auto r2 = tsne_fit(s.Xstd, c2, &init2);
    auto r3 = tsne_fit(s.Xstd, c3, &init3);
    s.perplexity_used = r2.perplexity_used; s.kl2 = r2.kl; s.kl3 = r3.kl; s.Y2 = r2.Y; s.Y3 = r3.Y;
    if (warm) { s.Y2 = procrustes_align(prev->Y2, s.Y2, pairs); s.Y3 = procrustes_align(prev->Y3, s.Y3, pairs); s.aligned = true; }
    Mat Dx = sq_dists(s.Xstd), D2 = sq_dists(s.Y2), D3 = sq_dists(s.Y3);
    s.trust2 = trustworthiness(Dx, D2, cfg.metric_k); s.cont2 = continuity(Dx, D2, cfg.metric_k); s.purity2 = knn_regime_purity(D2, label, cfg.metric_k);
    s.trust3 = trustworthiness(Dx, D3, cfg.metric_k); s.cont3 = continuity(Dx, D3, cfg.metric_k); s.purity3 = knn_regime_purity(D3, label, cfg.metric_k);
    s.grad2 = feature_gradients(s.Xstd, s.Y2); s.grad3 = feature_gradients(s.Xstd, s.Y3);
    Vec loo; for (size_t i = 0; i < n; ++i) loo.push_back(mean_knn_dist_to(s.Xstd, s.Xstd[i], cfg.place_k, static_cast<long>(i)));
    if (!loo.empty()) { std::sort(loo.begin(), loo.end()); s.knn_median = loo[loo.size() / 2]; s.knn_p95 = loo[std::min(loo.size() - 1, static_cast<size_t>(0.95 * loo.size()))]; }
    return s;
}

namespace detail {
inline void vec(std::ostringstream& o, const Vec& v) { o << "["; for (size_t i = 0; i < v.size(); ++i) { if (i) o << ","; o << vt::json::num(v[i]); } o << "]"; }
}

inline std::string viz_static_json(const regime::RegimeModel& model, const VizState& s) {
    std::ostringstream o; using detail::vec;
    o << "{\"ticker\":\"" << vt::json::esc(s.ticker) << "\",\"batch_id\":\"" << vt::json::esc(s.batch_id) << "\",\"feature_names\":[";
    for (size_t f = 0; f < regime::kNumFeatures; ++f) o << (f ? "," : "") << "\"" << vt::json::esc(regime::feature_names()[f]) << "\"";
    o << "],\"feature_short\":[";
    for (size_t f = 0; f < regime::kNumFeatures; ++f) o << (f ? "," : "") << "\"" << vt::json::esc(regime::feature_short_name(f)) << "\"";
    o << "],\"regime_names\":[";
    for (size_t r = 0; r < 5; ++r) o << (r ? "," : "") << "\"" << vt::json::esc(regime::to_string(static_cast<regime::RegimeName>(r))) << "\"";
    o << "],\"regime_colors\":[";
    for (size_t r = 0; r < 5; ++r) o << (r ? "," : "") << "\"" << regime::regime_color_hex(static_cast<regime::RegimeName>(r)) << "\"";
    o << "],\"points\":[";
    for (size_t i = 0; i < s.Xstd.size(); ++i) {
        if (i) o << ",";
        o << "{\"i\":" << i << ",\"date\":\"\",\"pca\":"; vec(o, s.pca3[i]);
        o << ",\"tsne2\":"; vec(o, s.Y2[i]); o << ",\"tsne3\":"; vec(o, s.Y3[i]);
        o << ",\"posterior\":"; vec(o, s.posterior[i]);
        o << ",\"dominant\":" << s.dominant[i] << ",\"z\":"; vec(o, s.Xstd[i]); o << "}";
    }
    o << "],\"pca_meta\":{\"loadings\":[";
    for (size_t f = 0; f < model.pca_model.loadings.size(); ++f) { if (f) o << ","; vec(o, model.pca_model.loadings[f]); }
    o << "],\"explained_variance_ratio\":"; vec(o, model.pca_model.explained_variance_fraction());
    o << ",\"cumulative\":" << vt::json::num(model.pca_model.cumulative_explained_variance()) << ",\"gmm_proj\":[";
    // the GMM is fit IN the PCA space, so mu_proj = W^T mu and Sigma_proj = W^T Sigma W are the stored parameters themselves
    for (size_t c = 0; c < model.gmm_model.components.size(); ++c) {
        if (c) o << ",";
        auto rn = model.label_mapping.cluster_to_regime[c];
        o << "{\"regime\":" << static_cast<int>(rn) << ",\"mu\":"; vec(o, model.gmm_model.components[c].mean); o << ",\"cov\":[";
        for (size_t r = 0; r < model.gmm_model.components[c].covariance.size(); ++r) { if (r) o << ","; vec(o, model.gmm_model.components[c].covariance[r]); }
        o << "]}";
    }
    o << "]},\"tsne_meta\":{\"perplexity\":" << vt::json::num(s.perplexity_used) << ",\"seed\":42"
      << ",\"kl_divergence\":" << vt::json::num(s.kl2) << ",\"kl_divergence_3d\":" << vt::json::num(s.kl3)
      << ",\"trustworthiness\":" << vt::json::num(s.trust2) << ",\"continuity\":" << vt::json::num(s.cont2)
      << ",\"knn_regime_purity\":" << vt::json::num(s.purity2)
      << ",\"trustworthiness_3d\":" << vt::json::num(s.trust3) << ",\"continuity_3d\":" << vt::json::num(s.cont3)
      << ",\"knn_regime_purity_3d\":" << vt::json::num(s.purity3)
      << ",\"knn_ref\":{\"median\":" << vt::json::num(s.knn_median) << ",\"p95\":" << vt::json::num(s.knn_p95) << "}"
      << ",\"alignment_to_prev_batch\":\"" << (s.aligned ? "procrustes" : "none") << "\",\"feature_gradients\":[";
    for (size_t f = 0; f < s.grad2.size(); ++f) {
        if (f) o << ",";
        o << "{\"feature\":\"" << vt::json::esc(regime::feature_names()[f]) << "\",\"dir2\":"; vec(o, s.grad2[f].dir);
        o << ",\"dir3\":"; vec(o, s.grad3[f].dir);
        o << ",\"r2\":" << vt::json::num(s.grad2[f].r2) << ",\"r2_3d\":" << vt::json::num(s.grad3[f].r2) << "}";
    }
    o << "]},\"live\":{\"trail\":[]}}";
    return o.str();
}

// per-cycle: placement of today's standardized features on both cached embeddings
inline std::string viz_live_json(const regime::RegimeModel& model, const VizState& s, const Vec& raw_features, const VizConfig& cfg) {
    Vec z = pca::standardize(raw_features, model.pca_model.standardization);
    auto live = regime::evaluate_live(model, raw_features);
    auto p2 = knn_place(s.Xstd, s.Y2, z, cfg.place_k, s.knn_p95);
    auto p3 = knn_place(s.Xstd, s.Y3, z, cfg.place_k, s.knn_p95);
    std::ostringstream o; using detail::vec;
    o << "{\"batch_id\":\"" << vt::json::esc(s.batch_id) << "\",\"tsne2\":"; vec(o, p2.coords);
    o << ",\"tsne3\":"; vec(o, p3.coords);
    o << ",\"posterior\":"; vec(o, Vec(live.regime_probability.begin(), live.regime_probability.end()));
    o << ",\"z\":"; vec(o, z);
    o << ",\"placement_confidence\":" << vt::json::num(s.knn_median > 0 ? p2.mean_knn_dist / s.knn_median : 0.0)
      << ",\"out_of_distribution\":" << (p2.out_of_distribution ? "true" : "false") << "}";
    return o.str();
}

} // namespace vt::viz
