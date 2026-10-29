// Exercises the §10 validation suite on synthetic history with KNOWN structure
// (positive control) and with that structure destroyed (negative control).
// A validation suite that cannot fail is worthless, so the negative controls
// are asserted to FAIL.
#include <cassert>
#include <iostream>
#include <random>
#include "../src/validation/regime_validation.h"

using namespace vt; using namespace vt::validation; using namespace vt::regime;

// sep scales archetype offsets (1 = well separated, ~0.3 = heavily overlapping); sd is per-feature noise.
static Vec day_of(RegimeName a, std::mt19937& rng, double sep = 1.0, double sd = 0.2) {
    std::normal_distribution<double> g(0.0, sd);
    Vec f(kNumFeatures); for (auto& v : f) v = g(rng);
    // Real feature sets are blocks of co-moving columns, and standardization gives every column unit variance, so
    // a cluster axis carried by ONE column is indistinguishable from the noise plateau. Each axis here is a block:
    //   A = gamma/flip (NetGEX, FlipDistance, DEX, Vanna)   B = vol level (ImpliedZ x2, VRP, RV/IV)
    //   C = decay/speed (Speed, Charm, Color)
    // and the five centres lie in those three axes so a 3-PC projection can resolve them.
    auto blk = [&](std::initializer_list<size_t> cols, double amt) { for (size_t c : cols) f[c] += sep * amt; };
    auto A = [&](double x) { blk({NetGEX_z, FlipDistancePct, DEX_z, Vanna_z}, x); };
    auto B = [&](double x) { blk({ImpliedZ_ATM, ImpliedZ_25dPut, VRP_z, RvIvZscoreTod}, x); };
    auto C = [&](double x) { blk({Speed_z, CharmPerHour_z, Color_z}, x); };
    switch (a) {
        case RegimeName::PinningLowVol: A(2.0); B(-1.0); C(-1.0); break;
        case RegimeName::HighVolatilityRisk: B(2.4); break;
        case RegimeName::TrendingDirectional: A(-2.0); C(1.2); break;
        case RegimeName::MeanReversion: A(0.4); C(-2.4); break;
        case RegimeName::TransitionalBreakout: C(2.4); break;
        default: break;
    }
    return f;
}
static double range_of(RegimeName a, std::mt19937& rng) {
    static const double base[5] = {0.004, 0.014, 0.011, 0.007, 0.009}; // pinning tightest, high-vol widest
    std::lognormal_distribution<double> ln(0.0, 0.25);
    return base[static_cast<size_t>(a)] * ln(rng);
}

static double cg_pick(const std::vector<IntradayBar>& b) { return calibrate_soft_trigger(b).recommended; }

int main() {
    std::mt19937 rng(7);
    RegimeName arch[5] = {RegimeName::PinningLowVol, RegimeName::HighVolatilityRisk, RegimeName::TrendingDirectional, RegimeName::MeanReversion, RegimeName::TransitionalBreakout};
    SuiteInput in; in.days.reserve(500);
    for (int i = 0; i < 500; ++i) {
        RegimeName a = arch[rng() % 5];
        DayRecord d; d.features = day_of(a, rng); d.fwd_range = range_of(a, rng); d.event_day = (rng() % 20 == 0);
        in.days.push_back(d);
    }
    // intraday: flip_pct changes sign every ~45 bars; speed is elevated in the 5 bars before each change
    std::normal_distribution<double> n(0.0, 1.0);
    std::vector<IntradayBar> good, bad;
    double sign = 1.0; size_t next = 45;
    for (size_t i = 0; i < 6000; ++i) {
        if (i == next) { sign = -sign; next += 35 + rng() % 25; }
        bool pre = (next - i) <= 5;
        IntradayBar b; b.flip_pct = sign * (0.2 + 0.05 * std::fabs(n(rng))); if (i == next - 0) b.flip_pct = sign * 0.2;
        b.speed = n(rng) + (pre ? 4.0 : 0.0);
        good.push_back(b);
        IntradayBar u = b; u.speed = n(rng); bad.push_back(u);
    }
    in.bars = good;

    auto results = run_suite(in);
    bool all = report(results);
    // the soft trigger default (p80) is only a spec example; calibration is allowed to say it is too loose
    in.soft_percentile = cg_pick(in.bars);
    results = run_suite(in); all = report(results);
    for (auto& r : results) assert(r.pass);
    assert(all);

    // negative control 1: outcomes carry no regime information -> separation must FAIL
    auto shuffled = in.days; std::mt19937 r2(99);
    for (size_t i = shuffled.size() - 1; i > 0; --i) std::swap(shuffled[i].fwd_range, shuffled[r2() % (i + 1)].fwd_range);
    auto neg = check_regime_separation(shuffled);
    std::cout << "[neg-control] shuffled outcomes -> " << (neg.pass ? "PASS (BAD)" : "FAIL (correct)") << "\n  " << neg.detail << "\n";
    assert(!neg.pass);

    // negative control 2: speed unrelated to flip changes -> precision must be materially worse than the informative series
    auto cg = calibrate_soft_trigger(good), cb = calibrate_soft_trigger(bad);
    auto at = [](const TriggerCalibration& c, double p) { for (auto& t : c.sweep) if (std::fabs(t.percentile - p) < 1e-9) return t; return c.sweep[0]; };
    std::cout << "[neg-control] lift@p90 informative=" << at(cg, 0.90).lift << "x uninformative=" << at(cb, 0.90).lift << "x\n";
    assert(at(cg, 0.90).lift > 3.0 && at(cb, 0.90).lift < 1.5);
    assert(!check_trigger_calibration(bad, 0.90).pass);

    // negative control 3: a flipped-sign pinning/trending relation must fail the direction gate
    auto inv = in.days; for (auto& d : inv) d.fwd_range = 0.02 - d.fwd_range;
    assert(!check_regime_separation(inv).pass);
    // negative control 4: heavily overlapping clusters (sep 0.25, noisy) -> the stability/mapping gates must refuse
    std::vector<DayRecord> weak;
    for (int i = 0; i < 500; ++i) { RegimeName a = arch[rng() % 5]; DayRecord d; d.features = day_of(a, rng, 0.25, 0.6); d.fwd_range = range_of(a, rng); weak.push_back(d); }
    auto w2 = check_refit_stability(weak), w4 = check_label_mapping(weak);
    std::cout << "[neg-control] overlapping clusters -> 10.2 " << (w2.pass ? "PASS" : "FAIL") << ", 10.4 " << (w4.pass ? "PASS" : "FAIL") << "\n  " << w2.detail << "\n  " << w4.detail << "\n";
    assert(!(w2.pass && w4.pass));
    std::cout << "[ok] validation suite: positive controls pass, negative controls fail\n";
    return 0;
}
