#include <cassert>
#include <cstdio>
#include <set>
#include "../src/multibook/mb_bridge.h"
using namespace vt; using namespace vt::mb;
struct Fake : adapters::OptionsChainAdapter {
    long long utc = 1792505400LL; double mult = 1.0;     // spot multiplier (price drift)
    ChainSnapshot fetch_snapshot(const std::string& u) override {
        static const std::map<std::string, double> sp{{"NQ", 21850.25}, {"NDX", 21800}, {"QQQ", 520.1}, {"NVDA", 130}, {"AAPL", 230}, {"MSFT", 420}, {"TQQQ_SQQQ", 75}, {"VX", 17.2}};
        ChainSnapshot c; c.underlying = u; c.spot = sp.at(u) * mult; c.contract_multiplier = 100; c.ts = TimePoint(std::chrono::seconds(utc)); c.risk_free_rate = 0.04; if (u == "NQ") return c;
        double s = c.spot, step = std::max(0.5, std::round(s * 0.004));
        for (int dte : {0, 1, 7, 30}) for (int i = -25; i <= 25; ++i) { double K = std::round(s / step) * step + i * step, d = std::abs(i);
            for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = K; q.dte_days = dte; q.t_years = (dte == 0 ? std::max(0.01, (960 - (utc % 86400) / 60.0 + 4 * 60) / 525600.0) : dte / 365.0); q.iv = 0.18 + 0.0015 * d; q.mid = 1;
                q.open_interest = 1000.0 * std::exp(-d / 9.0) * (t == OptionType::Call ? (i >= 0 ? 1.4 : 0.7) : (i <= 0 ? 1.4 : 0.7)); c.quotes.push_back(q); } }
        return c; }
};
int main() {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json"); Fake f; SessionTracker T; BookCache cache; EngineOptions eo; eo.track = &T;
    VolIn v; v.available = true; v.id = "VXN"; v.regime = "elevated"; v.trend = 1; eo.vols["NQ"] = v;
    auto run = [&](long long utc, double mult) { f.utc = utc; f.mult = mult; return run_scope(r, scope::build(r, "NQ", scope::Mode::Family, "", {}, "mock"), f, cache, eo); };
    const long long t0 = 1792503600LL;   // 09:40 ET Tue 2026-10-20
    auto a = run(1792492800LL, 1.0);                 // 06:00 ET: pre-open -> nothing recorded
    assert(a.session && !a.session->ok && T.series("QQQ").empty());
    auto b = run(t0, 1.000); assert(b.session && b.session->ok && b.session->n == 1 && b.session->observed.empty() && b.session->charm_headline.find("no through-the-session trend") != std::string::npos);
    run(t0 + 15 * 60, 1.002); run(t0 + 35 * 60, 1.004); auto c = run(t0 + 55 * 60, 1.006);
    auto& S = *c.session; assert(S.ok && S.n == 4 && !S.observed.empty() && S.observed[0].window == "Since open");
    assert(S.observed[0].move_pct > 0.5 && S.observed[0].move_pct < 0.7);                     // +0.6% over the window
    for (auto& o : S.observed) { bool same = (o.flow_c > 0) == (o.move_pct > 0); assert(same ? o.reading.find("WITH price") != std::string::npos : o.reading.find("AGAINST price") != std::string::npos); assert(o.from < o.to); }
    bool l30 = false; for (auto& o : S.observed) if (o.window == "Last 30 min") l30 = true; assert(l30);
    assert(S.pts.size() == 4 && S.pts[0].min_since_open == 10 && S.pts[3].min_since_open == 65 && S.reads.size() >= 3);
    assert(S.charm_headline.find("Charm is") != std::string::npos && S.interaction.find("VXN is rising") != std::string::npos && S.closing.find("Net gamma") != std::string::npos && S.vol_text.find("VXN elevated") != std::string::npos);
    // unavailable vol -> interaction conditional, never guessed
    SessionTracker T2; eo.track = &T2; eo.vols.clear(); auto d = run(t0, 1.0); assert(d.session->interaction.find("conditional") != std::string::npos && d.session->vol_text.find("unavailable") != std::string::npos);
    // new session day resets; same-minute re-record replaces; backwards time ignored
    eo.track = &T; auto n1 = run(t0 + 86400, 1.0); assert(n1.session->n == 1); run(t0 + 86400 + 10, 1.0); assert(T.series("QQQ").size() == 1); run(t0 + 86400 - 600, 1.0); assert(T.series("QQQ").size() == 1);
    // payload carries the session block; Single-Book has none
    auto js = multibook_json(c); auto j = mj::parse(js); assert(j.at("session").at("ok").b && j.at("session").at("pts").a.size() == 4 && j.at("session").at("observed").a.size() >= 2);
    auto s1 = run_scope(r, scope::build(r, "QQQ", scope::Mode::Single, "", {}, "mock"), f, cache, eo); assert(!s1.session);
    std::puts("test_session_track OK"); return 0;
}
