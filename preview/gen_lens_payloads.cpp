// MOCK fixtures for the Strike Lens UI, built by the REAL engine (run_scope + lens_json + multibook_json) on synthetic chains.
#include <cstdio>
#include <fstream>
#include <set>
#include "../src/multibook/mb_bridge.h"
#include "../src/multibook/vol_bridge.h"
#include "../src/chartfactors/chart_snapshot.h"
using namespace vt; using namespace vt::mb;
struct Fake : adapters::OptionsChainAdapter {
    std::map<std::string, double> spot, mult; std::set<std::string> down; long long utc = 1792505400LL; double drift = 1.0, ivk = 1.0;
    ChainSnapshot fetch_snapshot(const std::string& u) override {
        if (down.count(u)) throw std::runtime_error("stale feed (>5 min): " + u);
        ChainSnapshot c; c.underlying = u; c.spot = spot.at(u) * drift; c.contract_multiplier = mult.count(u) ? mult[u] : 100.0; c.ts = TimePoint(std::chrono::seconds(utc)); c.risk_free_rate = 0.04;
        static const std::set<std::string> anchors{"NQ", "ES", "GC", "YM", "CL", "SI"}; if (anchors.count(u)) return c;
        double s = spot.at(u), step = std::max(0.5, std::round(s * 0.004)); int h = 0; for (char ch : u) h = h * 31 + ch; double tilt = 0.8 + 0.4 * ((h & 7) / 7.0);
        for (int dte : {0, 1, 7, 30}) for (int i = -25; i <= 25; ++i) { double K = std::round(s / step) * step + i * step, d = std::abs(i);
            for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = K; q.dte_days = dte; q.t_years = (dte == 0 ? 0.25 : dte) / 365.0; q.iv = (0.18 + 0.0015 * d) * ivk; q.mid = std::max(0.01, bs::price(t, c.spot, K, 0.04, 0.0, q.iv, q.t_years)); q.bid = .9; q.ask = 1.1;
                q.open_interest = 1000.0 * std::exp(-d / 9.0) * (t == OptionType::Call ? (i >= 0 ? 1.4 : 0.7) * tilt : (i <= 0 ? 1.4 : 0.7)); q.volume = 100; c.quotes.push_back(q); } }
        return c; }
};
static volidx::Analysis synth_vol(const std::string& id, double base, double bump) {
    volidx::Series s; s.id = id; for (int i = 0; i < 260; ++i) s.daily.push_back({1760000000LL - 86400LL * (260 - i), base + 2.2 * std::sin(i * 0.19) + 0.8 * std::sin(i * 0.05) + 0.004 * i}); s.daily.back().close += bump; return volidx::analyze(s); }
int main(int, char** argv) {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json"); Fake f;
    f.spot = {{"ES", 6012.5}, {"SPX", 6000}, {"SPY", 599.4}, {"XSP", 600.0}, {"NVDA", 130}, {"AAPL", 230}, {"MSFT", 420}, {"UPRO_SPXU", 90},
              {"NQ", 21850.25}, {"NDX", 21800}, {"QQQ", 520.1}, {"TQQQ_SQQQ", 75}, {"VX", 17.2}, {"GC", 2712.4}, {"OG", 2705.0}, {"GLD", 250.3}, {"GDX", 45}, {"IAU", 51}, {"GDXJ", 40},
              {"YM", 43500.0}, {"DIA", 435.0}, {"DJX", 435.0}, {"OZA", 435.0}, {"UNH", 520}, {"GS", 600}, {"HD", 400}, {"CL", 70.5}, {"LO", 70.4}, {"USO", 72.0}, {"B_LCO", 74.0}, {"XLE", 90}, {"XOP", 130}, {"UCO_SCO", 25},
              {"SI", 31.5}, {"SO", 31.4}, {"SLV", 29.4}, {"SIL", 40}, {"SILJ", 15}, {"PSLV", 11}, {"AGQ_ZSL", 30}};
    std::ofstream o(argv[1]); o << "{";
    std::map<std::string, volidx::Analysis> an{{"VIX", synth_vol("VIX", 17, 3.5)}, {"VXN", synth_vol("VXN", 21, 6.0)}};
    auto emit = [&](const char* key, const std::string& t, scope::Mode m, const std::string& fam, std::set<std::string> down, bool withvol = true, int nh = 6, int step = 360) {
        Fake g = f; g.down = down; BookCache c; EngineOptions eo; if (withvol) eo.vols = vols_for_books(r, an); else eo.vols = vols_for_books(r, {}); SessionTracker trk; eo.track = &trk; AggregateResult res; std::vector<std::string> hist; for (int k = 0; k < nh; ++k) { double u = nh > 1 ? double(nh - 1 - k) / (nh - 1) : 0; g.utc = 1792505400LL - (long long)step * (nh - 1 - k); g.drift = nh == 6 ? 1.0 - 0.0007 * (5 - k) - (k == 3 ? 0.0004 : 0) : 1.0 + u * (0.0022 * std::sin(0.55 * k + 0.5) + 0.0008); g.ivk = 1.0 + 0.05 * u * std::sin(0.7 * k); BookCache c2; res = run_scope(r, scope::build(r, t, m, fam, {}, "mock"), g, c2, eo); if (res.lens) hist.push_back(vt::cf::chart_json(res, r)); }
        o << "\"" << key << "\":{\"scope\":{\"mode\":\"" << scope::mode_str(res.scope.mode) << "\",\"ticker\":\"" << t << "\",\"family\":\"" << res.scope.family << "\"},\"multibook\":" << multibook_json(res) << ",\"lens\":" << lens_json(*res.lens) << ",\"chart\":" << vt::cf::chart_json(res, r) << ",\"history\":["; for (size_t h = 0; h < hist.size(); ++h) o << (h ? "," : "") << hist[h]; o << "]}"; };
    emit("ES_multi_SPX", "SPX", scope::Mode::MultiBook, "ES", {}); o << ",";
    emit("NQ_family", "NQ", scope::Mode::Family, "", {}, true, 20, 120); o << ",";
    emit("ES_multi_SPX_degraded", "SPX", scope::Mode::MultiBook, "ES", {"SPY", "XSP"}); o << ",";
    emit("GC_family", "GC", scope::Mode::Family, "", {}, false); o << ",";
    emit("YM_family", "YM", scope::Mode::Family, "", {}); o << ",";
    emit("CL_family", "CL", scope::Mode::Family, "", {}, false); o << ",";
    emit("SI_family", "SI", scope::Mode::Family, "", {}, false); o << "}"; return 0;
}
