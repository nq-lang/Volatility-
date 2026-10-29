#pragma once
// Implied vol z-scores — current IV vs. its own historical distribution, by
// tenor/delta bucket. Flags unusually rich/cheap vol vs. its own history.
#include <vector>
#include <string>
#include "vt/types.h"
#include "zscore.h"

namespace vt::model {

// A single (tenor, delta) bucket's historical IV baseline. `history` is the
// adapter-supplied window of past IV readings for this exact bucket
// (e.g. trailing 60-90 sessions' closing IV at this tenor/delta point).
struct IvHistoryBucket {
    int    dte_days{};
    double delta{};              // 0..1, |delta| convention
    std::vector<double> history; // populated by the historical-data adapter
};

struct IvZScoreResult {
    int    dte_days{};
    double delta{};
    double current_iv{};
    double z{};
    size_t sample_n{};
};

inline IvZScoreResult iv_zscore(double current_iv, const IvHistoryBucket& bucket) {
    auto dist = stats::fit(bucket.history);
    return IvZScoreResult{bucket.dte_days, bucket.delta, current_iv,
                           stats::zscore(current_iv, dist), dist.n};
}

} // namespace vt::model
