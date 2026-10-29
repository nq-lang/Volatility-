#pragma once
// vt/linalg.h — minimal, dependency-free linear algebra sufficient for PCA
// (covariance + symmetric eigendecomposition) and a small-dimension GMM
// (matrix inverse/determinant). Per the PCA build spec's instruction to use
// "whatever numerical/statistics approach is idiomatic to the existing
// codebase... rather than introducing a new dependency stack" — this project
// has no existing linalg library, so this file IS that minimal addition,
// written for clarity/testability over raw performance (matrices here are at
// most ~16x16 feature covariance or ~3x3 GMM component covariance; a Jacobi
// eigensolver and Gauss-Jordan inverse are more than adequate at this scale).
#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace vt::linalg {

using Vec = std::vector<double>;
using Mat = std::vector<std::vector<double>>; // row-major, Mat[row][col]

inline Mat zeros(size_t n, size_t m) { return Mat(n, Vec(m, 0.0)); }
inline Mat identity(size_t n) { auto I = zeros(n, n); for (size_t i = 0; i < n; ++i) I[i][i] = 1.0; return I; }

inline Vec mat_vec(const Mat& A, const Vec& x) {
    Vec out(A.size(), 0.0);
    for (size_t i = 0; i < A.size(); ++i)
        for (size_t j = 0; j < x.size(); ++j)
            out[i] += A[i][j] * x[j];
    return out;
}

inline double dot(const Vec& a, const Vec& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

// Sample mean and (population, N-normalized isn't required at this scale;
// use N-1 for an unbiased sample covariance since feature history is a sample
// of trading days, not the full population) covariance matrix of a set of
// row-vectors (each row = one trading day's raw feature vector).
inline Vec column_means(const Mat& rows) {
    if (rows.empty()) return {};
    size_t d = rows[0].size();
    Vec mean(d, 0.0);
    for (const auto& r : rows) for (size_t j = 0; j < d; ++j) mean[j] += r[j];
    for (double& m : mean) m /= static_cast<double>(rows.size());
    return mean;
}

inline Vec column_stdevs(const Mat& rows, const Vec& means) {
    size_t d = means.size();
    Vec var(d, 0.0);
    for (const auto& r : rows) for (size_t j = 0; j < d; ++j) { double diff = r[j] - means[j]; var[j] += diff * diff; }
    size_t n = rows.size() > 1 ? rows.size() - 1 : 1;
    for (double& v : var) v = std::sqrt(v / static_cast<double>(n));
    return var;
}

inline Mat covariance(const Mat& rows, const Vec& means) {
    size_t d = means.size();
    Mat cov = zeros(d, d);
    for (const auto& r : rows) {
        for (size_t i = 0; i < d; ++i) {
            double di = r[i] - means[i];
            for (size_t j = 0; j < d; ++j) {
                cov[i][j] += di * (r[j] - means[j]);
            }
        }
    }
    size_t n = rows.size() > 1 ? rows.size() - 1 : 1;
    for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) cov[i][j] /= static_cast<double>(n);
    return cov;
}

// Jacobi eigenvalue algorithm for a real SYMMETRIC matrix. Returns eigenvalues
// (unsorted) and eigenvectors as COLUMNS of the returned matrix. Adequate and
// numerically robust for the small (<=~20x20) covariance matrices this model uses.
struct EigenResult { Vec values; Mat vectors; };

inline EigenResult jacobi_eigen(Mat A, int max_sweeps = 100, double tol = 1e-10) {
    size_t n = A.size();
    Mat V = identity(n);

    for (int sweep = 0; sweep < max_sweeps; ++sweep) {
        double off = 0.0;
        for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) off += A[i][j] * A[i][j];
        if (off < tol) break;

        for (size_t p = 0; p < n; ++p) {
            for (size_t q = p + 1; q < n; ++q) {
                if (std::abs(A[p][q]) < 1e-300) continue;
                double theta = (A[q][q] - A[p][p]) / (2.0 * A[p][q]);
                double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                double c = 1.0 / std::sqrt(t * t + 1.0);
                double s = t * c;

                double app = A[p][p], aqq = A[q][q], apq = A[p][q];
                A[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
                A[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
                A[p][q] = 0.0; A[q][p] = 0.0;
                for (size_t k = 0; k < n; ++k) {
                    if (k == p || k == q) continue;
                    double akp = A[k][p], akq = A[k][q];
                    A[k][p] = c * akp - s * akq; A[p][k] = A[k][p];
                    A[k][q] = s * akp + c * akq; A[q][k] = A[k][q];
                }
                for (size_t k = 0; k < n; ++k) {
                    double vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq;
                    V[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }

    EigenResult res;
    res.values.resize(n);
    for (size_t i = 0; i < n; ++i) res.values[i] = A[i][i];
    res.vectors = V;
    return res;
}

// Sorts eigen-pairs by eigenvalue descending, keeps the top `k` (for PCA's
// "reduce to 3 components"). Returns eigenvalues (desc) and eigenvectors as
// columns of a d x k matrix.
inline EigenResult top_k_eigen(const EigenResult& full, size_t k) {
    size_t n = full.values.size();
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return full.values[a] > full.values[b]; });
    k = std::min(k, n);
    EigenResult out;
    out.values.resize(k);
    out.vectors = zeros(n, k);
    for (size_t c = 0; c < k; ++c) {
        out.values[c] = full.values[idx[c]];
        for (size_t r = 0; r < n; ++r) out.vectors[r][c] = full.vectors[r][idx[c]];
    }
    return out;
}

// Small-matrix inverse + determinant via Gauss-Jordan, used by the GMM's
// Gaussian density evaluation (component covariances are only as large as the
// space the GMM is fit in -- 3x3 if fit on PCA space, as this model does by default).
struct InverseResult { Mat inverse; double det; bool ok; };

inline InverseResult inverse_and_det(Mat A) {
    size_t n = A.size();
    Mat inv = identity(n);
    double det = 1.0;
    for (size_t col = 0; col < n; ++col) {
        size_t pivot = col;
        for (size_t r = col + 1; r < n; ++r) if (std::abs(A[r][col]) > std::abs(A[pivot][col])) pivot = r;
        if (std::abs(A[pivot][col]) < 1e-12) return { inv, 0.0, false };
        if (pivot != col) { std::swap(A[pivot], A[col]); std::swap(inv[pivot], inv[col]); det = -det; }
        double pv = A[col][col];
        det *= pv;
        for (size_t j = 0; j < n; ++j) { A[col][j] /= pv; inv[col][j] /= pv; }
        for (size_t r = 0; r < n; ++r) {
            if (r == col) continue;
            double factor = A[r][col];
            if (factor == 0.0) continue;
            for (size_t j = 0; j < n; ++j) { A[r][j] -= factor * A[col][j]; inv[r][j] -= factor * inv[col][j]; }
        }
    }
    return { inv, det, true };
}

} // namespace vt::linalg
