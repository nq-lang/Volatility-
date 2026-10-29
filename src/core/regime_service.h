#pragma once
// regime_service.h — the thing the scheduler actually holds and calls each
// cycle. Wraps regime_model.h's free functions with the statefulness §5/§6
// need: a fitted model (set by a batch refit that the scheduler calls on its
// own slower cadence, never per-tick), the pre-market baseline, the intraday
// trail, the trigger detector, and a rolling Speed history for the soft
// trigger's percentile check.
#include <deque>
#include <optional>
#include "regime_model.h"

namespace vt::regime {

class RegimeModelService {
public:
    explicit RegimeModelService(size_t speed_history_window = 60) : speed_window_(speed_history_window) {}

    // Batch/offline layer (§9): call on a scheduled cadence (nightly, or
    // hourly intraday) with the trailing window of historical raw feature
    // rows from a RegimeHistoryAdapter. Never call this per-tick.
    void refit_batch(const linalg::Mat& history_rows) {
        if (history_rows.size() < 20) return; // too few days to fit anything meaningful
        model_ = fit_batch(history_rows);
        // keep the session baseline, but re-project it through the NEW loadings so the
        // decay-path deviation is never computed across two different PCA spaces
        if (baseline_features_) { baseline_read_ = evaluate_live(*model_, *baseline_features_); }
    }

    // Underlying switched (SPX -> NDX, ...): the fitted model, baseline, trail and
    // Speed history all belong to the old instrument and must not leak across.
    void reset_for_new_underlying() {
        model_.reset(); baseline_features_.reset(); baseline_read_.reset();
        trail_.clear(); speed_history_.clear(); trigger_detector_ = TriggerDetector();
    }

    bool has_baseline() const { return baseline_features_.has_value(); }
    bool has_model() const { return model_.has_value(); }
    const RegimeModel& model() const { return *model_; }

    // Call once at the pre-market snapshot (§5). Stores the baseline the rest
    // of the session's intraday monitoring compares against.
    void establish_baseline(const linalg::Vec& raw_features) {
        if (!model_) return;
        baseline_features_ = raw_features;
        baseline_read_ = evaluate_live(*model_, raw_features);
        trail_.clear();
        trail_.push_back(baseline_read_->pca_coords);
    }

    // Live layer (§9): cheap projection + GMM eval only, called every cycle
    // with the freshly-assembled raw feature vector. Also runs the dual
    // -trigger check, updates the intraday trail, and computes the decay
    // -only-path deviation if a baseline has been established.
    struct LiveCycleResult {
        LiveRead live;
        TriggerState triggers;
        double decay_deviation = 0.0;
        bool mr_override_active = false;
        std::string narrative;
    };

    std::optional<LiveCycleResult> evaluate_cycle(const linalg::Vec& raw_features,
                                                    int remaining_session_hours,
                                                    double charm_per_hour, double color_per_hour) {
        if (!model_) return std::nullopt;

        LiveCycleResult out;
        out.live = evaluate_live(*model_, raw_features);
        trail_.push_back(out.live.pca_coords);

        speed_history_.push_back(raw_features[Speed_z]);
        if (speed_history_.size() > speed_window_) speed_history_.pop_front();
        std::vector<double> speed_hist_vec(speed_history_.begin(), speed_history_.end());
        out.triggers = trigger_detector_.update(raw_features[Speed_z], speed_hist_vec, raw_features[FlipDistancePct]);

        if (baseline_features_) {
            auto decay_path = regime::decay_only_path(*model_, *baseline_features_, charm_per_hour, color_per_hour,
                                                        std::max(0, remaining_session_hours));
            if (!decay_path.empty()) out.decay_deviation = decay_path_deviation(out.live.pca_coords, decay_path.back());
        }

        RegimeName predicted = argmax_regime(out.live.regime_probability);
        out.mr_override_active = mean_reversion_override_active(predicted, raw_features[CharmPerHour_z], raw_features[Vanna_z]);
        out.narrative = generate_narrative(predicted, raw_features);

        return out;
    }

    const std::vector<linalg::Vec>& intraday_trail() const { return trail_; }
    std::optional<LiveRead> baseline_read() const { return baseline_read_; }

private:
    std::optional<RegimeModel> model_;
    std::optional<linalg::Vec> baseline_features_;
    std::optional<LiveRead> baseline_read_;
    std::vector<linalg::Vec> trail_;
    TriggerDetector trigger_detector_;
    std::deque<double> speed_history_;
    size_t speed_window_;
};

} // namespace vt::regime
