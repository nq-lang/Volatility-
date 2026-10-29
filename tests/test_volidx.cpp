#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/volidx/vol_index.h"
using namespace vt;
static volidx::Series mk(const std::string& id, int n, double base, double amp, double last_bump = 0) {
    volidx::Series s; s.id = id;
    for (int i = 0; i < n; ++i) s.daily.push_back({86400LL * i, base + amp * std::sin(i * 0.21) + 0.01 * i});
    s.daily.back().close += last_bump; return s;
}
int main() {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json");
    assert(r.volatility.size() == 2 && r.is_vol("vix") && r.is_vol("VXN") && r.selectable("VIX") && !r.is_vol("QQQ"));
    assert(r.vol_anchor.at("VXN") == "NQ" && r.vol_anchor.at("VIX") == "ES");
    assert(!r.is_anchor("VIX") && r.families_of("VIX").empty());              // not in the futures section, not a book
    // no feed -> unavailable, reason stated, no regime
    volidx::Series e; e.id = "VIX"; auto a0 = volidx::analyze(e); assert(!a0.available && !a0.reason.empty() && a0.regime == volidx::Regime::Unknown);
    // short history -> available but regime withheld
    auto a1 = volidx::analyze(mk("VIX", 30, 18, 2)); assert(a1.available && a1.regime == volidx::Regime::Unknown && a1.confidence < 0.2 && !a1.notes.empty());
    assert(a1.effect.find("withheld") != std::string::npos);
    // spike on top of a calm window -> top percentile -> stress, rising
    auto hi = volidx::analyze(mk("VIX", 260, 16, 1.5, 15)); assert(hi.regime == volidx::Regime::Stress && hi.pct_rank > 0.95 && hi.range_pos > 0.95 && hi.z > 1.5);
    assert(hi.chg_1d && *hi.chg_1d > 10 && hi.trend == 1 && hi.effect.find("Stress-level") != std::string::npos && hi.effect.find("selling") != std::string::npos);
    // collapse -> compressed, falling -> vanna supportive
    auto lo = volidx::analyze(mk("VIX", 260, 16, 1.5, -6)); assert(lo.regime == volidx::Regime::Compressed && lo.trend == -1 && lo.effect.find("buying") != std::string::npos);
    // term only when supplied
    auto s = mk("VXN", 260, 20, 2); assert(volidx::analyze(s).term_state == "unavailable");
    s.front = 24; s.back = 21; auto bk = volidx::analyze(s); assert(bk.term_state == "backwardation" && *bk.term_slope < 0);
    s.front = 20; s.back = 23; assert(volidx::analyze(s).term_state == "contango");
    s.realized = 0.15; assert(std::fabs(*volidx::analyze(s).iv_rv - (volidx::analyze(s).level / 100 - 0.15)) < 1e-12);
    // divergence: needs common sessions; tech stress when VXN spread jumps
    auto vix = mk("VIX", 260, 16, 1.5), vxn = mk("VXN", 260, 20, 2, 9);
    auto d = volidx::divergence(vxn, vix); assert(d.available && d.state == "tech_stress" && d.spread_z > 1 && d.text.find("QQQ/NDX/NQ") != std::string::npos);
    auto d2 = volidx::divergence(mk("VXN", 20, 20, 2), vix); assert(!d2.available && !d2.reason.empty());
    // payload: every registry vol ticker present; missing feed explicit; valid-looking JSON
    std::map<std::string, volidx::Series> feed{{"VIX", vix}};
    std::map<std::string, std::vector<std::string>> links{{"VIX", {"SPX", "SPY", "ES"}}, {"VXN", {"QQQ", "NDX", "NQ"}}};
    auto js = volidx::volatility_payload(r, feed, links, {{"VIX", "S&P 500 30d implied vol"}});
    assert(js.find("\"VXN\":{\"id\":\"VXN\",\"available\":false") != std::string::npos && js.find("no VXN feed connected") != std::string::npos);
    assert(js.find("\"VIX\":{\"id\":\"VIX\",\"available\":true") != std::string::npos && js.find("\"anchor\":\"ES\"") != std::string::npos && js.find("needs both VXN and VIX") != std::string::npos);
    std::puts("test_volidx OK"); return 0;
}
