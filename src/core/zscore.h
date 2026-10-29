#pragma once
#include <vector>
#include <cmath>
#include <numeric>

namespace vt::stats {

struct Distribution {
    double mean{};
    double stdev{};
    size_t n{};
};

inline Distribution fit(const std::vector<double>& samples) {
    Distribution d;
    d.n = samples.size();
    if (d.n == 0) return d;
    d.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / d.n;
    if (d.n < 2) return d;
    double var = 0.0;
    for (double x : samples) var += (x - d.mean) * (x - d.mean);
    var /= (d.n - 1);
    d.stdev = std::sqrt(var);
    return d;
}

// z-score; returns 0 if stdev is degenerate (avoids div-by-zero on thin history).
// Per KB Part II.5: ALWAYS report `n` alongside any z-score/percentage — never
// present the number alone. Callers should surface d.n in the UI/insight text.
inline double zscore(double value, const Distribution& d) {
    if (d.stdev <= 1e-12) return 0.0;
    return (value - d.mean) / d.stdev;
}

} // namespace vt::stats
