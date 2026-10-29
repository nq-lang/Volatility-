#include <cassert>
#include <cstdio>
#include "../src/multibook/normalize.h"
using namespace vt::mb;
static bool eq(double a, double b, double t = 1e-9) { return std::fabs(a - b) < t * std::max(1.0, std::fabs(b)); }
int main() {
    // SPX 5000 vs ES 5012 (basis 12), SPY ~ SPX/10 with drift
    auto m = fit_level_map2(5000, 5012, 5100, 5112); assert(eq(m.ratio, 1) && eq(m.basis, 12)); assert(eq(m.to_anchor(5050), 5062)); assert(eq(m.to_book(m.to_anchor(5050)), 5050));
    auto spy = fit_level_map(500.0, 5012.0, 12.0); assert(eq(spy.ratio, 10.0));             // ES = SPY*10 + basis => ratio 10
    assert(eq(spy.to_anchor(505), 5062));
    auto qqq = fit_level_map(430.5, 20100.0); assert(eq(qqq.to_anchor(430.5), 20100.0));    // drifting ratio ~46.69, recovered live
    // dollar gamma: hand-computed. gamma .01, OI 1000, S 100
    assert(eq(dollar_gamma_1pct(1, 0.01, 1000, 100, 100), 0.01 * 1000 * 100 * 1e4 * 0.01));  // equity 100x => 100000
    assert(eq(dollar_gamma_1pct(-1, 0.0002, 500, 50, 5000), -0.0002 * 500 * 50 * 25e6 * 0.01)); // ES 50x => -1,250,000
    assert(eq(dollar_gamma_1pct(1, 0.001, 200, 20, 20000), 0.001 * 200 * 20 * 4e8 * 0.01));   // NQ 20x
    assert(eq(dollar_gamma_1pct(1, 1e-5, 100, 5, 40000), 1e-5 * 100 * 5 * 16e8 * 0.01));      // YM 5x
    assert(eq(dollar_gamma_1pct(1, 1e-4, 10, 100, 2400), 1e-4 * 10 * 100 * 2400.0 * 2400 * 0.01)); // GC 100oz
    assert(eq(dollar_gamma_1pct(1, 1e-3, 10, 1000, 70), 1e-3 * 10 * 1000 * 4900 * 0.01));     // CL 1000bbl
    assert(eq(dollar_gamma_1pct(1, 1e-3, 10, 5000, 30), 1e-3 * 10 * 5000 * 900 * 0.01));      // SI 5000oz
    // leveraged ETF: AUM 10e9, 3x, +2% => 10e9*6*0.02 = 1.2e9 buy; -3x same formula L^2-L = 12 => 2.4e9 for inverse on +2%
    assert(eq(lev_etf_rebalance(10e9, 3, 0.02), 1.2e9)); assert(eq(lev_etf_rebalance(10e9, -3, 0.02), 2.4e9)); assert(eq(lev_etf_rebalance(10e9, 3, -0.02), -1.2e9));
    assert(eq(lev_etf_rebalance(1e9, 2, 0.01), 2e7)); assert(eq(lev_etf_rebalance(1e9, 1, 0.05), 0));
    assert(eq(component_to_index(1e6, 0.08, 1.2), 96000));
    assert(eq(confluence({{100, .5}, {100.1, .3}, {110, .2}}, 0.005), 0.8)); assert(eq(confluence({}, .01), 0));
    std::puts("test_normalize OK"); return 0;
}
