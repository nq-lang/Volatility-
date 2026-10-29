#pragma once
// regime_bridge.h — serializes the PCA regime model's outputs to JSON per the
// §7 data contract. Two payload shapes:
//  - "regime_static" (published once per batch refit): cluster point cloud +
//    covariance ellipsoid params + loading vectors + explained variance --
//    everything the biplot needs that does NOT change intraday.
//  - "regime_live" (published every live-evaluation cycle): current PCA
//    coords, regime probability vector, trail point, decay-only path point,
//    trigger states, and the generated narrative -- everything that DOES
//    change intraday.
#include <sstream>
#include <vector>
#include "../core/regime_model.h"
#include "json_util.h"

namespace vt::bridge {

inline std::string regime_static_json(const regime::RegimeModel& model,
                                       const std::vector<std::vector<double>>& historical_pca_points,
                                       const std::vector<int>& historical_cluster_ids) {
    std::ostringstream o;
    auto ev = model.pca_model.explained_variance_fraction();
    o << "{"
      << "\"explained_variance\":{\"pc1\":" << json::num(ev.size() > 0 ? ev[0] : 0.0)
      << ",\"pc2\":" << json::num(ev.size() > 1 ? ev[1] : 0.0)
      << ",\"pc3\":" << json::num(ev.size() > 2 ? ev[2] : 0.0)
      << ",\"cumulative\":" << json::num(model.pca_model.cumulative_explained_variance()) << "},"
      << "\"n_days_in_fit\":" << model.pca_model.n_days_in_fit << ","
      << "\"regimes\":[";
    for (size_t r = 0; r < 5; ++r) {
        if (r) o << ",";
        auto name = static_cast<regime::RegimeName>(r);
        o << "{\"name\":\"" << json::esc(regime::to_string(name)) << "\",\"color\":\""
          << regime::regime_color_hex(name) << "\"}";
    }
    o << "],"
      // Part A: one fitted ellipsoid per regime (center + 3 semi-axis
      // vectors, each pre-scaled to its radius) -- never a placeholder sphere.
      << "\"ellipsoids\":[";
    for (size_t cluster = 0; cluster < model.label_mapping.cluster_to_regime.size(); ++cluster) {
        if (cluster) o << ",";
        auto rn = model.label_mapping.cluster_to_regime[cluster];
        auto ell = regime::component_ellipsoid(model.gmm_model.components[cluster]);
        o << "{\"regime\":" << static_cast<int>(rn) << ",\"name\":\"" << json::esc(regime::to_string(rn)) << "\""
          << ",\"color\":\"" << regime::regime_color_hex(rn) << "\""
          << ",\"center\":[" << json::num(ell.center.size() > 0 ? ell.center[0] : 0.0) << ","
                              << json::num(ell.center.size() > 1 ? ell.center[1] : 0.0) << ","
                              << json::num(ell.center.size() > 2 ? ell.center[2] : 0.0) << "]"
          << ",\"axes\":[";
        for (int a = 0; a < 3; ++a) {
            if (a) o << ",";
            const auto& v = ell.axes[a];
            o << "[" << json::num(v.size() > 0 ? v[0] : 0.0) << "," << json::num(v.size() > 1 ? v[1] : 0.0)
              << "," << json::num(v.size() > 2 ? v[2] : 0.0) << "," << json::num(ell.radii[a]) << "]";
        }
        o << "]}";
    }
    o << "],"
      << "\"points\":[";
    for (size_t i = 0; i < historical_pca_points.size(); ++i) {
        if (i) o << ",";
        const auto& p = historical_pca_points[i];
        int cluster = i < historical_cluster_ids.size() ? historical_cluster_ids[i] : -1;
        regime::RegimeName rn = (cluster >= 0 && cluster < 5) ? model.label_mapping.cluster_to_regime[cluster] : regime::RegimeName::Count;
        o << "{\"pc1\":" << json::num(p.size() > 0 ? p[0] : 0.0)
          << ",\"pc2\":" << json::num(p.size() > 1 ? p[1] : 0.0)
          << ",\"pc3\":" << json::num(p.size() > 2 ? p[2] : 0.0)
          << ",\"regime\":" << static_cast<int>(rn) << "}";
    }
    o << "],"
      // Part B: loading vectors, under the fixed convention "points as raw
      // scores, arrows scaled by ONE shared constant" -- arrow_scale is that
      // constant, applied uniformly by the renderer, never per-arrow.
      << "\"arrow_scale\":" << json::num(regime::kLoadingArrowScale) << ","
      << "\"loadings\":[";
    for (size_t f = 0; f < regime::kNumFeatures; ++f) {
        if (f) o << ",";
        double c1 = model.pca_model.loadings.size() > f && model.pca_model.loadings[f].size() > 0 ? model.pca_model.loadings[f][0] : 0.0;
        double c2 = model.pca_model.loadings.size() > f && model.pca_model.loadings[f].size() > 1 ? model.pca_model.loadings[f][1] : 0.0;
        double c3 = model.pca_model.loadings.size() > f && model.pca_model.loadings[f].size() > 2 ? model.pca_model.loadings[f][2] : 0.0;
        o << "{\"name\":\"" << json::esc(regime::feature_names()[f]) << "\""
          << ",\"short\":\"" << json::esc(regime::feature_short_name(f)) << "\""
          << ",\"pc1\":" << json::num(c1) << ",\"pc2\":" << json::num(c2) << ",\"pc3\":" << json::num(c3) << "}";
    }
    o << "]}";
    return o.str();
}

inline std::string regime_live_json(const regime::LiveRead& live,
                                     const regime::TriggerState& triggers,
                                     const std::string& narrative,
                                     double flip_distance_pct,
                                     double speed_value,
                                     bool mr_override_active,
                                     double decay_deviation,
                                     const std::vector<std::vector<double>>& intraday_trail,
                                     bool directional_bias_bullish,
                                     const std::vector<regime::FeatureContribution>& contributions = {}) {
    std::ostringstream o;
    auto predicted = regime::argmax_regime(live.regime_probability);
    o << "{"
      << "\"predicted_regime\":\"" << json::esc(regime::to_string(predicted)) << "\","
      << "\"predicted_color\":\"" << regime::regime_color_hex(predicted) << "\","
      << "\"probabilities\":[";
    for (size_t r = 0; r < 5; ++r) {
        if (r) o << ",";
        o << "{\"name\":\"" << json::esc(regime::to_string(static_cast<regime::RegimeName>(r))) << "\""
          << ",\"color\":\"" << regime::regime_color_hex(static_cast<regime::RegimeName>(r)) << "\""
          << ",\"p\":" << json::num(live.regime_probability[r]) << "}";
    }
    o << "],"
      << "\"pca_coords\":{\"pc1\":" << json::num(live.pca_coords.size() > 0 ? live.pca_coords[0] : 0.0)
      << ",\"pc2\":" << json::num(live.pca_coords.size() > 1 ? live.pca_coords[1] : 0.0)
      << ",\"pc3\":" << json::num(live.pca_coords.size() > 2 ? live.pca_coords[2] : 0.0) << "},"
      << "\"trail\":[";
    for (size_t i = 0; i < intraday_trail.size(); ++i) {
        if (i) o << ",";
        const auto& p = intraday_trail[i];
        o << "[" << json::num(p.size() > 0 ? p[0] : 0.0) << "," << json::num(p.size() > 1 ? p[1] : 0.0)
          << "," << json::num(p.size() > 2 ? p[2] : 0.0) << "]";
    }
    o << "],"
      << "\"directional_bias_bullish\":" << (directional_bias_bullish ? "true" : "false") << ","
      << "\"triggers\":{"
      << "\"soft_active\":" << (triggers.soft_active ? "true" : "false") << ","
      << "\"hard_active\":" << (triggers.hard_active ? "true" : "false") << ","
      << "\"flip_distance_pct\":" << json::num(flip_distance_pct) << ","
      << "\"speed_value\":" << json::num(speed_value)
      << "},"
      << "\"decay_path_deviation\":" << json::num(decay_deviation) << ","
      << "\"mean_reversion_override_active\":" << (mr_override_active ? "true" : "false") << ","
      // Part B overlay: exact per-feature pull vectors for THIS observation,
      // under the same shared arrow_scale as the static loadings -- lets the
      // UI show which flows are moving the day, not just the resultant dot.
      << "\"feature_contributions\":[";
    for (size_t i = 0; i < contributions.size(); ++i) {
        if (i) o << ",";
        const auto& c = contributions[i];
        o << "{\"name\":\"" << json::esc(c.name) << "\",\"pull\":["
          << json::num(c.pull.size() > 0 ? c.pull[0] : 0.0) << ","
          << json::num(c.pull.size() > 1 ? c.pull[1] : 0.0) << ","
          << json::num(c.pull.size() > 2 ? c.pull[2] : 0.0) << "]}";
    }
    o << "],"
      << "\"narrative\":\"" << json::esc(narrative) << "\""
      << "}";
    return o.str();
}

} // namespace vt::bridge
