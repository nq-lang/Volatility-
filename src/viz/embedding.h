#pragma once
// embedding.h -- DISPLAY-ONLY projections for the PCA regime view (visualization task spec).
//
// Nothing here is an input to the classifier. Regime membership is always computed in the
// original 17-feature space (regime_model.h); this file only produces coordinates for a picture
// of the same standardized matrix: exact t-SNE (2D and 3D), Procrustes alignment to the previous
// batch, kNN-barycentric placement of live points, estimated feature directions (regression on
// the embedding, with R^2), and fidelity metrics (trustworthiness, continuity, kNN regime purity).
//
// The model objects are only ever read through const references.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include "vt/linalg.h"

namespace vt::viz {

using linalg::Vec;
using linalg::Mat;

// ---------------- config (constants, not UI controls) ----------------
struct TsneConfig {
    double perplexity = 20.0;      // roughly 230 days/ticker: keep 15-30 and well below n/3
    int    iterations = 750;
    int    exaggeration_iters = 250;
    double exaggeration = 12.0;
    unsigned seed = 42;
    size_t dims = 2;               // 3 for the optional t-SNE 3D tab
    bool warm_start = false;       // init is a previous (aligned) layout: keep its scale, skip early exaggeration
};

struct TsneResult { Mat Y; double kl = 0.0; double perplexity_used = 0.0; };

inline Mat sq_dists(const Mat& X) {
    size_t n = X.size();
    Mat D(n, Vec(n, 0.0));
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            double s = 0; for (size_t k = 0; k < X[i].size(); ++k) { double d = X[i][k] - X[j][k]; s += d * d; }
            D[i][j] = D[j][i] = s;
        }
    return D;
}

// Exact t-SNE (O(n^2) per iteration; n is a few hundred trading days, so this is cheap enough to run
// inside the batch refit and never in the live loop). Deterministic for a fixed seed + init.
// `init` (optional): n x >=dims matrix, normally the PCA scores, rescaled to std 1e-4 (PCA initialization).
inline TsneResult tsne_fit(const Mat& X, const TsneConfig& cfg, const Mat* init = nullptr) {
    const size_t n = X.size(), d = cfg.dims;
    TsneResult out; out.Y.assign(n, Vec(d, 0.0));
    if (n < 5) return out;
    const double perp = std::min(cfg.perplexity, std::max(2.0, (static_cast<double>(n) - 1.0) / 3.0));
    out.perplexity_used = perp;
    Mat D = sq_dists(X);

    // conditional P by per-row binary search on beta to hit the target perplexity
    Mat P(n, Vec(n, 0.0));
    const double target = std::log(perp);
    for (size_t i = 0; i < n; ++i) {
        double lo = 0.0, hi = 1e300, beta = 1.0;
        for (int it = 0; it < 60; ++it) {
            double sum = 0, H = 0;
            for (size_t j = 0; j < n; ++j) if (j != i) { P[i][j] = std::exp(-D[i][j] * beta); sum += P[i][j]; }
            if (sum < 1e-300) { hi = beta; beta = (lo + hi) / 2; continue; }
            double dp = 0;
            for (size_t j = 0; j < n; ++j) if (j != i) { P[i][j] /= sum; dp += P[i][j] * D[i][j]; }
            H = std::log(sum) + beta * dp;
            double diff = H - target;
            if (std::fabs(diff) < 1e-5) break;
            if (diff > 0) { lo = beta; beta = hi > 1e299 ? beta * 2 : (beta + hi) / 2; }
            else          { hi = beta; beta = (beta + lo) / 2; }
        }
    }
    Mat Ps(n, Vec(n, 0.0));
    for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) Ps[i][j] = std::max((P[i][j] + P[j][i]) / (2.0 * n), 1e-12);
    for (size_t i = 0; i < n; ++i) Ps[i][i] = 0.0;

    // initialization
    Mat& Y = out.Y;
    std::mt19937 rng(cfg.seed);
    std::normal_distribution<double> g(0.0, 1.0);
    if (init && init->size() == n && (*init)[0].size() >= d && cfg.warm_start) {
        for (size_t i = 0; i < n; ++i) for (size_t k = 0; k < d; ++k) Y[i][k] = (*init)[i][k];
    } else if (init && init->size() == n && (*init)[0].size() >= d) {
        for (size_t i = 0; i < n; ++i) for (size_t k = 0; k < d; ++k) Y[i][k] = (*init)[i][k];
        double sd = 0, m = 0; for (size_t i = 0; i < n; ++i) m += Y[i][0]; m /= n;
        for (size_t i = 0; i < n; ++i) sd += (Y[i][0] - m) * (Y[i][0] - m);
        sd = std::sqrt(sd / n); if (sd < 1e-12) sd = 1.0;
        for (auto& r : Y) for (double& v : r) v = v / sd * 1e-4;
    } else {
        for (auto& r : Y) for (double& v : r) v = g(rng) * 1e-4;
    }

    const double lr = std::max(static_cast<double>(n) / cfg.exaggeration / 4.0, 50.0);
    Mat vel(n, Vec(d, 0.0)), gain(n, Vec(d, 1.0)), grad(n, Vec(d, 0.0));
    Mat num(n, Vec(n, 0.0));
    double kl = 0.0;
    for (int it = 0; it < cfg.iterations; ++it) {
        const int ex_iters = cfg.warm_start ? 0 : cfg.exaggeration_iters;
        const double ex = it < ex_iters ? cfg.exaggeration : 1.0;
        const double mom = it < ex_iters ? 0.5 : 0.8;
        double Z = 0;
        for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) {
            double s = 0; for (size_t k = 0; k < d; ++k) { double df = Y[i][k] - Y[j][k]; s += df * df; }
            num[i][j] = num[j][i] = 1.0 / (1.0 + s); Z += 2.0 * num[i][j];
        }
        Z = std::max(Z, 1e-300);
        for (size_t i = 0; i < n; ++i) {
            std::fill(grad[i].begin(), grad[i].end(), 0.0);
            for (size_t j = 0; j < n; ++j) if (j != i) {
                double mult = 4.0 * (ex * Ps[i][j] - num[i][j] / Z) * num[i][j];
                for (size_t k = 0; k < d; ++k) grad[i][k] += mult * (Y[i][k] - Y[j][k]);
            }
        }
        for (size_t i = 0; i < n; ++i) for (size_t k = 0; k < d; ++k) {
            bool same = (grad[i][k] > 0) == (vel[i][k] > 0);
            gain[i][k] = same ? gain[i][k] * 0.8 : gain[i][k] + 0.2;
            gain[i][k] = std::max(gain[i][k], 0.01);
            vel[i][k] = mom * vel[i][k] - lr * gain[i][k] * grad[i][k];
            Y[i][k] += vel[i][k];
        }
        for (size_t k = 0; k < d; ++k) { double m = 0; for (size_t i = 0; i < n; ++i) m += Y[i][k]; m /= n; for (size_t i = 0; i < n; ++i) Y[i][k] -= m; }
        if (it == cfg.iterations - 1) {
            kl = 0;
            for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) if (i != j && Ps[i][j] > 0) {
                double q = std::max(num[i][j] / Z, 1e-12); kl += Ps[i][j] * std::log(Ps[i][j] / q);
            }
        }
    }
    out.kl = kl;
    return out;
}

// ---------------- neighbours / fidelity metrics ----------------
inline std::vector<std::vector<size_t>> knn_sets(const Mat& D, size_t k) { // D = squared distances
    size_t n = D.size(); std::vector<std::vector<size_t>> out(n);
    for (size_t i = 0; i < n; ++i) {
        std::vector<size_t> idx; for (size_t j = 0; j < n; ++j) if (j != i) idx.push_back(j);
        std::partial_sort(idx.begin(), idx.begin() + std::min(k, idx.size()), idx.end(),
                          [&](size_t a, size_t b) { return D[i][a] < D[i][b] || (D[i][a] == D[i][b] && a < b); });
        idx.resize(std::min(k, idx.size())); out[i] = idx;
    }
    return out;
}
// rank[i][j] = position of j in i's distance-sorted neighbour list (1 = nearest)
inline std::vector<std::vector<int>> rank_matrix(const Mat& D) {
    size_t n = D.size(); std::vector<std::vector<int>> R(n, std::vector<int>(n, 0));
    for (size_t i = 0; i < n; ++i) {
        std::vector<size_t> idx; for (size_t j = 0; j < n; ++j) if (j != i) idx.push_back(j);
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return D[i][a] < D[i][b] || (D[i][a] == D[i][b] && a < b); });
        for (size_t r = 0; r < idx.size(); ++r) R[i][idx[r]] = static_cast<int>(r + 1);
    }
    return R;
}
// trustworthiness(X -> Y) and continuity (= trustworthiness with roles swapped), neighbourhood size k.
inline double trustworthiness(const Mat& Dx, const Mat& Dy, size_t k) {
    size_t n = Dx.size(); if (n <= 2 * k + 1) return 0.0;
    auto Rx = rank_matrix(Dx); auto Ny = knn_sets(Dy, k);
    double s = 0;
    for (size_t i = 0; i < n; ++i) for (size_t j : Ny[i]) if (Rx[i][j] > static_cast<int>(k)) s += Rx[i][j] - static_cast<int>(k);
    double nn = static_cast<double>(n), kk = static_cast<double>(k);
    return 1.0 - 2.0 / (nn * kk * (2 * nn - 3 * kk - 1)) * s;
}
inline double continuity(const Mat& Dx, const Mat& Dy, size_t k) { return trustworthiness(Dy, Dx, k); }
inline double knn_regime_purity(const Mat& Dy, const std::vector<int>& label, size_t k) {
    auto N = knn_sets(Dy, k); double same = 0, tot = 0;
    for (size_t i = 0; i < N.size(); ++i) for (size_t j : N[i]) { same += label[i] == label[j]; ++tot; }
    return tot ? same / tot : 0.0;
}

// ---------------- Procrustes alignment (display only) ----------------
// Aligns Y_new to Y_prev using rows present in both (pairs[i] = {row in prev, row in new}).
// Orthogonal (rotation/reflection) + uniform scale + translation, so a flipped or rotated refit
// does not look like the regimes moved. Returns Y_new unchanged if there are too few common rows.
inline Mat procrustes_align(const Mat& Y_prev, const Mat& Y_new, const std::vector<std::pair<size_t, size_t>>& pairs) {
    if (pairs.size() < 5 || Y_new.empty()) return Y_new;
    const size_t d = Y_new[0].size();
    Vec ma(d, 0.0), mb(d, 0.0);
    for (auto& [a, b] : pairs) for (size_t k = 0; k < d; ++k) { ma[k] += Y_prev[a][k]; mb[k] += Y_new[b][k]; }
    for (size_t k = 0; k < d; ++k) { ma[k] /= pairs.size(); mb[k] /= pairs.size(); }
    Mat M(d, Vec(d, 0.0)); double nb = 0;                      // M = B^T A
    for (auto& [a, b] : pairs) for (size_t i = 0; i < d; ++i) {
        double bi = Y_new[b][i] - mb[i]; nb += bi * bi;
        for (size_t j = 0; j < d; ++j) M[i][j] += bi * (Y_prev[a][j] - ma[j]);
    }
    Mat S(d, Vec(d, 0.0));                                      // S = M^T M
    for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) for (size_t k = 0; k < d; ++k) S[i][j] += M[k][i] * M[k][j];
    auto eg = linalg::jacobi_eigen(S);
    Mat Sinv(d, Vec(d, 0.0));                                   // S^{-1/2}
    for (size_t c = 0; c < d; ++c) {
        double lam = std::max(eg.values[c], 1e-18), w = 1.0 / std::sqrt(lam);
        for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) Sinv[i][j] += w * eg.vectors[i][c] * eg.vectors[j][c];
    }
    Mat R(d, Vec(d, 0.0));                                      // R = M S^{-1/2}  (orthogonal polar factor)
    for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) for (size_t k = 0; k < d; ++k) R[i][j] += M[i][k] * Sinv[k][j];
    double tr = 0; for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) tr += R[i][j] * M[i][j];
    double scale = nb > 1e-18 ? std::max(tr, 0.0) / nb : 1.0; if (scale < 1e-12) scale = 1.0;
    Mat out = Y_new;
    for (auto& row : out) {
        Vec c(d); for (size_t k = 0; k < d; ++k) c[k] = row[k] - mb[k];
        for (size_t j = 0; j < d; ++j) { double s = 0; for (size_t i = 0; i < d; ++i) s += c[i] * R[i][j]; row[j] = s * scale + ma[j]; }
    }
    return out;
}

// ---------------- estimated feature directions on an embedding ----------------
struct FeatureGradient { Vec dir; double r2 = 0.0; };   // dir = unit vector along which the feature INCREASES across the map
inline std::vector<FeatureGradient> feature_gradients(const Mat& Z, const Mat& Y) {
    size_t n = Z.size(), F = n ? Z[0].size() : 0, d = n ? Y[0].size() : 0;
    std::vector<FeatureGradient> out(F);
    Mat A(d + 1, Vec(d + 1, 0.0));                              // normal matrix of [1, Y]
    for (size_t i = 0; i < n; ++i) { Vec r(d + 1, 1.0); for (size_t k = 0; k < d; ++k) r[k + 1] = Y[i][k];
        for (size_t a = 0; a <= d; ++a) for (size_t b = 0; b <= d; ++b) A[a][b] += r[a] * r[b]; }
    auto inv = linalg::inverse_and_det(A);
    for (size_t f = 0; f < F; ++f) {
        out[f].dir.assign(d, 0.0);
        if (!inv.ok) continue;
        Vec rhs(d + 1, 0.0); double m = 0;
        for (size_t i = 0; i < n; ++i) { m += Z[i][f]; rhs[0] += Z[i][f]; for (size_t k = 0; k < d; ++k) rhs[k + 1] += Y[i][k] * Z[i][f]; }
        m /= n;
        Vec beta = linalg::mat_vec(inv.inverse, rhs);
        double ssr = 0, sst = 0;
        for (size_t i = 0; i < n; ++i) { double p = beta[0]; for (size_t k = 0; k < d; ++k) p += beta[k + 1] * Y[i][k];
            ssr += (Z[i][f] - p) * (Z[i][f] - p); sst += (Z[i][f] - m) * (Z[i][f] - m); }
        out[f].r2 = sst > 1e-12 ? std::max(0.0, 1.0 - ssr / sst) : 0.0;
        double nrm = 0; for (size_t k = 0; k < d; ++k) nrm += beta[k + 1] * beta[k + 1]; nrm = std::sqrt(nrm);
        for (size_t k = 0; k < d; ++k) out[f].dir[k] = nrm > 1e-15 ? beta[k + 1] / nrm : 0.0;
    }
    return out;
}

// ---------------- deterministic out-of-sample placement ----------------
struct Placement { Vec coords; double mean_knn_dist = 0.0; bool out_of_distribution = false; };
inline double mean_knn_dist_to(const Mat& Xref, const Vec& x, size_t k, long skip = -1) {
    Vec d; for (size_t i = 0; i < Xref.size(); ++i) { if (static_cast<long>(i) == skip) continue;
        double s = 0; for (size_t c = 0; c < x.size(); ++c) { double df = Xref[i][c] - x[c]; s += df * df; } d.push_back(std::sqrt(s)); }
    std::sort(d.begin(), d.end()); k = std::min(k, d.size());
    double m = 0; for (size_t i = 0; i < k; ++i) m += d[i]; return k ? m / k : 0.0;
}
// inverse-distance weighted mean of the k nearest HISTORICAL days (17D standardized space), placed
// on the cached embedding. Never alters the embedding. `ood_threshold` = p95 of leave-one-out kNN distance.
inline Placement knn_place(const Mat& Xref, const Mat& Yref, const Vec& x, size_t k, double ood_threshold) {
    Placement p; size_t n = Xref.size(), d = n ? Yref[0].size() : 0; p.coords.assign(d, 0.0);
    if (!n) return p;
    std::vector<std::pair<double, size_t>> dist;
    for (size_t i = 0; i < n; ++i) { double s = 0; for (size_t c = 0; c < x.size(); ++c) { double df = Xref[i][c] - x[c]; s += df * df; } dist.push_back({std::sqrt(s), i}); }
    k = std::min(k, n); std::partial_sort(dist.begin(), dist.begin() + k, dist.end());
    double wsum = 0, md = 0;
    for (size_t r = 0; r < k; ++r) { double w = 1.0 / (dist[r].first + 1e-9); wsum += w; md += dist[r].first;
        for (size_t c = 0; c < d; ++c) p.coords[c] += w * Yref[dist[r].second][c]; }
    for (double& v : p.coords) v /= wsum;
    p.mean_knn_dist = md / k; p.out_of_distribution = ood_threshold > 0 && p.mean_knn_dist > ood_threshold;
    return p;
}

} // namespace vt::viz
