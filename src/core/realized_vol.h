#pragma once
// Realized-vol estimators used by VRP (src/core/vrp.h) and the time-of-day
// RV-vs-IV z-score model. Bar-close cadence per the build spec's Section 5.
#include <vector>
#include <cmath>
#include <numeric>
#include "vt/types.h"

namespace vt::rvol {

// Close-to-close realized vol, annualized. `bars_per_year` should reflect the
// bar size used (e.g. 1-min RTH bars ~ 98,280/yr; 5-min ~ 19,656/yr).
inline double close_to_close(const std::vector<Bar>& bars, double bars_per_year) {
    if (bars.size() < 2) return 0.0;
    std::vector<double> log_rets;
    log_rets.reserve(bars.size() - 1);
    for (size_t i = 1; i < bars.size(); ++i) {
        if (bars[i - 1].close > 0.0 && bars[i].close > 0.0)
            log_rets.push_back(std::log(bars[i].close / bars[i - 1].close));
    }
    if (log_rets.empty()) return 0.0;
    double mean = std::accumulate(log_rets.begin(), log_rets.end(), 0.0) / log_rets.size();
    double var = 0.0;
    for (double r : log_rets) var += (r - mean) * (r - mean);
    var /= (log_rets.size() > 1 ? (log_rets.size() - 1) : 1);
    return std::sqrt(var * bars_per_year);
}

// Parkinson estimator (uses high/low range, ~5x more efficient than close-to-close
// for the same sample size — the default recommended by the build spec).
inline double parkinson(const std::vector<Bar>& bars, double bars_per_year) {
    if (bars.empty()) return 0.0;
    double sum = 0.0;
    size_t n = 0;
    for (const auto& b : bars) {
        if (b.high > 0.0 && b.low > 0.0 && b.high >= b.low) {
            double lr = std::log(b.high / b.low);
            sum += lr * lr;
            ++n;
        }
    }
    if (n == 0) return 0.0;
    static const double factor = 1.0 / (4.0 * std::log(2.0));
    return std::sqrt(factor * (sum / n) * bars_per_year);
}

// Garman-Klass estimator (uses O/H/L/C, most information-efficient of the three;
// offered as the assistant's choice per the build spec's Section 5 wording).
inline double garman_klass(const std::vector<Bar>& bars, double bars_per_year) {
    if (bars.empty()) return 0.0;
    double sum = 0.0;
    size_t n = 0;
    for (const auto& b : bars) {
        if (b.high > 0 && b.low > 0 && b.open > 0 && b.close > 0) {
            double hl = std::log(b.high / b.low);
            double co = std::log(b.close / b.open);
            sum += 0.5 * hl * hl - (2.0 * std::log(2.0) - 1.0) * co * co;
            ++n;
        }
    }
    if (n == 0) return 0.0;
    double mean = sum / n;
    return mean > 0.0 ? std::sqrt(mean * bars_per_year) : 0.0;
}

} // namespace vt::rvol
