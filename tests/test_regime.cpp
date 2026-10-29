// Headless smoke test for the PCA + GMM regime model: builds synthetic
// 17-dim standardized feature data with 5 well-separated archetype clusters,
// fits the model, checks explained variance is reported, checks the label
// mapping recovers sensible regime assignments, checks live projection +
// triggers + decay path + narrative all run without crashing.
#include <cassert>
#include <iostream>
#include <random>
#include "vt/pca.h"
#include "vt/gmm.h"
#include "../src/core/regime_model.h"
#include "../src/bridge/regime_bridge.h"

using namespace vt;
using namespace vt::regime;

// Builds one synthetic day's raw (already "standardized-scale", i.e. roughly
// z-score-shaped) feature vector for a given archetype, with noise.
static Vec synthetic_day(RegimeName archetype, std::mt19937& rng) {
    std::normal_distribution<double> noise(0.0, 0.35);
    Vec f(kNumFeatures, 0.0);
    for (auto& v : f) v = noise(rng);

    switch (archetype) {
        case RegimeName::PinningLowVol:
            f[NetGEX_z] = 2.2 + noise(rng); f[FlipDistancePct] = 1.8 + noise(rng);
            f[Speed_z] = -1.2 + noise(rng); f[RvIvZscoreTod] = -1.0 + noise(rng);
            break;
        case RegimeName::HighVolatilityRisk:
            f[ImpliedZ_ATM] = 2.4 + noise(rng); f[RvIvZscoreTod] = 2.1 + noise(rng);
            break;
        case RegimeName::TrendingDirectional:
            f[NetGEX_z] = -2.3 + noise(rng); f[FlipDistancePct] = -1.9 + noise(rng);
            f[Speed_z] = 2.0 + noise(rng);
            break;
        case RegimeName::MeanReversion:
            f[NetGEX_z] = 0.6 + noise(rng); f[Speed_z] = -0.8 + noise(rng);
            break;
        case RegimeName::TransitionalBreakout:
            f[FlipDistancePct] = 0.05 + noise(rng) * 0.3; f[Speed_z] = 1.8 + noise(rng);
            break;
        default: break;
    }
    return f;
}

int main() {
    std::mt19937 rng(123);
    Mat rows;
    std::vector<RegimeName> truth;
    RegimeName archetypes[5] = { RegimeName::PinningLowVol, RegimeName::HighVolatilityRisk,
                                 RegimeName::TrendingDirectional, RegimeName::MeanReversion,
                                 RegimeName::TransitionalBreakout };
    for (int i = 0; i < 400; ++i) {
        RegimeName a = archetypes[i % 5];
        rows.push_back(synthetic_day(a, rng));
        truth.push_back(a);
    }

    // --- batch fit ---
    RegimeModel model = fit_batch(rows);
    std::cout << "[ok] PCA fit on " << model.pca_model.n_days_in_fit << " days\n";
    auto ev = model.pca_model.explained_variance_fraction();
    double cum = model.pca_model.cumulative_explained_variance();
    std::cout << "[ok] explained variance PC1=" << ev[0] << " PC2=" << ev[1] << " PC3=" << ev[2]
              << " cumulative=" << cum << "\n";
    assert(cum > 0.0 && cum <= 1.0001);

    std::cout << "[ok] label mapping: ";
    for (size_t c = 0; c < 5; ++c)
        std::cout << "cluster" << c << "->" << to_string(model.label_mapping.cluster_to_regime[c])
                  << "(conf=" << model.label_mapping.confidence[c] << ") ";
    std::cout << "\n";

    // Every regime should appear exactly once in the mapping (bijective).
    std::array<int, 5> seen{};
    for (auto r : model.label_mapping.cluster_to_regime) seen[static_cast<size_t>(r)]++;
    for (int count : seen) assert(count == 1);
    std::cout << "[ok] label mapping is a valid bijection across all 5 regimes\n";

    // --- live evaluation: classify a fresh Trending-archetype day, expect it argmaxes to Trending ---
    Vec fresh = synthetic_day(RegimeName::TrendingDirectional, rng);
    LiveRead live = evaluate_live(model, fresh);
    RegimeName predicted = argmax_regime(live.regime_probability);
    std::cout << "[ok] live PCA coords: (" << live.pca_coords[0] << ", " << live.pca_coords[1]
              << ", " << live.pca_coords[2] << ")\n";
    std::cout << "[ok] live regime probabilities: ";
    for (size_t i = 0; i < 5; ++i) std::cout << to_string(static_cast<RegimeName>(i)) << "=" << live.regime_probability[i] << " ";
    std::cout << "\n[ok] predicted regime for a fresh Trending-archetype day: " << to_string(predicted) << "\n";
    assert(predicted == RegimeName::TrendingDirectional);

    // --- dual trigger system ---
    TriggerDetector trig(0.8);
    std::vector<double> speed_hist;
    for (int i = 0; i < 50; ++i) speed_hist.push_back(std::normal_distribution<double>(0, 1)(rng));
    auto st1 = trig.update(0.1, speed_hist, -5.0);   // low speed, negative flip
    assert(!st1.soft_active);
    auto st2 = trig.update(4.0, speed_hist, 5.0);    // very high speed -> soft trigger; flip flipped sign -> hard trigger
    assert(st2.soft_active);
    assert(st2.hard_active);
    std::cout << "[ok] dual-trigger system: soft=" << st2.soft_active << " hard=" << st2.hard_active << "\n";

    // --- decay-only path + deviation ---
    auto path = decay_only_path(model, fresh, /*charm_per_hour=*/-0.3, /*color_per_hour=*/-0.1, /*remaining_hours=*/4);
    assert(path.size() > 1);
    double dev = decay_path_deviation(live.pca_coords, path.back());
    std::cout << "[ok] decay-only path has " << path.size() << " points, live-vs-decay deviation=" << dev << "\n";

    // --- mean-reversion override ---
    bool override_active = mean_reversion_override_active(RegimeName::MeanReversion, /*charm_z=*/3.2, /*vanna_z=*/0.1);
    assert(override_active);
    std::cout << "[ok] mean-reversion override correctly flags large charm_z\n";

    // --- narrative ---
    std::string narrative = generate_narrative(predicted, fresh);
    std::cout << "[ok] narrative: " << narrative << "\n";
    assert(!narrative.empty());

    // --- bridge serialization ---
    std::vector<std::vector<double>> pca_points_hist;
    std::vector<int> cluster_ids_hist;
    for (const auto& r : rows) {
        Vec std_r = pca::standardize(r, model.pca_model.standardization);
        pca_points_hist.push_back(model.pca_model.project(std_r));
        cluster_ids_hist.push_back(0); // placeholder cluster id for the smoke test
    }
    std::string static_json = bridge::regime_static_json(model, pca_points_hist, cluster_ids_hist);
    assert(!static_json.empty());
    std::cout << "[ok] regime_static_json length=" << static_json.size() << "\n";

    std::vector<std::vector<double>> trail = { live.pca_coords };
    std::string live_json = bridge::regime_live_json(live, st2, narrative, 5.0, 4.0, override_active, dev, trail, true);
    assert(!live_json.empty());
    std::cout << "[ok] regime_live_json length=" << live_json.size() << "\n";

    std::cout << "\nALL REGIME MODEL SMOKE TESTS PASSED\n";
    return 0;
}
