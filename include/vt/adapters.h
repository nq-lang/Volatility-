#pragma once
// vt/adapters.h — abstract interfaces the quant core depends on. Nothing in
// src/core includes this file or any concrete adapter; the core only ever
// consumes plain ChainSnapshot/Bar/history vectors. Wire your live API by
// implementing these interfaces once you send it over — the scheduler
// (src/core/scheduler.h) and the regime model service are the only things
// that talk to them.

#include <vector>
#include <optional>
#include <chrono>
#include "vt/types.h"
#include "src/core/iv_zscore.h"
#include "src/core/rv_iv_zscore_tod.h"

namespace vt::adapters {

// Live options chain + underlying spot for one instrument.
class OptionsChainAdapter {
public:
    virtual ~OptionsChainAdapter() = default;
    virtual ChainSnapshot fetch_snapshot(const std::string& underlying) = 0;
};

// Recent OHLC bars for realized-vol estimators (1-min/5-min RTH bars).
class BarFeedAdapter {
public:
    virtual ~BarFeedAdapter() = default;
    virtual std::vector<Bar> recent_bars(const std::string& underlying,
                                          std::chrono::minutes lookback) = 0;
};

// Historical baselines for the z-score models. Implementations are expected
// to maintain rolling windows (e.g. trailing 60-90 sessions) keyed by
// (tenor, delta) for IV z-scores and by time-of-day bucket for RV/IV z-scores.
class HistoricalBaselineAdapter {
public:
    virtual ~HistoricalBaselineAdapter() = default;
    virtual model::IvHistoryBucket iv_history(const std::string& underlying,
                                               int dte_days, double delta) = 0;
    virtual model::TodBaseline tod_baseline(const std::string& underlying,
                                             const model::TodBucketKey& key) = 0;
};

// Supplies the rolling window of historical daily RAW feature vectors the PCA
// regime model's batch layer (§9) refits against. One row per trading day,
// each row's columns matching vt::regime::FeatureIndex exactly (see
// src/core/regime_model.h). Implementations own how they persist/compute this
// history; the model only ever consumes plain rows here, never raw chain data.
class RegimeHistoryAdapter {
public:
    virtual ~RegimeHistoryAdapter() = default;
    // window: how far back to pull, e.g. 6-12 months per the spec's §11.3 recommendation.
    virtual std::vector<std::vector<double>> trailing_feature_history(const std::string& underlying,
                                                                       std::chrono::hours window) = 0;
};

} // namespace vt::adapters
