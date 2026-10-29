#pragma once
// vt/pca.h — Layer A of the two-layer regime model: dimensionality reduction
// and visualization ONLY (per §2 of the PCA build spec — do not conflate this
// with the classifier, which lives in gmm.h/regime_model.h).
//
// Batch layer (fit): run on a rolling trailing window (§11.3, recommend 6-12
// months), standardizes every input feature (§3, a hard requirement — do not
// skip), computes the covariance matrix, and eigendecomposes it to get the
// top-3 principal components + explained variance.
//
// Live layer (project): a single fixed linear transform against the
// already-fit loading matrix. No fitting happens here — this is what keeps
// the intraday path cheap and safe for a live terminal (§9).
#include "vt/linalg.h"
#include <string>
#include <vector>

namespace vt::pca {

using linalg::Vec;
using linalg::Mat;

struct StandardizationParams {
    Vec means;   // per-feature trailing mean, used to center new observations
    Vec stdevs;  // per-feature trailing stdev, used to scale new observations
};

inline Vec standardize(const Vec& raw, const StandardizationParams& p) {
    Vec out(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        double sd = p.stdevs[i] > 1e-9 ? p.stdevs[i] : 1e-9; // guard degenerate/constant features
        out[i] = (raw[i] - p.means[i]) / sd;
    }
    return out;
}

struct PcaModel {
    StandardizationParams standardization;
    Mat loadings;              // d x 3 matrix (columns are PC1/PC2/PC3), fit on standardized features
    Vec eigenvalues;           // top-3 eigenvalues (variance explained by each PC, in standardized units)
    double total_variance = 0; // sum of ALL eigenvalues (trace of the covariance matrix) -- denominator for %
    std::vector<std::string> feature_names;
    int n_days_in_fit = 0;

    // Explained variance fractions -- the "required transparency element" the
    // spec calls out (§8/§11.6): flag a low cumulative capture explicitly
    // rather than presenting it with the same visual confidence either way.
    Vec explained_variance_fraction() const {
        Vec f(eigenvalues.size());
        for (size_t i = 0; i < eigenvalues.size(); ++i)
            f[i] = total_variance > 1e-12 ? eigenvalues[i] / total_variance : 0.0;
        return f;
    }
    double cumulative_explained_variance() const {
        double s = 0.0; for (double e : eigenvalues) s += e;
        return total_variance > 1e-12 ? s / total_variance : 0.0;
    }

    // Live layer: cheap linear projection of one new, ALREADY-STANDARDIZED
    // feature vector (standardize() first) into the fixed 3D PCA space.
    Vec project(const Vec& standardized_features) const {
        size_t k = loadings.empty() ? 0 : loadings[0].size();
        Vec out(k, 0.0);
        for (size_t c = 0; c < k; ++c)
            for (size_t r = 0; r < standardized_features.size(); ++r)
                out[c] += standardized_features[r] * loadings[r][c];
        return out;
    }

    // Convenience: standardize raw features against this model's fit params, then project.
    Vec standardize_and_project(const Vec& raw_features) const {
        return project(standardize(raw_features, standardization));
    }
};

// Batch/offline fit (§9): call on a scheduled cadence (nightly, or hourly
// intraday per the spec), never on every tick.
inline PcaModel fit(const Mat& raw_feature_rows, const std::vector<std::string>& feature_names) {
    PcaModel model;
    model.feature_names = feature_names;
    model.n_days_in_fit = static_cast<int>(raw_feature_rows.size());

    model.standardization.means = linalg::column_means(raw_feature_rows);
    model.standardization.stdevs = linalg::column_stdevs(raw_feature_rows, model.standardization.means);

    Mat standardized_rows;
    standardized_rows.reserve(raw_feature_rows.size());
    for (const auto& row : raw_feature_rows) standardized_rows.push_back(standardize(row, model.standardization));

    Vec std_means = linalg::column_means(standardized_rows); // ~0 by construction, kept for covariance()'s API
    Mat cov = linalg::covariance(standardized_rows, std_means);

    auto full_eigen = linalg::jacobi_eigen(cov);
    auto top3 = linalg::top_k_eigen(full_eigen, 3);

    model.loadings = top3.vectors;
    model.eigenvalues = top3.values;
    for (double v : full_eigen.values) model.total_variance += v;

    return model;
}

} // namespace vt::pca
