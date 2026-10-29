#pragma once
// Realized-vs-implied vol z-score BY TIME-OF-DAY. Controls for the intraday
// U-shaped vol pattern (higher at open/close, lower midday) by z-scoring
// today's RV(t)-IV(t) reading against the historical distribution sampled at
// the SAME time bucket (e.g. 9:30-9:45) rather than the whole-day distribution.
// Recomputed at each bar close against a pre-built historical time-bucket
// distribution (Section 5 / Framework doc Section 7).
#include <string>
#include <vector>
#include <chrono>
#include <cstdio>
#include "vt/types.h"
#include "zscore.h"

namespace vt::model {

// A 15-minute (configurable) time-of-day bucket label, e.g. "09:30-09:45".
using TodBucketKey = std::string;

inline TodBucketKey tod_bucket_key(const std::chrono::hh_mm_ss<std::chrono::minutes>& hms,
                                    int bucket_minutes = 15) {
    int total_min = hms.hours().count() * 60 + hms.minutes().count();
    int bucket_start = (total_min / bucket_minutes) * bucket_minutes;
    int bucket_end = bucket_start + bucket_minutes;
    auto fmt = [](int m) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", (m / 60) % 100, m % 60);
        return std::string(buf);
    };
    return fmt(bucket_start) + "-" + fmt(bucket_end);
}

// Historical baseline for one time bucket: distribution of (RV(t) - IV(t)) or
// (RV(t)/IV(t)) observed at that specific bucket across many past sessions.
// Populated by the historical-data adapter.
struct TodBaseline {
    TodBucketKey key;
    std::vector<double> history; // past sessions' RV-IV readings at this bucket
};

struct RvIvTodResult {
    TodBucketKey bucket;
    double current_reading{};   // today's RV(t) - IV(t) (or ratio, per house convention)
    double z{};
    size_t sample_n{};
};

// use_ratio: false => additive (RV - IV); true => multiplicative (RV / IV).
inline RvIvTodResult rv_iv_zscore(double rv_now, double iv_now, const TodBaseline& baseline, bool use_ratio = false) {
    double reading = use_ratio ? (iv_now > 1e-9 ? rv_now / iv_now : 0.0) : (rv_now - iv_now);
    auto dist = stats::fit(baseline.history);
    RvIvTodResult r;
    r.bucket = baseline.key;
    r.current_reading = reading;
    r.z = stats::zscore(reading, dist);
    r.sample_n = dist.n;
    return r;
}

} // namespace vt::model
