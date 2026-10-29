#pragma once
// Scheduler — recomputes every Section-1 model at the cadence specified in
// Section 5 of the build spec, and pushes results through the UI bridge.
// Deliberately has NO webview dependency: `bridge::Sink` can be a no-op
// (headless backtest mode) or wired to the real web view in src/app/main.cpp.
// This is the "same engine runs headless for backtesting" requirement.
#include <chrono>
#include <array>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include "vt/types.h"
#include "vt/dte.h"
#include "vt/adapters.h"
#include "gex.h"
#include "dex.h"
#include "vanna.h"
#include "charm.h"
#include "speed_color.h"
#include "theta.h"
#include "vrp.h"
#include "rnd.h"
#include "delta_iv_surface.h"
#include "iv_zscore.h"
#include "oi_momentum.h"
#include "gamma_flip.h"
#include "rv_iv_zscore_tod.h"
#include "realized_vol.h"
#include "regime_service.h"
#include "../bridge/ui_bridge.h"
#include "../bridge/regime_bridge.h"
#include "../viz/regime_viz.h"
#include "../multibook/mb_bridge.h"
#include "../multibook/cross_block.h"
#include <map>

namespace vt::core {

struct SchedulerConfig {
    std::string underlying = "SPX";
    exposure::DealerConvention convention{}; // house convention: dealers long calls / short puts (state explicitly in the UI)
    bool   use_volume_proxy_intraday = true; // true during RTH before EOD OI settles
    double bars_per_year = 98280.0;    // ~1-min RTH bars/year; adjust to your bar size
    std::chrono::seconds fast_cadence{30};   // IV surfaces / all greeks / GEX-DEX-Vanna-Charm
    std::chrono::seconds rnd_cadence{180};   // RND: every few minutes
    std::chrono::seconds bar_close_cadence{60}; // VRP RV leg / RV-IV z-score, at bar close
    // Approximation: used only to estimate remaining-session-hours for the PCA
    // regime model's decay-only projection (§6). Replace with a real market
    // -calendar lookup for your venue/timezone in production.
    int assumed_session_close_utc_hour = 21; // e.g. ~4pm ET during EDT
    // Display-only embedding layer (t-SNE views). Publishes extra channels, never feeds the classifier.
    viz::VizConfig viz{};
};

// Snapshot of every Section-1 model's latest output for one cycle — this is
// exactly what gets handed to the Section 2 structural-insight engine each
// cycle (see src/insight/structural_insight_engine.h).
struct ModelState {
    double spot{};
    bool convention_inverted{false}; // false = house default (dealers long calls / short puts)
    std::array<ExposureCurve, 4> gex, dex, vanna, charm, speed, color, theta; // indexed by DteBucket
    std::array<Surface, 4> vrp, call_iv, put_iv;
    Surface rnd; // stacked across expiries already; not DTE-bucketed the same way
    model::GammaFlipResult gamma_flip;
    double call_oi_ratio_term{};
    double put_oi_ratio_term{};
    std::optional<double> call_oi_momentum;
    std::optional<double> put_oi_momentum;
    // Additional readouts assembled specifically to feed the PCA regime
    // model's 17-feature vector (see src/core/regime_model.h); also useful
    // standalone (e.g. ATM IV z-score isn't otherwise exposed elsewhere).
    model::IvZScoreResult iv_z_atm;      // delta = 0.5, term bucket
    model::IvZScoreResult iv_z_25d_put;  // delta = 0.25 (put side), term bucket
    std::optional<double> risk_reversal_25d;      // put IV(25d) - call IV(25d), term bucket
    std::optional<double> risk_reversal_25d_momentum;
    model::RndShapeStats rnd_shape_term; // term-bucket RND skew/kurtosis proxies
    model::RvIvTodResult rv_iv_z;
};

class Scheduler {
public:
    Scheduler(adapters::OptionsChainAdapter& chain_src,
              adapters::BarFeedAdapter& bar_src,
              adapters::HistoricalBaselineAdapter& hist_src,
              bridge::Sink sink,
              SchedulerConfig cfg = {})
        : chain_src_(chain_src), bar_src_(bar_src), hist_src_(hist_src),
          sink_(std::move(sink)), cfg_(std::move(cfg)), call_oi_momentum_tracker_(20), put_oi_momentum_tracker_(20),
          rr25_momentum_tracker_(20) {}

    // Attaches the PCA regime model (optional -- the scheduler runs fine
    // without one attached, simply skipping regime_live publication). The
    // service is expected to already be constructed by the caller; batch
    // refitting (§9) is triggered separately via refit_regime_model(), on
    // whatever slower cadence the app layer chooses (nightly/hourly), never
    // from inside run_cycle()'s fast path.
    void attach_regime_model(regime::RegimeModelService& svc, adapters::RegimeHistoryAdapter* history_src = nullptr) {
        regime_service_ = &svc;
        regime_history_src_ = history_src;
    }

    // Batch/offline layer (§9): pulls the trailing feature history (if a
    // RegimeHistoryAdapter was attached) and refits PCA+GMM. Publishes the
    // resulting static biplot payload once the refit completes. Returns false
    // if no regime model/history source is attached, or too little history exists.
    bool refit_regime_model(std::chrono::hours window = std::chrono::hours(24 * 270)) {
        apply_pending_underlying();
        if (!regime_service_ || !regime_history_src_) return false;
        auto history = regime_history_src_->trailing_feature_history(cfg_.underlying, window);
        regime_service_->refit_batch(history);
        baseline_pending_ = baseline_pending_ || !regime_service_->has_baseline();
        if (!regime_service_->has_model()) return false;
        // The static payload needs each historical day's PCA point + which
        // cluster it fell into, for the point-cloud part of the biplot.
        std::vector<std::vector<double>> pca_points;
        std::vector<int> cluster_ids;
        const auto& m = regime_service_->model();
        for (const auto& row : history) {
            auto std_row = pca::standardize(row, m.pca_model.standardization);
            pca_points.push_back(m.pca_model.project(std_row));
            auto resp = gmm::responsibilities(pca_points.back(), m.gmm_model);
            int best = 0;
            for (size_t i = 1; i < resp.size(); ++i) if (resp[i] > resp[best]) best = static_cast<int>(i);
            cluster_ids.push_back(best);
        }
        bridge::publish(sink_, "regime_static", bridge::regime_static_json(m, pca_points, cluster_ids));
        // Display-only: same batch, same standardized matrix, published on its OWN channel so the
        // regime_static / regime_live payloads (and everything the classifier computes) are untouched.
        if (cfg_.viz.enabled) {
            const viz::VizState* prev = nullptr;
            auto it = viz_cache_.find(cfg_.underlying);
            if (it != viz_cache_.end()) prev = &it->second;
            viz::VizState st = viz::build_viz_state(m, history, cfg_.underlying, cfg_.viz, prev);
            bridge::publish(sink_, "regime_viz", viz::viz_static_json(m, st));
            viz_cache_[cfg_.underlying] = std::move(st);
        }
        return true;
    }

    // Thread-safe request to switch the underlying (SPX/SPY/NDX/QQQ). Safe to call
    // from the UI thread; applied at the top of the next cycle/refit so a cycle is
    // never half old-instrument, half new.
    void set_underlying(const std::string& u) { std::lock_guard<std::mutex> l(pending_mu_); pending_underlying_ = u; }
    const std::string& underlying() const { return cfg_.underlying; }

    // Multi-book / family scope (Task 2). Optional: with no registry attached or mode == Single, run_cycle() is unchanged.
    // Chains for out-of-scope books are never requested (ScopedChainSource refuses them).
    void attach_multibook(const reg::Registry& r) { registry_ = &r; }
    // multibook_v1 model service: its card is published on `multibook_model` only in Multi-Book / Family scope, and only when (family, ticker, days) changed (refit is walk-forward, never per tick).
    // Called after every non-Single multibook publish with the aggregate (used to drive the scoped LLM analysis; kept as a hook so scheduler.h does not depend on the insight layer).
    void on_aggregate(std::function<void(const mb::AggregateResult&)> f) { agg_hook_ = std::move(f); }
    void attach_model_service(mb::MultibookModelService& m) { model_svc_ = &m; }
    void set_scope(const scope::Scope& s) { std::lock_guard<std::mutex> l(scope_mu_); scope_ = s; }
    mb::BookCache& book_cache() { return book_cache_; }

    // Runs exactly one full recompute cycle. In production this is called from
    // a timer loop (see src/app/main.cpp); in backtests it's called directly
    // per historical tick/bar with no webview attached (headless mode).
    ModelState run_cycle() {
        if (apply_pending_underlying() && regime_service_) refit_regime_model();
        ChainSnapshot chain = chain_src_.fetch_snapshot(cfg_.underlying);
        ModelState state;
        state.spot = chain.spot;
        state.convention_inverted = cfg_.convention.invert;
        bridge::publish(sink_, "spot", bridge::to_json_scalar("spot", chain.spot));

        for (uint8_t i = 0; i < static_cast<uint8_t>(DteBucket::Count); ++i) {
            auto b = static_cast<DteBucket>(i);
            state.gex[i]   = model::gex(chain, b, cfg_.convention, cfg_.use_volume_proxy_intraday);
            state.dex[i]   = model::dex(chain, b, cfg_.convention, cfg_.use_volume_proxy_intraday);
            state.vanna[i] = model::vanna_exposure(chain, b, cfg_.use_volume_proxy_intraday);
            state.charm[i] = model::charm_exposure(chain, b, cfg_.use_volume_proxy_intraday);
            state.speed[i] = model::speed_exposure(chain, b, cfg_.use_volume_proxy_intraday);
            state.color[i] = model::color_exposure(chain, b, cfg_.use_volume_proxy_intraday);
            state.theta[i] = model::theta_harvest(chain, b, cfg_.use_volume_proxy_intraday);
            state.call_iv[i] = model::call_delta_iv_surface(chain, b);
            state.put_iv[i]  = model::put_delta_iv_surface(chain, b);

            publish_curve(state.gex[i], "gex");
            publish_curve(state.dex[i], "dex");
            publish_curve(state.vanna[i], "vanna");
            publish_curve(state.charm[i], "charm");
            publish_curve(state.speed[i], "speed");
            publish_curve(state.color[i], "color");
            publish_curve(state.theta[i], "theta");
            bridge::publish(sink_, "call_delta_iv", bridge::to_json(state.call_iv[i]));
            bridge::publish(sink_, "put_delta_iv", bridge::to_json(state.put_iv[i]));
        }

        // VRP: IV leg continuous, RV leg at bar close.
        auto bars = bar_src_.recent_bars(cfg_.underlying, std::chrono::minutes(120));
        for (uint8_t i = 0; i < static_cast<uint8_t>(DteBucket::Count); ++i) {
            auto b = static_cast<DteBucket>(i);
            state.vrp[i] = model::vrp_surface(chain, b, bars, cfg_.bars_per_year);
            bridge::publish(sink_, "vrp", bridge::to_json(state.vrp[i]));
        }

        // RND: assumed called at its own slower cadence by the app-layer timer;
        // computed here unconditionally for simplicity/headless backtest parity.
        for (uint8_t i = 0; i < static_cast<uint8_t>(DteBucket::Count); ++i) {
            auto b = static_cast<DteBucket>(i);
            Surface rb = model::rnd_surface(chain, b);
            if (b == DteBucket::Term) state.rnd = rb;   // term (all expiries) feeds the shape stats / regime model
            bridge::publish(sink_, "rnd", bridge::to_json(rb));
        }

        // Gamma flip: the regime filter, computed off the all-DTE/term bucket per spec.
        state.gamma_flip = model::solve_gamma_flip(chain, DteBucket::Term, 0.10, 41,
                                                     cfg_.convention, cfg_.use_volume_proxy_intraday);
        bridge::publish(sink_, "gamma_flip", bridge::to_json_scalar(
            "gamma_flip_distance", state.gamma_flip.distance, 0,
            "\"regime\":\"" + std::string(state.gamma_flip.regime_long_gamma ? "long_gamma" : "short_gamma") + "\""
            ",\"flip_level\":" + (state.gamma_flip.flip_level ? std::to_string(*state.gamma_flip.flip_level) : "null")));

        // Call/Put OI ratio momentum: term bucket, rate of change not level (12_/13_).
        state.call_oi_ratio_term = model::call_oi_ratio(chain, DteBucket::Term, cfg_.use_volume_proxy_intraday);
        state.put_oi_ratio_term  = model::put_oi_ratio(chain, DteBucket::Term, cfg_.use_volume_proxy_intraday);
        state.call_oi_momentum = call_oi_momentum_tracker_.update(state.call_oi_ratio_term);
        state.put_oi_momentum  = put_oi_momentum_tracker_.update(state.put_oi_ratio_term);
        bridge::publish(sink_, "call_oi_momentum", bridge::to_json_scalar(
            "call_oi_ratio_momentum", state.call_oi_momentum.value_or(0.0)));
        bridge::publish(sink_, "put_oi_momentum", bridge::to_json_scalar(
            "put_oi_ratio_momentum", state.put_oi_momentum.value_or(0.0)));

        // IV z-scores: representative 25-delta call/put buckets per DTE, term-level.
        // (Extend to the full delta ladder by looping target deltas if your dashboard
        // wants a full z-score surface rather than headline points.)
        for (double target_delta : {0.25}) {
            for (int dte_days : {0, 1, 2}) {
                auto iv_hist = hist_src_.iv_history(cfg_.underlying, dte_days, target_delta);
                double current = nearest_iv(state.call_iv[static_cast<uint8_t>(vt::classify_dte(dte_days))], target_delta, dte_days);
                auto z = model::iv_zscore(current, iv_hist);
                bridge::publish(sink_, "iv_zscore", bridge::to_json_scalar(
                    "iv_zscore", z.z, z.sample_n,
                    "\"dte\":" + std::to_string(z.dte_days) + ",\"delta\":" + std::to_string(z.delta)));
            }
        }

        // RV-vs-IV time-of-day z-score, sampled against the current 15-min bucket.
        double rv_now = 0.0;
        {
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            std::tm tmv{}; gmtime_r(&tt, &tmv);
            auto hms = std::chrono::hh_mm_ss<std::chrono::minutes>(
                std::chrono::hours(tmv.tm_hour) + std::chrono::minutes(tmv.tm_min));
            auto key = model::tod_bucket_key(hms, 15);
            auto baseline = hist_src_.tod_baseline(cfg_.underlying, key);
            rv_now = rvol::parkinson(bars, cfg_.bars_per_year);
            double iv_now = model::atm_vrp(state.vrp[static_cast<uint8_t>(DteBucket::D0)], chain.spot).value_or(0.0) + rv_now; // recover ATM IV = VRP + RV
            auto r = model::rv_iv_zscore(rv_now, iv_now, baseline);
            bridge::publish(sink_, "rv_iv_zscore_tod", bridge::to_json_scalar(
                "rv_iv_zscore_tod", r.z, r.sample_n, "\"bucket\":\"" + r.bucket + "\""));
            state.rv_iv_z = r;
        }

        // ---- extra readouts assembled specifically for the PCA regime model ----
        {
            auto iv_hist_atm = hist_src_.iv_history(cfg_.underlying, 0, 0.5);
            double atm_iv_now = nearest_iv(state.call_iv[static_cast<uint8_t>(DteBucket::Term)], 0.5, 0);
            state.iv_z_atm = model::iv_zscore(atm_iv_now, iv_hist_atm);

            auto iv_hist_25p = hist_src_.iv_history(cfg_.underlying, 0, 0.25);
            double put25_now = nearest_iv(state.put_iv[static_cast<uint8_t>(DteBucket::Term)], 0.25, 0);
            state.iv_z_25d_put = model::iv_zscore(put25_now, iv_hist_25p);

            state.risk_reversal_25d = model::skew_divergence(state.call_iv[static_cast<uint8_t>(DteBucket::Term)],
                                                               state.put_iv[static_cast<uint8_t>(DteBucket::Term)], 0.25, 0);
            state.risk_reversal_25d_momentum = rr25_momentum_tracker_.update(state.risk_reversal_25d.value_or(0.0));

            // RND is stacked across all expiries in one Surface; use the longest
            // -dated expiry present as the "term" read for the regime model.
            int max_dte = 0;
            for (const auto& p : state.rnd.points) max_dte = std::max(max_dte, p.dte_days);
            state.rnd_shape_term = model::shape_stats(state.rnd, max_dte, chain.spot, 50.0);
        }

        // ---- PCA regime model: live layer only (cheap projection + GMM eval).
        // Batch refit happens separately via refit_regime_model(), never here. ----
        if (regime_service_ && regime_service_->has_model()) {
            linalg::Vec features(regime::kNumFeatures, 0.0);
            features[regime::NetGEX_z]        = exposure::net(state.gex[static_cast<uint8_t>(DteBucket::Term)]);
            features[regime::FlipDistancePct] = chain.spot > 0 ? (state.gamma_flip.distance / chain.spot) * 100.0 : 0.0;
            features[regime::Speed_z]         = exposure::net(state.speed[static_cast<uint8_t>(DteBucket::Term)]);
            features[regime::CharmPerHour_z]  = exposure::net(state.charm[static_cast<uint8_t>(DteBucket::D1)]);
            features[regime::Color_z]         = exposure::net(state.color[static_cast<uint8_t>(DteBucket::Term)]);
            features[regime::Vanna_z]         = exposure::net(state.vanna[static_cast<uint8_t>(DteBucket::Term)]);
            features[regime::DEX_z]           = exposure::net(state.dex[static_cast<uint8_t>(DteBucket::Term)]);
            features[regime::CallOiMomentum]  = state.call_oi_momentum.value_or(0.0);
            features[regime::PutOiMomentum]   = state.put_oi_momentum.value_or(0.0);
            features[regime::ImpliedZ_ATM]    = state.iv_z_atm.z;
            features[regime::ImpliedZ_25dPut] = state.iv_z_25d_put.z;
            features[regime::VRP_z]           = model::atm_vrp(state.vrp[static_cast<uint8_t>(DteBucket::Term)], chain.spot).value_or(0.0);
            features[regime::RvIvZscoreTod]   = state.rv_iv_z.z;
            features[regime::RiskReversal25d_z]        = state.risk_reversal_25d.value_or(0.0);
            features[regime::RiskReversal25d_Momentum] = state.risk_reversal_25d_momentum.value_or(0.0);
            features[regime::RndSkewness_z]   = state.rnd_shape_term.skewness_proxy;
            features[regime::RndKurtosis_z]   = state.rnd_shape_term.kurtosis_proxy;

            auto now_tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm now_tm{}; gmtime_r(&now_tt, &now_tm);
            int remaining_hours = std::max(0, cfg_.assumed_session_close_utc_hour - now_tm.tm_hour);

            // Section 5 pre-market baseline: the FIRST feature vector of each trading date
            // (and the first after startup, a refit, or an underlying switch) becomes the
            // session baseline the decay-only path is measured against. Keyed on the UTC
            // date of the wall clock, so no market-calendar hook is needed: the first
            // cycle of a day is by definition the earliest snapshot of that session.
            if (baseline_pending_ || now_tm.tm_yday != baseline_yday_) {
                regime_service_->establish_baseline(features);
                baseline_pending_ = false;
                baseline_yday_ = now_tm.tm_yday;
            }

            // Reuse the (already per-hour, per the feature definition) Charm/Color
            // readouts directly as the decay path's hourly increments -- see
            // regime_model.h's decay_only_path() docs for the approximation this implies.
            auto cycle_result = regime_service_->evaluate_cycle(features, remaining_hours,
                                                                  features[regime::CharmPerHour_z], features[regime::Color_z]);
            if (cycle_result) {
                bool directional_bias_bullish = state.dex[static_cast<uint8_t>(DteBucket::Term)].points.empty()
                    ? true : exposure::net(state.dex[static_cast<uint8_t>(DteBucket::Term)]) >= 0.0;
                std::vector<std::vector<double>> trail;
                for (const auto& p : regime_service_->intraday_trail()) trail.push_back(p);
                auto contributions = regime::feature_contributions(regime_service_->model(), features);
                bridge::publish(sink_, "regime_live", bridge::regime_live_json(
                    cycle_result->live, cycle_result->triggers, cycle_result->narrative,
                    features[regime::FlipDistancePct], features[regime::Speed_z],
                    cycle_result->mr_override_active, cycle_result->decay_deviation, trail,
                    directional_bias_bullish, contributions));
                auto vit = viz_cache_.find(cfg_.underlying);
                if (cfg_.viz.enabled && vit != viz_cache_.end())
                    bridge::publish(sink_, "regime_viz_live", viz::viz_live_json(regime_service_->model(), vit->second, features, cfg_.viz));
            }
        }

        publish_multibook();
        return state;
    }

private:
    std::function<void(const mb::AggregateResult&)> agg_hook_;
    mb::MultibookModelService* model_svc_ = nullptr; std::string last_model_key_;
    void publish_multibook() {
        if (!registry_) return;
        scope::Scope sc; { std::lock_guard<std::mutex> l(scope_mu_); sc = scope_; }
        if (sc.mode == scope::Mode::Single) return;
        try {
            auto res = mb::run_scope(*registry_, sc, chain_src_, book_cache_, mb_opts_);
            bridge::publish(sink_, "multibook", mb::multibook_json(res));
            if (res.lens) bridge::publish(sink_, "strike_lens", mb::lens_json(*res.lens));
            if (agg_hook_) { agg_hook_(res); }
            if (model_svc_) { std::string k = mb::MultibookModelService::key(sc.family, sc.ticker) + "|" + std::to_string(model_svc_->days(sc.family, sc.ticker));
                if (k != last_model_key_) { last_model_key_ = k; bridge::publish(sink_, "multibook_model", model_svc_->status_json(sc.family, sc.ticker, sc.snapshot_ts)); } }
        } catch (const std::exception& e) {
            bridge::publish(sink_, "multibook", std::string("{\"error\":\"") + json::esc(e.what()) + "\"}");
        }
    }

    // Finds the closest surface point to (target_delta, dte_days); returns 0 if the
    // surface has no points yet (e.g. thin/no chain data for that bucket).
    static double nearest_iv(const Surface& surf, double target_delta, int dte_days) {
        const SurfacePoint* best = nullptr;
        double best_dist = 1e18;
        for (const auto& p : surf.points) {
            if (p.dte_days != dte_days) continue;
            double d = std::abs(p.x - target_delta);
            if (d < best_dist) { best_dist = d; best = &p; }
        }
        return best ? best->iv : 0.0;
    }

    void publish_curve(const ExposureCurve& c, const std::string& model_name) {
        bridge::publish(sink_, model_name, bridge::to_json(c, model_name));
    }

    adapters::OptionsChainAdapter& chain_src_;
    adapters::BarFeedAdapter& bar_src_;
    adapters::HistoricalBaselineAdapter& hist_src_;
    bridge::Sink sink_;
    const reg::Registry* registry_ = nullptr; scope::Scope scope_; std::mutex scope_mu_; mb::BookCache book_cache_; mb::EngineOptions mb_opts_;
    SchedulerConfig cfg_;
    // Returns true if the underlying changed. Per-instrument rolling state is cleared.
    bool apply_pending_underlying() {
        std::string u;
        { std::lock_guard<std::mutex> l(pending_mu_); u.swap(pending_underlying_); }
        if (u.empty() || u == cfg_.underlying) return false;
        cfg_.underlying = u;
        call_oi_momentum_tracker_ = model::CallOiRatioMomentum(20);
        put_oi_momentum_tracker_ = model::PutOiRatioMomentum(20);
        rr25_momentum_tracker_ = model::OiRatioMomentumTracker(20);
        if (regime_service_) regime_service_->reset_for_new_underlying();
        baseline_pending_ = true;
        return true;
    }
    std::map<std::string, viz::VizState> viz_cache_;   // per ticker, versioned by batch id; display only
    bool baseline_pending_ = true;
    int baseline_yday_ = -1;
    std::mutex pending_mu_;
    std::string pending_underlying_;
    model::CallOiRatioMomentum call_oi_momentum_tracker_;
    model::PutOiRatioMomentum put_oi_momentum_tracker_;
    model::OiRatioMomentumTracker rr25_momentum_tracker_;
    regime::RegimeModelService* regime_service_ = nullptr;
    adapters::RegimeHistoryAdapter* regime_history_src_ = nullptr;
};

} // namespace vt::core
