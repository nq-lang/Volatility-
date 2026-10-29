#pragma once
// source_probe.h -- measures two facts about a CONNECTED data source that the PCA spec
// (§13) leaves open and that cannot be known from this codebase:
//   1. how often intraday open interest actually refreshes
//   2. how much historical feature/chain history exists for the 6-12 month rolling fit
// Both run against the abstract adapters, so the same code probes the real API once
// it is wired (src/app/probe_main.cpp) and a fake in tests/test_probe.cpp.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "vt/adapters.h"
#include "../core/regime_model.h"

namespace vt::tools {

// ---------- 1. intraday OI refresh cadence ----------
struct OiCadenceReport {
    size_t polls = 0, changes = 0;
    double median_interval_s = 0, min_interval_s = 0, max_interval_s = 0;
    bool refreshes_intraday = false;
    std::string verdict;
};

inline double oi_signature(const ChainSnapshot& c) {
    double s = 0.0; size_t i = 1;
    for (const auto& q : c.quotes) s += q.open_interest * static_cast<double>(1 + (i++ % 97));
    return s;
}

// Polls fetch_snapshot every `poll_s` seconds for `polls` polls and records when the OI vector changes.
// `sleep_s` is injected so tests run instantly and the real probe uses std::this_thread::sleep_for.
inline OiCadenceReport measure_oi_refresh_cadence(adapters::OptionsChainAdapter& src, const std::string& underlying,
                                                  size_t polls, double poll_s, const std::function<void(double)>& sleep_s) {
    OiCadenceReport r; r.polls = polls;
    double t = 0.0, last_sig = 0.0, last_change_t = -1.0; bool have = false;
    std::vector<double> intervals;
    for (size_t i = 0; i < polls; ++i) {
        double sig = oi_signature(src.fetch_snapshot(underlying));
        if (have && std::fabs(sig - last_sig) > 1e-9 * std::max(1.0, std::fabs(last_sig))) {
            ++r.changes;
            if (last_change_t >= 0.0) intervals.push_back(t - last_change_t);
            last_change_t = t;
        }
        last_sig = sig; have = true;
        sleep_s(poll_s); t += poll_s;
    }
    r.refreshes_intraday = r.changes > 0;
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        r.min_interval_s = intervals.front(); r.max_interval_s = intervals.back();
        r.median_interval_s = intervals[intervals.size() / 2];
    }
    char buf[400];
    if (!r.refreshes_intraday)
        std::snprintf(buf, sizeof buf, "OI did not change in %zu polls over %.0f s: treat OI as end-of-day only and keep the intraday OI-momentum features on the volume proxy.", polls, t);
    else if (intervals.empty())
        std::snprintf(buf, sizeof buf, "OI changed %zu time(s) in %.0f s but not often enough to estimate a cadence; extend the probe window.", r.changes, t);
    else
        std::snprintf(buf, sizeof buf, "OI refreshes about every %.0f s (min %.0f, max %.0f over %zu intervals). Set the regime batch-refit cadence and the OI-momentum window against this.",
                      r.median_interval_s, r.min_interval_s, r.max_interval_s, intervals.size());
    r.verdict = buf;
    return r;
}

// ---------- 2. historical depth ----------
struct DepthPoint { int requested_days; size_t rows_returned; double nan_fraction; bool wrong_width; };
struct DepthReport {
    std::vector<DepthPoint> points;
    size_t max_rows = 0;
    bool supports_6mo = false, supports_12mo = false;
    std::string verdict;
};

inline DepthReport probe_history_depth(adapters::RegimeHistoryAdapter& src, const std::string& underlying,
                                       const std::vector<int>& request_days = {30, 90, 180, 270, 365, 540, 730}) {
    DepthReport r;
    for (int d : request_days) {
        auto rows = src.trailing_feature_history(underlying, std::chrono::hours(24 * d));
        DepthPoint p{d, rows.size(), 0.0, false};
        size_t cells = 0, bad = 0;
        for (const auto& row : rows) {
            if (row.size() != regime::kNumFeatures) p.wrong_width = true;
            for (double v : row) { ++cells; bad += !std::isfinite(v); }
        }
        p.nan_fraction = cells ? static_cast<double>(bad) / cells : 0.0;
        r.max_rows = std::max(r.max_rows, p.rows_returned);
        r.points.push_back(p);
    }
    // ~252 trading days per 365 calendar days; require 85% coverage before claiming a window is supported
    auto covers = [&](int days) { for (auto& p : r.points) if (p.requested_days == days) return p.rows_returned >= static_cast<size_t>(0.85 * days * 252.0 / 365.0) && !p.wrong_width; return false; };
    r.supports_6mo = covers(180); r.supports_12mo = covers(365);
    char buf[400];
    std::snprintf(buf, sizeof buf, "max %zu trading-day rows available. 6-month window %s, 12-month window %s.%s",
                  r.max_rows, r.supports_6mo ? "supported" : "NOT supported", r.supports_12mo ? "supported" : "NOT supported",
                  r.supports_6mo ? "" : " Shorten the rolling window or backfill history before relying on the regime fit (the spec's §10 stability gate also needs more than one window of rows).");
    r.verdict = buf;
    return r;
}

inline void print(const OiCadenceReport& c, const DepthReport& d, std::FILE* f = stdout) {
    std::fprintf(f, "DATA SOURCE PROBE\n[OI cadence] %s\n[history depth] %s\n", c.verdict.c_str(), d.verdict.c_str());
    for (auto& p : d.points) std::fprintf(f, "  request %4d d -> %5zu rows, non-finite cells %.2f%%%s\n", p.requested_days, p.rows_returned, 100 * p.nan_fraction, p.wrong_width ? ", WRONG ROW WIDTH" : "");
}

} // namespace vt::tools
