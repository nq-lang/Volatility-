// Writes preview/synthetic_viz_payloads.json (MOCK DATA): the real C++ viz payload builders run on a synthetic
// 5-cluster history, so the UI is exercised with exactly the schema production emits.
#include <fstream>
#include <random>
#include "../src/viz/regime_viz.h"
using namespace vt; using namespace vt::viz; using namespace vt::regime;
static Mat hist(unsigned seed, size_t n) {
    std::mt19937 rng(seed); std::normal_distribution<double> g(0, 0.3); Mat rows;
    for (size_t i = 0; i < n; ++i) { Vec f(kNumFeatures); for (auto& v : f) v = g(rng); int a = rng() % 5;
        auto blk = [&](std::initializer_list<size_t> c, double x) { for (size_t k : c) f[k] += x; };
        if (a == 0) { blk({NetGEX_z, FlipDistancePct, DEX_z, Vanna_z}, 2.0); blk({ImpliedZ_ATM, ImpliedZ_25dPut, VRP_z, RvIvZscoreTod}, -1.0); blk({Speed_z, CharmPerHour_z, Color_z}, -1.0); }
        if (a == 1) blk({ImpliedZ_ATM, ImpliedZ_25dPut, VRP_z, RvIvZscoreTod}, 2.4);
        if (a == 2) { blk({NetGEX_z, FlipDistancePct, DEX_z, Vanna_z}, -2.0); blk({Speed_z, CharmPerHour_z, Color_z}, 1.2); }
        if (a == 3) { blk({NetGEX_z, FlipDistancePct, DEX_z, Vanna_z}, 0.4); blk({Speed_z, CharmPerHour_z, Color_z}, -2.4); }
        if (a == 4) blk({Speed_z, CharmPerHour_z, Color_z}, 2.4);
        rows.push_back(f); }
    return rows;
}
int main() {
    Mat h = hist(11, 230); RegimeModel m = fit_batch(h); VizConfig cfg;
    VizState s = build_viz_state(m, h, "SPX", cfg, nullptr);
    std::ofstream o("preview/synthetic_viz_payloads.json");
    o << "{\"static\":" << viz_static_json(m, s) << ",\"live\":[";
    Mat live = hist(77, 12);
    for (size_t i = 0; i < live.size(); ++i) {
        Vec f = live[i]; if (i > 7) for (size_t k = 0; k < kNumFeatures; ++k) f[k] += 0.15 * (i - 7);  // drift so the trail travels
        auto lv = evaluate_live(m, f);
        o << (i ? "," : "") << "{\"viz\":" << viz_live_json(m, s, f, cfg) << ",\"pca\":[" << lv.pca_coords[0] << "," << lv.pca_coords[1] << "," << lv.pca_coords[2] << "]}";
    }
    o << "]}";
}
