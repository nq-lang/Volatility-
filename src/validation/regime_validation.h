#pragma once
// regime_validation.h -- PCA spec §10 validation & backtesting suite. Mandatory
// before any live-decision use of the regime model's output.
//
//   §10.1 check_regime_separation    out-of-sample forward-range separation
//   §10.2 check_refit_stability      rolling-refit loading / centroid drift
//   §10.3 calibrate_soft_trigger     soft-warning percentile sweep (FP vs TP)
//   §10.4 check_label_mapping        mapping re-derived each refit, bijective, seed-stable
//   §10.5 check_event_day_effect     fit with vs without flagged macro-event days
//
// Pure functions over plain history rows. No adapters, no I/O: the real
// RegimeHistoryAdapter feeds DayRecord rows, the tests feed synthetic ones.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>
#include "vt/linalg.h"
#include "../core/regime_model.h"

namespace vt::validation {

using linalg::Vec;
using linalg::Mat;
using regime::RegimeName;
using regime::RegimeModel;

struct DayRecord {
    Vec features;           // raw feature row, columns per regime::FeatureIndex
    double fwd_range = 0.0; // SUBSEQUENT realized range (high-low)/open, the outcome the regime must predict
    bool event_day = false; // known macro catalyst (FOMC/CPI/NFP/...)
};

struct CheckResult {
    std::string name;
    bool pass = false;
    std::string detail; // human-readable numbers behind the verdict
};

// ---------------- statistics helpers ----------------
namespace stats {
inline double median(Vec v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
inline double mean(const Vec& v) { return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }

// Regularized upper incomplete gamma Q(a,x) (Numerical Recipes series / cont. fraction).
inline double gamma_q(double a, double x) {
    if (x <= 0) return 1.0;
    double gln = std::lgamma(a);
    if (x < a + 1.0) {
        double ap = a, sum = 1.0 / a, del = sum;
        for (int n = 0; n < 500; ++n) { ap += 1; del *= x / ap; sum += del; if (std::fabs(del) < std::fabs(sum) * 1e-14) break; }
        return 1.0 - sum * std::exp(-x + a * std::log(x) - gln);
    }
    double b = x + 1.0 - a, c = 1.0 / 1e-300, d = 1.0 / b, h = d;
    for (int i = 1; i < 500; ++i) {
        double an = -i * (i - a); b += 2.0;
        d = an * d + b; if (std::fabs(d) < 1e-300) d = 1e-300;
        c = b + an / c; if (std::fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d; double del = d * c; h *= del;
        if (std::fabs(del - 1.0) < 1e-14) break;
    }
    return std::exp(-x + a * std::log(x) - gln) * h;
}
inline double chi2_sf(double x, double df) { return gamma_q(df / 2.0, x / 2.0); }

// Average ranks (ties share the mean rank).
inline Vec ranks(const Vec& v) {
    std::vector<size_t> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
    Vec r(v.size());
    for (size_t i = 0; i < idx.size();) {
        size_t j = i;
        while (j + 1 < idx.size() && v[idx[j + 1]] == v[idx[i]]) ++j;
        for (size_t k = i; k <= j; ++k) r[idx[k]] = 0.5 * (i + j) + 1.0;
        i = j + 1;
    }
    return r;
}

// Kruskal-Wallis H across groups; returns p-value (chi-square, df = groups-1).
inline double kruskal_wallis_p(const std::vector<Vec>& groups) {
    Vec all; std::vector<size_t> gid;
    for (size_t g = 0; g < groups.size(); ++g) for (double v : groups[g]) { all.push_back(v); gid.push_back(g); }
    if (groups.size() < 2 || all.size() < 3) return 1.0;
    Vec r = ranks(all);
    std::vector<double> rs(groups.size(), 0.0);
    for (size_t i = 0; i < all.size(); ++i) rs[gid[i]] += r[i];
    double n = all.size(), h = 0.0;
    for (size_t g = 0; g < groups.size(); ++g) if (!groups[g].empty()) h += rs[g] * rs[g] / groups[g].size();
    h = 12.0 / (n * (n + 1.0)) * h - 3.0 * (n + 1.0);
    return chi2_sf(std::max(h, 0.0), static_cast<double>(groups.size() - 1));
}

// One-sided Mann-Whitney: p that `a` is stochastically SMALLER than `b` (normal approx).
inline double mann_whitney_less_p(const Vec& a, const Vec& b) {
    if (a.empty() || b.empty()) return 1.0;
    Vec all = a; all.insert(all.end(), b.begin(), b.end());
    Vec r = ranks(all);
    double ra = 0; for (size_t i = 0; i < a.size(); ++i) ra += r[i];
    double na = a.size(), nb = b.size();
    double u = ra - na * (na + 1) / 2.0;
    double mu = na * nb / 2.0, sd = std::sqrt(na * nb * (na + nb + 1) / 12.0);
    double z = (u - mu) / sd;
    return 0.5 * std::erfc(-z / std::sqrt(2.0)); // P(Z <= z)
}
} // namespace stats

inline RegimeName classify(const RegimeModel& m, const Vec& row) {
    return regime::argmax_regime(regime::evaluate_live(m, row).regime_probability);
}

inline double abs_cosine(const Vec& a, const Vec& b) {
    double d = 0, na = 0, nb = 0;
    for (size_t i = 0; i < a.size(); ++i) { d += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
    return (na > 0 && nb > 0) ? std::fabs(d) / std::sqrt(na * nb) : 0.0;
}
inline Vec loading_column(const RegimeModel& m, size_t pc) {
    Vec c(m.pca_model.loadings.size());
    for (size_t i = 0; i < c.size(); ++i) c[i] = m.pca_model.loadings[i][pc];
    return c;
}
inline Mat rows_of(const std::vector<DayRecord>& d, size_t from, size_t to, bool skip_events = false) {
    Mat m;
    for (size_t i = from; i < to && i < d.size(); ++i) if (!(skip_events && d[i].event_day)) m.push_back(d[i].features);
    return m;
}
template <class... A> inline std::string fmt(const char* f, A... a) {
    char buf[512]; std::snprintf(buf, sizeof buf, f, a...); return buf;
}

// ============ §10.1 Out-of-sample regime separation ============
struct SeparationConfig { double train_fraction = 0.6; double alpha = 0.01; size_t min_group = 8; };

inline CheckResult check_regime_separation(const std::vector<DayRecord>& days, const SeparationConfig& cfg = {}) {
    CheckResult res{"10.1 out-of-sample regime separation", false, ""};
    size_t split = static_cast<size_t>(days.size() * cfg.train_fraction);
    if (split < 60 || days.size() - split < 40) { res.detail = "insufficient history for a train/holdout split"; return res; }
    RegimeModel m = regime::fit_batch(rows_of(days, 0, split));
    std::vector<Vec> grp(5);
    for (size_t i = split; i < days.size(); ++i) grp[static_cast<size_t>(classify(m, days[i].features))].push_back(days[i].fwd_range);
    std::string d;
    std::vector<Vec> usable;
    for (size_t r = 0; r < 5; ++r) {
        d += fmt("%s n=%zu med=%.4f; ", regime::to_string(static_cast<RegimeName>(r)), grp[r].size(), stats::median(grp[r]));
        if (grp[r].size() >= cfg.min_group) usable.push_back(grp[r]);
    }
    double kw = stats::kruskal_wallis_p(usable);
    const Vec& pin = grp[static_cast<size_t>(RegimeName::PinningLowVol)];
    const Vec& trend = grp[static_cast<size_t>(RegimeName::TrendingDirectional)];
    bool have_pair = pin.size() >= cfg.min_group && trend.size() >= cfg.min_group;
    double mw = have_pair ? stats::mann_whitney_less_p(pin, trend) : 1.0;
    d += fmt("| Kruskal-Wallis p=%.2e | Pinning<Trending Mann-Whitney p=%.2e", kw, mw);
    res.pass = usable.size() >= 2 && kw < cfg.alpha && have_pair && mw < cfg.alpha;
    res.detail = d;
    return res;
}

// ============ §10.2 Rolling refit stability ============
struct StabilityConfig { size_t window = 120; size_t step = 10; double min_loading_cos = 0.90; double min_assign_agreement = 0.80; };
struct StabilityReport { size_t refits = 0; std::array<double, 3> min_cos{1, 1, 1}; double min_agreement = 1.0; double max_centroid_shift = 0.0; };

inline CheckResult check_refit_stability(const std::vector<DayRecord>& days, const StabilityConfig& cfg = {}, StabilityReport* out = nullptr) {
    CheckResult res{"10.2 rolling refit stability", false, ""};
    if (days.size() < cfg.window + 2 * cfg.step) { res.detail = "history shorter than window + 2 steps"; return res; }
    StabilityReport rep;
    std::optional<RegimeModel> prev; size_t prev_end = 0;
    for (size_t end = cfg.window; end <= days.size(); end += cfg.step) {
        RegimeModel m = regime::fit_batch(rows_of(days, end - cfg.window, end));
        if (prev) {
            ++rep.refits;
            for (size_t pc = 0; pc < 3; ++pc)
                rep.min_cos[pc] = std::min(rep.min_cos[pc], abs_cosine(loading_column(*prev, pc), loading_column(m, pc)));
            // label agreement on the days both fits saw, in regime-NAME space
            size_t same = 0, tot = 0;
            for (size_t i = end - cfg.window; i < prev_end; ++i) { ++tot; same += classify(*prev, days[i].features) == classify(m, days[i].features); }
            if (tot) rep.min_agreement = std::min(rep.min_agreement, static_cast<double>(same) / tot);
            // regime-labelled centroid drift, in raw-feature space (fit-independent coordinates)
            for (size_t r = 0; r < 5; ++r) {
                Vec ca(days[0].features.size(), 0.0), cb = ca; size_t na = 0, nb = 0;
                for (size_t i = end - cfg.window; i < prev_end; ++i) {
                    auto ra = classify(*prev, days[i].features), rb = classify(m, days[i].features);
                    if (static_cast<size_t>(ra) == r) { for (size_t k = 0; k < ca.size(); ++k) ca[k] += days[i].features[k]; ++na; }
                    if (static_cast<size_t>(rb) == r) { for (size_t k = 0; k < cb.size(); ++k) cb[k] += days[i].features[k]; ++nb; }
                }
                if (na < 3 || nb < 3) continue;
                double s = 0; for (size_t k = 0; k < ca.size(); ++k) { double x = ca[k] / na - cb[k] / nb; s += x * x; }
                rep.max_centroid_shift = std::max(rep.max_centroid_shift, std::sqrt(s));
            }
        }
        prev = std::move(m); prev_end = end;
    }
    if (out) *out = rep;
    res.detail = fmt("%zu refits | min |cos| PC1=%.3f PC2=%.3f PC3=%.3f | min day-label agreement=%.3f | max regime-centroid shift=%.3f",
                     rep.refits, rep.min_cos[0], rep.min_cos[1], rep.min_cos[2], rep.min_agreement, rep.max_centroid_shift);
    // PC3 is the least determined axis; only PC1/PC2 gate the verdict, PC3 is reported.
    res.pass = rep.refits > 0 && rep.min_cos[0] >= cfg.min_loading_cos && rep.min_cos[1] >= cfg.min_loading_cos &&
               rep.min_agreement >= cfg.min_assign_agreement;
    return res;
}

// ============ §10.3 Soft-trigger calibration ============
struct IntradayBar { double speed = 0.0; double flip_pct = 0.0; };
struct TriggerPoint { double percentile; size_t warnings, true_pos, hard_events, caught; double precision, recall, fp_rate, lift; };
struct TriggerCalibration { std::vector<TriggerPoint> sweep; double recommended = 0.0; };

// A soft warning is a rising edge of "speed percentile >= p" within its own trailing window.
// It is a true positive if a hard trigger (flip_pct sign change) occurs within `horizon` bars.
inline TriggerCalibration calibrate_soft_trigger(const std::vector<IntradayBar>& bars, size_t trailing = 200, size_t horizon = 12,
                                                 const std::vector<double>& percentiles = {0.60, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95}) {
    TriggerCalibration cal;
    std::vector<size_t> hard;
    for (size_t i = 1; i < bars.size(); ++i)
        if ((bars[i - 1].flip_pct > 0 && bars[i].flip_pct <= 0) || (bars[i - 1].flip_pct < 0 && bars[i].flip_pct >= 0)) hard.push_back(i);
    std::vector<double> rank(bars.size(), -1.0);
    for (size_t i = trailing; i < bars.size(); ++i) {
        size_t below = 0;
        for (size_t j = i - trailing; j < i; ++j) below += bars[j].speed <= bars[i].speed;
        rank[i] = static_cast<double>(below) / trailing;
    }
    double best = -1.0;
    for (double p : percentiles) {
        TriggerPoint t{p, 0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<size_t> warns;
        for (size_t i = trailing + 1; i < bars.size(); ++i) if (rank[i] >= p && rank[i - 1] < p) warns.push_back(i);
        t.warnings = warns.size();
        for (size_t w : warns) for (size_t h : hard) if (h > w && h <= w + horizon) { ++t.true_pos; break; }
        for (size_t h : hard) if (h > trailing) {
            ++t.hard_events;
            for (size_t w : warns) if (w < h && h <= w + horizon) { ++t.caught; break; }
        }
        t.precision = t.warnings ? static_cast<double>(t.true_pos) / t.warnings : 0.0;
        t.recall = t.hard_events ? static_cast<double>(t.caught) / t.hard_events : 0.0;
        t.fp_rate = t.warnings ? 1.0 - t.precision : 0.0;
        // chance level: probability a random bar sits within `horizon` bars before some hard trigger
        double base = std::min(1.0, static_cast<double>(hard.size() * horizon) / std::max<size_t>(1, bars.size() - trailing));
        t.lift = base > 0 ? t.precision / base : 0.0;
        cal.sweep.push_back(t);
        double f1 = (t.precision + t.recall) > 0 ? 2 * t.precision * t.recall / (t.precision + t.recall) : 0.0;
        if (f1 > best) { best = f1; cal.recommended = p; }
    }
    return cal;
}
inline CheckResult check_trigger_calibration(const std::vector<IntradayBar>& bars, double chosen_percentile = 0.80) {
    CheckResult res{"10.3 soft-trigger calibration", false, ""};
    TriggerCalibration cal = calibrate_soft_trigger(bars);
    for (auto& t : cal.sweep) {
        res.detail += fmt("p%.2f: warn=%zu prec=%.2f recall=%.2f fp=%.2f lift=%.1fx; ", t.percentile, t.warnings, t.precision, t.recall, t.fp_rate, t.lift);
        if (std::fabs(t.percentile - chosen_percentile) < 1e-9)
            res.pass = t.warnings >= 5 && t.lift >= 1.5 && t.recall >= 0.5; // must beat chance, and must catch most hard triggers
    }
    res.detail += fmt("| data-recommended percentile=%.2f (chosen=%.2f)", cal.recommended, chosen_percentile);
    return res;
}

// ============ §10.4 Label-mapping re-validation ============
inline CheckResult check_label_mapping(const std::vector<DayRecord>& days, size_t window = 150, size_t n_seeds = 6, double min_agreement = 0.80) {
    CheckResult res{"10.4 cluster-to-regime mapping re-derivation", false, ""};
    if (days.size() < window) { res.detail = "insufficient history"; return res; }
    Mat rows = rows_of(days, days.size() - window, days.size());
    std::vector<RegimeModel> models;
    for (unsigned s = 0; s < n_seeds; ++s) models.push_back(regime::fit_batch(rows, 1000 + 17 * s));
    bool bij = true; size_t index_changes = 0;
    for (auto& m : models) {
        std::array<int, 5> seen{};
        for (auto r : m.label_mapping.cluster_to_regime) ++seen[static_cast<size_t>(r)];
        for (int c : seen) bij = bij && c == 1;
    }
    double min_ag = 1.0;
    for (size_t s = 1; s < models.size(); ++s) {
        size_t same = 0;
        for (auto& r : rows) same += classify(models[0], r) == classify(models[s], r);
        min_ag = std::min(min_ag, static_cast<double>(same) / rows.size());
        for (size_t c = 0; c < 5; ++c) index_changes += models[s].label_mapping.cluster_to_regime[c] != models[0].label_mapping.cluster_to_regime[c];
    }
    res.detail = fmt("%zu seeds | bijective every refit=%s | min cross-seed day agreement=%.3f | cluster-index->label changes vs seed0=%zu (nonzero proves the index is not hard-coded)",
                     n_seeds, bij ? "yes" : "NO", min_ag, index_changes);
    res.pass = bij && min_ag >= min_agreement;
    return res;
}

// ============ §10.5 Event-day segregation ============
struct EventEffect { size_t event_days = 0; double loading_cos[3] = {0, 0, 0}; double agreement = 0.0; double dEV = 0.0; };
inline CheckResult check_event_day_effect(const std::vector<DayRecord>& days, double min_cos = 0.90, double min_agreement = 0.80, EventEffect* out = nullptr) {
    CheckResult res{"10.5 event-day segregation", false, ""};
    EventEffect e;
    for (auto& d : days) e.event_days += d.event_day;
    if (e.event_days < 3 || days.size() < 100) { res.detail = "too few flagged event days (or too little history) to compare"; return res; }
    RegimeModel all = regime::fit_batch(rows_of(days, 0, days.size()));
    RegimeModel ex = regime::fit_batch(rows_of(days, 0, days.size(), true));
    for (size_t pc = 0; pc < 3; ++pc) e.loading_cos[pc] = abs_cosine(loading_column(all, pc), loading_column(ex, pc));
    size_t same = 0, tot = 0;
    for (auto& d : days) if (!d.event_day) { ++tot; same += classify(all, d.features) == classify(ex, d.features); }
    e.agreement = tot ? static_cast<double>(same) / tot : 0.0;
    e.dEV = all.pca_model.cumulative_explained_variance() - ex.pca_model.cumulative_explained_variance();
    if (out) *out = e;
    res.detail = fmt("%zu event days | loading |cos| incl-vs-excl PC1=%.3f PC2=%.3f PC3=%.3f | non-event day label agreement=%.3f | cumulative EV delta=%+.4f",
                     e.event_days, e.loading_cos[0], e.loading_cos[1], e.loading_cos[2], e.agreement, e.dEV);
    res.pass = e.loading_cos[0] >= min_cos && e.loading_cos[1] >= min_cos && e.agreement >= min_agreement;
    return res;
}

// ============ full suite ============
struct SuiteInput { std::vector<DayRecord> days; std::vector<IntradayBar> bars; double soft_percentile = 0.80; };
inline std::vector<CheckResult> run_suite(const SuiteInput& in) {
    return { check_regime_separation(in.days), check_refit_stability(in.days), check_trigger_calibration(in.bars, in.soft_percentile),
             check_label_mapping(in.days), check_event_day_effect(in.days) };
}
inline bool report(const std::vector<CheckResult>& rs, std::FILE* f = stdout) {
    bool all = true;
    std::fprintf(f, "PCA REGIME MODEL -- SPEC SECTION 10 VALIDATION\n");
    for (auto& r : rs) { std::fprintf(f, "[%s] %s\n      %s\n", r.pass ? "PASS" : "FAIL", r.name.c_str(), r.detail.c_str()); all = all && r.pass; }
    std::fprintf(f, "VERDICT: %s\n", all ? "all five gates passed" : "NOT cleared for live-decision use");
    return all;
}

} // namespace vt::validation
