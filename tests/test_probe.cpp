#include <cassert>
#include <iostream>
#include "../src/tools/source_probe.h"
using namespace vt;
struct FakeChain : adapters::OptionsChainAdapter {
    int calls = 0; int every;
    explicit FakeChain(int e) : every(e) {}
    ChainSnapshot fetch_snapshot(const std::string&) override {
        ChainSnapshot c; OptionQuote q{}; q.strike = 5000; q.open_interest = 1000.0 + (every > 0 ? 50.0 * (calls / every) : 0.0);
        c.quotes.push_back(q); ++calls; return c;
    }
};
struct FakeHist : adapters::RegimeHistoryAdapter {
    size_t have; explicit FakeHist(size_t h) : have(h) {}
    std::vector<std::vector<double>> trailing_feature_history(const std::string&, std::chrono::hours w) override {
        size_t want = static_cast<size_t>(w.count() / 24.0 * 252.0 / 365.0);
        return std::vector<std::vector<double>>(std::min(want, have), std::vector<double>(regime::kNumFeatures, 0.5));
    }
};
int main() {
    double clock = 0; auto sl = [&](double s) { clock += s; };
    FakeChain refreshing(6);                       // OI changes every 6 polls = 6 * 30 s
    auto a = tools::measure_oi_refresh_cadence(refreshing, "SPX", 100, 30.0, sl);
    assert(a.refreshes_intraday && std::fabs(a.median_interval_s - 180.0) < 1e-9);
    FakeChain frozen(0);
    auto b = tools::measure_oi_refresh_cadence(frozen, "SPX", 50, 30.0, sl);
    assert(!b.refreshes_intraday);
    FakeHist deep(400), shallow(60);
    auto d1 = tools::probe_history_depth(deep, "SPX"), d2 = tools::probe_history_depth(shallow, "SPX");
    assert(d1.supports_6mo && d1.supports_12mo);
    assert(!d2.supports_6mo && !d2.supports_12mo);
    tools::print(a, d2);
    std::cout << "[ok] source probe: cadence measured (180 s), frozen OI flagged, shallow history flagged\n";
}
