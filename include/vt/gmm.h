#pragma once
// vt/gmm.h — Layer B of the two-layer regime model: a Gaussian Mixture Model,
// fit via EM, producing a full probability distribution across regimes (not a
// hard label) per §2 of the PCA build spec. Dimension-agnostic: the spec asks
// to "test both" fitting on the PCA-reduced space and the full standardized
// feature space during calibration (§10.2) — this same implementation
// supports either, since it only depends on Vec dimensionality, not on being
// specifically 3D. The regime_model.h default fits on the 3D PCA space.
#include "vt/linalg.h"
#include <cmath>
#include <numbers>
#include <random>
#include <algorithm>

namespace vt::gmm {

using linalg::Vec;
using linalg::Mat;

struct Component {
    Vec mean;
    Mat covariance;
    double weight = 0.0; // mixture weight (prior), sums to 1 across all components
};

struct GmmModel {
    std::vector<Component> components; // one per regime (5, per §4)
    int n_iterations_run = 0;
    double final_log_likelihood = 0.0;
};

inline double gaussian_density(const Vec& x, const Component& c) {
    size_t d = x.size();
    auto inv = linalg::inverse_and_det(c.covariance);
    if (!inv.ok || inv.det <= 0.0) return 1e-300; // degenerate covariance guard
    Vec diff(d);
    for (size_t i = 0; i < d; ++i) diff[i] = x[i] - c.mean[i];
    Vec mdiff = linalg::mat_vec(inv.inverse, diff);
    double mahalanobis = linalg::dot(diff, mdiff);
    double norm_const = std::pow(2.0 * std::numbers::pi, -static_cast<double>(d) / 2.0) * std::pow(inv.det, -0.5);
    return norm_const * std::exp(-0.5 * mahalanobis);
}

// Responsibility (soft-assignment) vector: P(component_k | x) for every
// component, summing to 1 -- this IS the "regime probability vector" the data
// contract (§7) requires, evaluated in the live path against fixed parameters.
inline Vec responsibilities(const Vec& x, const GmmModel& model) {
    Vec r(model.components.size());
    double total = 0.0;
    for (size_t k = 0; k < model.components.size(); ++k) {
        r[k] = model.components[k].weight * gaussian_density(x, model.components[k]);
        total += r[k];
    }
    if (total > 1e-300) for (double& v : r) v /= total;
    else for (double& v : r) v = 1.0 / static_cast<double>(r.size()); // fully ambiguous fallback: uniform
    return r;
}

// Batch/offline EM fit (§9): run on the same rolling window as the PCA fit.
// k-means++-style seeding for initial means keeps EM from collapsing components
// on a bad random start; small ridge added to covariances for numerical stability.
inline GmmModel fit_once(const Mat& points, size_t k, int max_iterations, double tol, unsigned seed) {
    size_t n = points.size();
    size_t d = n > 0 ? points[0].size() : 0;
    std::mt19937 rng(seed);

    GmmModel model;
    model.components.resize(k);

    // k-means++ seeding
    std::vector<size_t> chosen;
    std::uniform_int_distribution<size_t> first_pick(0, n - 1);
    chosen.push_back(first_pick(rng));
    while (chosen.size() < k) {
        Vec dist2(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            double best = 1e300;
            for (size_t c : chosen) {
                double s = 0.0;
                for (size_t j = 0; j < d; ++j) { double diff = points[i][j] - points[c][j]; s += diff * diff; }
                best = std::min(best, s);
            }
            dist2[i] = best;
        }
        std::discrete_distribution<size_t> pick(dist2.begin(), dist2.end());
        chosen.push_back(pick(rng));
    }
    for (size_t c = 0; c < k; ++c) {
        model.components[c].mean = points[chosen[c]];
        model.components[c].covariance = linalg::identity(d); // start spherical; EM reshapes it
        model.components[c].weight = 1.0 / static_cast<double>(k);
    }

    Mat resp(n, Vec(k, 0.0));
    double prev_ll = -1e300;

    for (int iter = 0; iter < max_iterations; ++iter) {
        // E-step
        double log_likelihood = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double total = 0.0;
            for (size_t c = 0; c < k; ++c) {
                resp[i][c] = model.components[c].weight * gaussian_density(points[i], model.components[c]);
                total += resp[i][c];
            }
            if (total < 1e-300) total = 1e-300;
            for (size_t c = 0; c < k; ++c) resp[i][c] /= total;
            log_likelihood += std::log(total);
        }

        // M-step
        for (size_t c = 0; c < k; ++c) {
            double nk = 0.0;
            for (size_t i = 0; i < n; ++i) nk += resp[i][c];
            if (nk < 1e-9) nk = 1e-9;

            Vec new_mean(d, 0.0);
            for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < d; ++j) new_mean[j] += resp[i][c] * points[i][j];
            for (double& v : new_mean) v /= nk;

            Mat new_cov = linalg::zeros(d, d);
            for (size_t i = 0; i < n; ++i) {
                Vec diff(d);
                for (size_t j = 0; j < d; ++j) diff[j] = points[i][j] - new_mean[j];
                for (size_t a = 0; a < d; ++a)
                    for (size_t b = 0; b < d; ++b)
                        new_cov[a][b] += resp[i][c] * diff[a] * diff[b];
            }
            for (size_t a = 0; a < d; ++a) for (size_t b = 0; b < d; ++b) new_cov[a][b] /= nk;
            for (size_t a = 0; a < d; ++a) new_cov[a][a] += 1e-6; // ridge: avoid a singular/degenerate covariance

            model.components[c].mean = new_mean;
            model.components[c].covariance = new_cov;
            model.components[c].weight = nk / static_cast<double>(n);
        }

        model.n_iterations_run = iter + 1;
        model.final_log_likelihood = log_likelihood;
        if (std::abs(log_likelihood - prev_ll) < tol) break;
        prev_ll = log_likelihood;
    }

    return model;
}

// Multi-restart EM: a single EM run lands in a seed-dependent local optimum
// (the §10.2/§10.4 validation suite measured this directly), so keep the
// highest-likelihood of n_init independent k-means++ starts.
inline GmmModel fit(const Mat& points, size_t k, int max_iterations = 200, double tol = 1e-6, unsigned seed = 42, int n_init = 10) {
    GmmModel best;
    bool have = false;
    for (int r = 0; r < n_init; ++r) {
        GmmModel m = fit_once(points, k, max_iterations, tol, seed + 7919u * static_cast<unsigned>(r));
        if (!have || m.final_log_likelihood > best.final_log_likelihood) { best = std::move(m); have = true; }
    }
    return best;
}

} // namespace vt::gmm
