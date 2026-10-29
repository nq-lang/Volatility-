#pragma once
// regime_model.h — the PCA regime classification model per the supplied build
// spec: Layer A (pca.h) for visualization, Layer B (gmm.h) for the five-regime
// probabilistic decision, plus everything §5-§7 require on top: pre-market
// baseline, intraday dual-trigger transition detection, a decay-only
// projected path, a Mean-Reversion/Charm-Vanna override flag, and the full
// UI-facing data contract with a generated structural narrative.
#include <array>
#include <string>
#include <sstream>
#include <optional>
#include <chrono>
#include "vt/pca.h"
#include "vt/gmm.h"
#include "zscore.h"

namespace vt::regime {

using linalg::Vec;
using linalg::Mat;

// ---- §3: the standardized feature set. 17 inputs (the table's 14 rows, two
// of which each expand into a level + momentum / skew + kurtosis pair) --
// slightly over the "~14-16" guideline; acceptable per the spec's own
// tolerance, but check explained variance (§8) before adding more. ----
enum FeatureIndex : size_t {
    NetGEX_z = 0, FlipDistancePct, Speed_z, CharmPerHour_z, Color_z, Vanna_z, DEX_z,
    CallOiMomentum, PutOiMomentum, ImpliedZ_ATM, ImpliedZ_25dPut, VRP_z, RvIvZscoreTod,
    RiskReversal25d_z, RiskReversal25d_Momentum, RndSkewness_z, RndKurtosis_z,
    kNumFeatures
};

inline const std::array<const char*, kNumFeatures>& feature_names() {
    static const std::array<const char*, kNumFeatures> names = {
        "Net GEX (z)", "Gamma Flip Distance %", "Speed (z)", "Charm/session-hour (z)",
        "Color (z)", "Vanna VEX (z)", "Net Dealer Hedge Delta / DEX (z)",
        "Call OI Ratio Momentum", "Put OI Ratio Momentum", "Implied Z-Score (ATM)",
        "Implied Z-Score (25d put)", "VRP (z)", "RV-vs-IV Z-Score (TOD)",
        "25d Risk Reversal (z)", "25d Risk Reversal Momentum", "RND Skewness (z)", "RND Kurtosis (z)"
    };
    return names;
}

// Short display labels for the biplot's vector arrows -- matched to the
// supplied reference (GEX, DEX, Speed, Vanna(VEX), Charm(CEX), VRP_z,
// IV Skew, Flip Dist) so the visualization reads the same way.
inline const char* feature_short_name(size_t i) {
    static const std::array<const char*, kNumFeatures> shorts = {
        "GEX", "Flip Dist", "Speed", "Charm(CEX)", "Color", "Vanna(VEX)", "DEX",
        "Call OI Mom", "Put OI Mom", "IV Z (ATM)", "IV Z (25dP)", "VRP_z", "RV/IV Z",
        "IV Skew", "Skew Mom", "RND Skew", "RND Kurt"
    };
    return i < shorts.size() ? shorts[i] : "?";
}

// Shared scaling constant for loading-vector arrows in the biplot (Part B):
// under this project's fixed convention, points are plotted as raw PCA
// scores and EVERY arrow is multiplied by this ONE constant -- never a
// per-arrow scale, and never mixed with a "correlation biplot" convention
// (unit-length/correlation-scaled arrows) in the same view. Tune this single
// number if arrows are visually too short/long relative to the point cloud;
// never rescale individual arrows to "fix" one that looks short.
inline constexpr double kLoadingArrowScale = 6.0;

enum class RegimeName { PinningLowVol, HighVolatilityRisk, TrendingDirectional, MeanReversion, TransitionalBreakout, Count };

inline const char* to_string(RegimeName r) {
    switch (r) {
        case RegimeName::PinningLowVol:        return "Pinning / Low-Volatility";
        case RegimeName::HighVolatilityRisk:   return "High-Volatility Risk";
        case RegimeName::TrendingDirectional:  return "Trending / Directional-Bias";
        case RegimeName::MeanReversion:        return "Mean-Reversion";
        case RegimeName::TransitionalBreakout: return "Transitional / Breakout";
        default:                               return "Unknown";
    }
}
// Regime colors are deliberately distinct from the terminal's bullish/bearish
// red/green convention (§8) — regime identity is not itself a directional
// call. Hex values for the web bridge/front end.
inline const char* regime_color_hex(RegimeName r) {
    switch (r) {
        case RegimeName::PinningLowVol:        return "#9aa0aa"; // grey
        case RegimeName::HighVolatilityRisk:   return "#e0a339"; // amber
        case RegimeName::TrendingDirectional:  return "#2dd4bf"; // teal
        case RegimeName::MeanReversion:        return "#9a8bf0"; // purple
        case RegimeName::TransitionalBreakout: return "#e0587c"; // pink
        default:                               return "#ffffff";
    }
}

// ---- Cluster-to-regime label mapping (§4, §10.4, §11.1/§11.4): re-derived on
// EVERY refit from each cluster's responsibility-weighted feature profile,
// never hard-coded to a fixed cluster index. ----
struct LabelMapping {
    std::array<RegimeName, 5> cluster_to_regime{}; // cluster index -> regime
    std::array<double, 5> confidence{};            // how cleanly each cluster matched its archetype
};

inline double archetype_score(RegimeName r, const Vec& profile) {
    double gex = profile[NetGEX_z], flip = profile[FlipDistancePct], speed = profile[Speed_z];
    double ivz = profile[ImpliedZ_ATM], rviv = profile[RvIvZscoreTod];
    switch (r) {
        case RegimeName::PinningLowVol:        return gex + flip - speed - rviv;
        case RegimeName::HighVolatilityRisk:   return ivz + rviv;
        case RegimeName::TrendingDirectional:  return -gex - flip + speed;
        case RegimeName::MeanReversion:        return -std::abs(gex - 0.5) - 0.3 * speed;
        case RegimeName::TransitionalBreakout: return -std::abs(flip) + speed;
        default: return 0.0;
    }
}

// Responsibility-weighted average of the STANDARDIZED raw feature vectors for
// one cluster -- this is the "centroid feature profile" the spec requires
// labeling be based on (not the PCA-space centroid, which isn't directly
// interpretable in original feature units).
inline Vec cluster_feature_profile(const Mat& standardized_rows, const Mat& responsibilities, size_t cluster) {
    size_t n = standardized_rows.size();
    size_t d = n > 0 ? standardized_rows[0].size() : 0;
    Vec profile(d, 0.0);
    double total_weight = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double w = responsibilities[i][cluster];
        total_weight += w;
        for (size_t j = 0; j < d; ++j) profile[j] += w * standardized_rows[i][j];
    }
    if (total_weight > 1e-9) for (double& v : profile) v /= total_weight;
    return profile;
}

// Greedy bijective assignment: repeatedly pick the best-scoring (cluster,
// regime) pair not yet used, so every regime is assigned to exactly one
// cluster even though archetype_score() alone might favor the same regime for
// two different clusters.
inline LabelMapping derive_label_mapping(const std::vector<Vec>& cluster_profiles) {
    LabelMapping mapping;
    size_t k = cluster_profiles.size();
    std::vector<std::vector<double>> scores(k, std::vector<double>(5));
    for (size_t c = 0; c < k; ++c)
        for (size_t r = 0; r < 5; ++r)
            scores[c][r] = archetype_score(static_cast<RegimeName>(r), cluster_profiles[c]);

    std::vector<bool> cluster_used(k, false), regime_used(5, false);
    for (size_t step = 0; step < std::min<size_t>(k, 5); ++step) {
        double best = -1e300; size_t best_c = 0, best_r = 0;
        for (size_t c = 0; c < k; ++c) {
            if (cluster_used[c]) continue;
            for (size_t r = 0; r < 5; ++r) {
                if (regime_used[r]) continue;
                if (scores[c][r] > best) { best = scores[c][r]; best_c = c; best_r = r; }
            }
        }
        cluster_used[best_c] = true; regime_used[best_r] = true;
        mapping.cluster_to_regime[best_c] = static_cast<RegimeName>(best_r);
        mapping.confidence[best_c] = best;
    }
    return mapping;
}

// ---- The full fitted model (batch layer output) ----
struct RegimeModel {
    pca::PcaModel pca_model;
    gmm::GmmModel gmm_model; // fit on the 3D PCA-projected space, per the spec's default recommendation
    LabelMapping label_mapping;
    std::chrono::system_clock::time_point fit_time;
};

inline RegimeModel fit_batch(const Mat& raw_feature_rows, unsigned seed = 42) {
    RegimeModel model;
    model.pca_model = pca::fit(raw_feature_rows, std::vector<std::string>(feature_names().begin(), feature_names().end()));
    model.fit_time = std::chrono::system_clock::now();

    Mat pca_points;
    pca_points.reserve(raw_feature_rows.size());
    Mat standardized_rows;
    standardized_rows.reserve(raw_feature_rows.size());
    for (const auto& row : raw_feature_rows) {
        Vec std_row = pca::standardize(row, model.pca_model.standardization);
        standardized_rows.push_back(std_row);
        pca_points.push_back(model.pca_model.project(std_row));
    }

    model.gmm_model = gmm::fit(pca_points, 5, 200, 1e-6, seed);

    Mat resp(raw_feature_rows.size(), Vec(5, 0.0));
    for (size_t i = 0; i < pca_points.size(); ++i) resp[i] = gmm::responsibilities(pca_points[i], model.gmm_model);

    std::vector<Vec> profiles(5);
    for (size_t c = 0; c < 5; ++c) profiles[c] = cluster_feature_profile(standardized_rows, resp, c);
    model.label_mapping = derive_label_mapping(profiles);

    return model;
}

// ---- Live evaluation (§9's cheap live layer): projection + GMM eval only,
// remapped into regime-name-keyed probabilities via the current label mapping. ----
struct LiveRead {
    Vec pca_coords;                              // PC1/PC2/PC3 for this observation
    std::array<double, 5> regime_probability{};  // indexed by RegimeName
};

inline LiveRead evaluate_live(const RegimeModel& model, const Vec& raw_features) {
    LiveRead read;
    Vec std_features = pca::standardize(raw_features, model.pca_model.standardization);
    read.pca_coords = model.pca_model.project(std_features);
    Vec resp = gmm::responsibilities(read.pca_coords, model.gmm_model);
    for (size_t c = 0; c < resp.size() && c < 5; ++c) {
        RegimeName r = model.label_mapping.cluster_to_regime[c];
        read.regime_probability[static_cast<size_t>(r)] += resp[c];
    }
    return read;
}

inline RegimeName argmax_regime(const std::array<double, 5>& probs) {
    size_t best = 0;
    for (size_t i = 1; i < 5; ++i) if (probs[i] > probs[best]) best = i;
    return static_cast<RegimeName>(best);
}

// ---- §6: dual-trigger intraday transition detection ----
struct TriggerState {
    bool soft_active = false;   // Speed above its trailing percentile threshold
    bool hard_active = false;   // D_flip% sign just changed vs the previous evaluation
    std::optional<std::chrono::system_clock::time_point> soft_triggered_at;
    std::optional<std::chrono::system_clock::time_point> hard_triggered_at;
};

class TriggerDetector {
public:
    // percentile_threshold: e.g. 0.80 for "80th percentile" (§6, calibrate during validation).
    TriggerDetector(double percentile_threshold = 0.80) : threshold_(percentile_threshold) {}

    TriggerState update(double speed_value, const std::vector<double>& speed_trailing_history,
                         double flip_distance_pct) {
        TriggerState st = state_;
        // Soft trigger: Speed's percentile rank within its own trailing distribution.
        if (!speed_trailing_history.empty()) {
            size_t below = 0;
            for (double v : speed_trailing_history) if (v <= speed_value) ++below;
            double pct_rank = static_cast<double>(below) / static_cast<double>(speed_trailing_history.size());
            bool now_soft = pct_rank >= threshold_;
            if (now_soft && !st.soft_active) st.soft_triggered_at = std::chrono::system_clock::now();
            st.soft_active = now_soft;
        }
        // Hard trigger: D_flip% sign change vs the previous reading.
        if (have_prev_flip_) {
            bool sign_changed = (prev_flip_ > 0.0 && flip_distance_pct <= 0.0) ||
                                 (prev_flip_ < 0.0 && flip_distance_pct >= 0.0);
            st.hard_active = sign_changed;
            if (sign_changed) st.hard_triggered_at = std::chrono::system_clock::now();
        }
        prev_flip_ = flip_distance_pct;
        have_prev_flip_ = true;
        state_ = st;
        return st;
    }

private:
    double threshold_;
    TriggerState state_;
    double prev_flip_ = 0.0;
    bool have_prev_flip_ = false;
};

// ---- §6: decay-only projected path -- what the PCA point WOULD do across the
// remaining session if only Charm/Color time-decay occurred, spot & IV held
// fixed. Reuses the same PCA projection; only the Charm/Color feature entries
// are advanced, everything else is held at the pre-market baseline value. ----
inline std::vector<Vec> decay_only_path(const RegimeModel& model, const Vec& baseline_raw_features,
                                         double charm_per_hour, double color_per_hour, int remaining_hours,
                                         int steps_per_hour = 4) {
    std::vector<Vec> path;
    int total_steps = std::max(1, remaining_hours * steps_per_hour);
    path.reserve(total_steps + 1);
    for (int s = 0; s <= total_steps; ++s) {
        double hours_elapsed = static_cast<double>(s) / steps_per_hour;
        Vec projected = baseline_raw_features;
        // Charm/Color compound roughly linearly over short horizons for this
        // projection's purposes; a full second-order (Color-of-Charm) term is
        // deliberately omitted here for simplicity -- documented approximation.
        projected[CharmPerHour_z] = baseline_raw_features[CharmPerHour_z] + charm_per_hour * hours_elapsed;
        projected[Color_z] = baseline_raw_features[Color_z] + color_per_hour * hours_elapsed;
        path.push_back(model.pca_model.standardize_and_project(projected));
    }
    return path;
}

inline double decay_path_deviation(const Vec& live_pca_coords, const Vec& decay_point_at_same_time) {
    double s = 0.0;
    for (size_t i = 0; i < live_pca_coords.size() && i < decay_point_at_same_time.size(); ++i) {
        double d = live_pca_coords[i] - decay_point_at_same_time[i];
        s += d * d;
    }
    return std::sqrt(s);
}

// ---- §6: Mean-Reversion / Charm-Vanna override ----
inline bool mean_reversion_override_active(RegimeName current, double charm_z, double vanna_z, double threshold = 2.0) {
    return current == RegimeName::MeanReversion && (std::abs(charm_z) > threshold || std::abs(vanna_z) > threshold);
}

// ---- §7: narrative string, generated from the current dominant standardized
// feature contributions (not a generic template) ----
inline std::string generate_narrative(RegimeName regime, const Vec& raw_features) {
    std::vector<std::pair<double, size_t>> ranked;
    for (size_t i = 0; i < raw_features.size(); ++i) ranked.push_back({std::abs(raw_features[i]), i});
    std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });

    std::ostringstream o;
    o << to_string(regime) << " read, dominated by ";
    for (int i = 0; i < 3 && i < static_cast<int>(ranked.size()); ++i) {
        if (i > 0) o << (i == 2 ? ", and " : ", ");
        size_t idx = ranked[i].second;
        o << feature_names()[idx] << " (" << (raw_features[idx] >= 0 ? "+" : "") << raw_features[idx] << ")";
    }
    o << ".";
    return o.str();
}

// ---- §Part A: regime "bubbles" as true ellipsoids, not spheres. Each
// regime's GMM component covariance (already a 3x3 matrix in PCA space, since
// the GMM is fit on the 3D projection) is eigendecomposed into 3 orthogonal
// semi-axes. `scale` sets the visual envelope size -- 1.5 is a reasonable
// "most of the mass" default, not a strict statistical confidence level; for
// an actual 3D confidence region at probability p, use
// scale = sqrt(chi-square inverse CDF at p, 3 dof) (e.g. ~2.795 for 90%). ----
struct Ellipsoid {
    Vec center;                 // 3D PCA-space centroid
    std::array<Vec, 3> axes;    // 3 orthonormal directions (from eigenvectors)
    std::array<double, 3> radii; // semi-axis lengths = sqrt(eigenvalue) * scale
};

inline Ellipsoid component_ellipsoid(const gmm::Component& c, double scale = 1.5) {
    Ellipsoid e;
    e.center = c.mean;
    auto full = linalg::jacobi_eigen(c.covariance);
    auto top = linalg::top_k_eigen(full, c.covariance.size());
    for (size_t i = 0; i < 3 && i < top.values.size(); ++i) {
        Vec axis(top.vectors.size());
        for (size_t r = 0; r < top.vectors.size(); ++r) axis[r] = top.vectors[r][i];
        e.axes[i] = axis;
        e.radii[i] = std::sqrt(std::max(0.0, top.values[i])) * scale;
    }
    return e;
}

// ---- §Part B: per-feature contribution vectors for the live observation.
// Under the fixed scaling convention (raw PCA scores for points, one shared
// constant for loading arrows -- never mixed), a live point's coordinates
// decompose EXACTLY as the sum of these per-feature pulls:
//   pca_coords[c] = sum_i standardized_features[i] * loadings[i][c]
// This is what lets the UI show "which flows are moving the day" as an exact
// breakdown, not an approximation. ----
struct FeatureContribution {
    std::string name;
    Vec pull; // 3D vector: this feature's exact contribution to the live PCA coords
};

inline std::vector<FeatureContribution> feature_contributions(const RegimeModel& model, const Vec& raw_features) {
    Vec std_features = pca::standardize(raw_features, model.pca_model.standardization);
    std::vector<FeatureContribution> out;
    out.reserve(kNumFeatures);
    for (size_t i = 0; i < kNumFeatures; ++i) {
        Vec pull(3, 0.0);
        for (size_t c = 0; c < 3 && c < model.pca_model.loadings[i].size(); ++c)
            pull[c] = std_features[i] * model.pca_model.loadings[i][c];
        out.push_back({feature_names()[i], pull});
    }
    return out;
}

} // namespace vt::regime
