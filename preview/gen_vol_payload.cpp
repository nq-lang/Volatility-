// Synthetic VIX/VXN history -> the real volatility_payload() output. Fixture for UI tests/screenshots only (MOCK).
#include <cmath>
#include <iostream>
#include "../src/volidx/vol_index.h"
using namespace vt;
static volidx::Series mk(const std::string& id, double base, double amp, double bump, bool term) {
    volidx::Series s; s.id = id; const long long t0 = 1760000000LL - 86400LL * 260;
    for (int i = 0; i < 260; ++i) s.daily.push_back({t0 + 86400LL * i, base + amp * std::sin(i * 0.19) + 0.8 * std::sin(i * 0.05) + 0.004 * i});
    s.daily.back().close += bump; s.as_of = s.daily.back().ts; if (term) { s.front = s.daily.back().close + 1.2; s.back = s.daily.back().close - 0.8; } return s; }
int main() {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json");
    std::map<std::string, volidx::Series> feed{{"VIX", mk("VIX", 17, 2.2, 3.5, true)}, {"VXN", mk("VXN", 21, 2.6, 6.0, false)}};
    std::map<std::string, std::vector<std::string>> links{{"VIX", {"SPX", "SPY", "XSP", "ES"}}, {"VXN", {"QQQ", "NDX", "NQ"}}};
    std::cout << volidx::volatility_payload(r, feed, links, {{"VIX", "S&P 500 30-day implied vol"}, {"VXN", "Nasdaq-100 30-day implied vol"}});
}
