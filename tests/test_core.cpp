// Headless smoke test for the entire quant core. Builds synthetic chain data
// and exercises every model in Section 3 of the build spec, plus the scheduler
// with stub adapters (proving the "runs headless without the web view" requirement).
// Build: see tests/CMakeLists snippet / the root CMakeLists `vt_tests` target.
#include <cassert>
#include <iostream>
#include <random>
#include <regex>
#include <set>
#include <map>
#include "vt/types.h"
#include "vt/blackscholes.h"
#include "vt/adapters.h"
#include "../src/core/gex.h"
#include "../src/core/dex.h"
#include "../src/core/vanna.h"
#include "../src/core/charm.h"
#include "../src/core/speed_color.h"
#include "../src/core/theta.h"
#include "../src/core/vrp.h"
#include "../src/core/rnd.h"
#include "../src/core/delta_iv_surface.h"
#include "../src/core/iv_zscore.h"
#include "../src/core/oi_momentum.h"
#include "../src/core/gamma_flip.h"
#include "../src/core/rv_iv_zscore_tod.h"
#include "../src/core/scheduler.h"

using namespace vt;

static ChainSnapshot make_synthetic_chain(double spot = 6000.0) {
    ChainSnapshot chain;
    chain.underlying = "SPX";
    chain.ts = Clock::now();
    chain.spot = spot;
    chain.risk_free_rate = 0.05;
    chain.dividend_yield = 0.013;
    chain.contract_multiplier = 100.0;

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> oi_dist(100, 5000);
    std::uniform_real_distribution<double> vol_dist(10, 2000);

    for (int dte : {0, 1, 2, 5, 30}) {
        double T = std::max(dte, 1) / 365.0;
        for (int k = -20; k <= 20; ++k) {
            double strike = spot + k * 5.0;
            // simple synthetic smile: higher IV further OTM, higher on put side
            double base_iv = 0.14 + 0.0008 * std::abs(k);
            for (auto type : {OptionType::Call, OptionType::Put}) {
                OptionQuote q;
                q.type = type;
                q.strike = strike;
                q.dte_days = dte;
                q.t_years = T;
                q.iv = base_iv + (type == OptionType::Put ? 0.01 : 0.0);
                double px = bs::price(type, spot, strike, chain.risk_free_rate, chain.dividend_yield, q.iv, T);
                q.mid = px;
                q.bid = px * 0.98;
                q.ask = px * 1.02;
                q.open_interest = oi_dist(rng);
                q.volume = vol_dist(rng);
                q.is_opex = (dte == 30);
                chain.quotes.push_back(q);
            }
        }
    }
    return chain;
}

static std::vector<Bar> make_synthetic_bars(double start = 6000.0, int n = 60) {
    std::vector<Bar> bars;
    std::mt19937 rng(7);
    std::normal_distribution<double> ret(0.0, 0.0005);
    double px = start;
    auto t0 = Clock::now();
    for (int i = 0; i < n; ++i) {
        double o = px;
        double r = ret(rng);
        px *= (1.0 + r);
        double c = px;
        double h = std::max(o, c) * 1.0005;
        double l = std::min(o, c) * 0.9995;
        bars.push_back(Bar{t0 + std::chrono::minutes(i), o, h, l, c});
    }
    return bars;
}

// --- stub adapters for the scheduler's headless-mode smoke test ---
struct StubChainAdapter : adapters::OptionsChainAdapter {
    std::string last_underlying;
    ChainSnapshot fetch_snapshot(const std::string& u) override { last_underlying = u; return make_synthetic_chain(); }
};
struct StubBarAdapter : adapters::BarFeedAdapter {
    std::vector<Bar> recent_bars(const std::string&, std::chrono::minutes) override { return make_synthetic_bars(); }
};
struct StubHistAdapter : adapters::HistoricalBaselineAdapter {
    model::IvHistoryBucket iv_history(const std::string&, int dte, double delta) override {
        model::IvHistoryBucket b; b.dte_days = dte; b.delta = delta;
        std::mt19937 rng(1); std::normal_distribution<double> d(0.15, 0.02);
        for (int i = 0; i < 60; ++i) b.history.push_back(d(rng));
        return b;
    }
    model::TodBaseline tod_baseline(const std::string&, const model::TodBucketKey& key) override {
        model::TodBaseline b; b.key = key;
        std::mt19937 rng(2); std::normal_distribution<double> d(0.0, 0.01);
        for (int i = 0; i < 60; ++i) b.history.push_back(d(rng));
        return b;
    }
};

struct StubRegimeHistoryAdapter : adapters::RegimeHistoryAdapter {
    std::vector<std::vector<double>> trailing_feature_history(const std::string&, std::chrono::hours) override {
        std::mt19937 rng(9);
        std::normal_distribution<double> noise(0.0, 0.5);
        std::vector<std::vector<double>> rows;
        for (int i = 0; i < 150; ++i) {
            std::vector<double> row(vt::regime::kNumFeatures);
            for (auto& v : row) v = noise(rng);
            rows.push_back(row);
        }
        return rows;
    }
};

int main() {
    auto chain = make_synthetic_chain();
    auto bars = make_synthetic_bars();

    // --- Black-Scholes sanity: ATM call delta ~0.5-ish, gamma positive ---
    Greeks g = bs::greeks(OptionType::Call, 6000, 6000, 0.05, 0.013, 0.15, 30.0/365.0);
    assert(g.gamma > 0.0);
    assert(g.delta > 0.3 && g.delta < 0.7);
    std::cout << "[ok] black-scholes ATM greeks: delta=" << g.delta << " gamma=" << g.gamma
              << " vanna=" << g.vanna << " charm=" << g.charm
              << " speed=" << g.speed << " color=" << g.color << "\n";

    // --- Exposure family ---
    for (auto b : {DteBucket::D0, DteBucket::D1, DteBucket::D2, DteBucket::Term}) {
        auto gex = model::gex(chain, b);
        auto dex = model::dex(chain, b);
        auto van = model::vanna_exposure(chain, b);
        auto chm = model::charm_exposure(chain, b);
        auto spd = model::speed_exposure(chain, b);
        auto clr = model::color_exposure(chain, b);
        auto th  = model::theta_harvest(chain, b);
        std::cout << "[ok] " << to_string(b) << " GEX_net=" << exposure::net(gex)
                  << " DEX_net=" << exposure::net(dex)
                  << " points(gex)=" << gex.points.size() << "\n";
        (void)van; (void)chm; (void)spd; (void)clr; (void)th;
    }

    // --- VRP ---
    auto vrp0 = model::vrp_surface(chain, DteBucket::D0, bars, 98280.0);
    assert(!vrp0.points.empty());
    auto atm = model::atm_vrp(vrp0, chain.spot);
    assert(atm.has_value());
    std::cout << "[ok] VRP ATM(0DTE) = " << *atm << "\n";

    // --- RND ---
    auto rnd = model::rnd_surface(chain);
    assert(!rnd.points.empty());
    auto shape = model::shape_stats(rnd, 30, chain.spot, 50.0);
    std::cout << "[ok] RND mode(30DTE)=" << shape.mode_strike
              << " skew_proxy=" << shape.skewness_proxy << "\n";

    // --- Delta-indexed IV surfaces + divergence ---
    auto civ = model::call_delta_iv_surface(chain, DteBucket::D2);
    auto piv = model::put_delta_iv_surface(chain, DteBucket::D2);
    assert(!civ.points.empty() && !piv.points.empty());
    auto div = model::skew_divergence(civ, piv, 0.25, 2);
    std::cout << "[ok] 25-delta skew divergence(2DTE) = "
              << (div ? std::to_string(*div) : std::string("n/a")) << "\n";

    // --- Gamma flip ---
    auto flip = model::solve_gamma_flip(chain, DteBucket::Term);
    std::cout << "[ok] gamma flip: net_gex=" << flip.net_gex_at_spot
              << " regime=" << (flip.regime_long_gamma ? "long" : "short")
              << " flip_level=" << (flip.flip_level ? std::to_string(*flip.flip_level) : "not found in band")
              << "\n";

    // --- OI momentum tracker (call + put) ---
    model::CallOiRatioMomentum mom(5);
    std::optional<double> last;
    for (double r : {0.40, 0.41, 0.43, 0.46, 0.50, 0.55})
        last = mom.update(r);
    assert(last.has_value());
    std::cout << "[ok] call OI ratio momentum = " << *last << "\n";

    model::PutOiRatioMomentum pmom(5);
    std::optional<double> plast;
    for (double r : {0.60, 0.58, 0.55, 0.52, 0.48})
        plast = pmom.update(r);
    assert(plast.has_value());
    std::cout << "[ok] put OI ratio momentum = " << *plast << "\n";

    // --- IV z-score ---
    model::IvHistoryBucket hb; hb.dte_days = 1; hb.delta = 0.25;
    for (double x : {0.14, 0.15, 0.16, 0.15, 0.14, 0.155}) hb.history.push_back(x);
    auto z = model::iv_zscore(0.20, hb);
    std::cout << "[ok] IV z-score (n=" << z.sample_n << ") = " << z.z << "\n";

    // --- RV/IV time-of-day z-score ---
    model::TodBaseline tb; tb.key = "09:30-09:45";
    for (double x : {-0.01, 0.0, 0.01, -0.005, 0.005}) tb.history.push_back(x);
    auto rz = model::rv_iv_zscore(0.03, 0.02, tb);
    std::cout << "[ok] RV-IV TOD z-score (n=" << rz.sample_n << ") = " << rz.z << "\n";

    // --- Scheduler headless run (no webview) ---
    StubChainAdapter cadapt; StubBarAdapter badapt; StubHistAdapter hadapt;
    int publish_count = 0;
    vt::bridge::Sink sink = [&](const std::string& channel, const std::string& payload) {
        ++publish_count;
        assert(!payload.empty());
        (void)channel;
    };
    vt::core::Scheduler sched(cadapt, badapt, hadapt, sink);
    auto state = sched.run_cycle();
    assert(publish_count > 0);
    std::cout << "[ok] scheduler headless cycle published " << publish_count << " payloads\n";

    // --- Scheduler + PCA regime model, fully wired end-to-end, still headless ---
    vt::regime::RegimeModelService regime_svc;
    StubRegimeHistoryAdapter regime_hist;
    sched.attach_regime_model(regime_svc, &regime_hist);
    bool refit_ok = sched.refit_regime_model();
    assert(refit_ok);
    std::cout << "[ok] regime model batch refit succeeded via Scheduler::refit_regime_model()\n";

    int publish_count_with_regime = 0;
    bool saw_regime_live = false, saw_regime_static = false;
    vt::bridge::Sink sink2 = [&](const std::string& channel, const std::string& payload) {
        ++publish_count_with_regime;
        assert(!payload.empty());
        if (channel == "regime_live") saw_regime_live = true;
        if (channel == "regime_static") saw_regime_static = true;
    };
    vt::core::Scheduler sched2(cadapt, badapt, hadapt, sink2);
    sched2.attach_regime_model(regime_svc, &regime_hist);
    assert(sched2.refit_regime_model());
    saw_regime_static = false; // reset after the refit call above already published one
    auto state2 = sched2.run_cycle();
    assert(saw_regime_live);
    (void)state2;
    assert(regime_svc.has_baseline()); // first cycle of the session established the section-5 baseline
    std::cout << "[ok] Scheduler::run_cycle() published a regime_live payload once a model was attached\n";

    // --- model integrity through the scheduler: display layer on vs off must not change a single byte
    //     of the classifier's own channels (regime_static / regime_live) ---
    {
        auto run = [&](bool viz_on, std::string& stat, std::string& live, bool& saw_viz, bool& saw_viz_live) {
            vt::regime::RegimeModelService svc; vt::core::SchedulerConfig c; c.viz.enabled = viz_on; c.viz.tsne.iterations = 150;
            vt::core::Scheduler sc(cadapt, badapt, hadapt, [&](const std::string& ch, const std::string& pl) {
                if (ch == "regime_static") { stat = pl; }
                if (ch == "regime_live") { live = pl; }
                if (ch == "regime_viz") { saw_viz = true; }
                if (ch == "regime_viz_live") { saw_viz_live = true; } }, c);
            sc.attach_regime_model(svc, &regime_hist); sc.refit_regime_model(); sc.run_cycle();
        };
        std::string s_on, l_on, s_off, l_off; bool v1 = false, v2 = false, v3 = false, v4 = false;
        run(true, s_on, l_on, v1, v2); run(false, s_off, l_off, v3, v4);
        assert(s_on == s_off && l_on == l_off && !s_on.empty() && !l_on.empty());
        assert(v1 && v2 && !v3 && !v4);
        std::cout << "[ok] regime_static and regime_live are byte-identical with the display layer on or off; regime_viz channels only exist when on\n";
    }

    // --- ticker switch: the next cycle must query the NEW underlying, and only that ---
    {
        StubChainAdapter ca; vt::core::Scheduler s_tk(ca, badapt, hadapt, [](const std::string&, const std::string&) {});
        s_tk.run_cycle(); assert(ca.last_underlying == "SPX");
        for (const char* t : {"SPY", "NDX", "QQQ"}) { s_tk.set_underlying(t); s_tk.run_cycle(); assert(ca.last_underlying == t); }
        std::cout << "[ok] set_underlying() switches the instrument on the next cycle (SPX/SPY/NDX/QQQ)\n";
    }


    // --- DTE-bucket integrity on every bucketed channel (regression test) ---
    // The UI trusts each payload's "bucket" label to decide which DTE tab it
    // feeds. 0/1/2DTE payloads must contain ONLY that expiry; the term payload
    // must be labelled "term" (it spans many expiries). A mislabelled term
    // payload would overwrite the 0DTE tab with all-expiry data.
    {
        std::map<std::string, int> checked;
        auto check_sink = [&](const std::string& channel, const std::string& payload) {
            static const std::set<std::string> bucketed = {"gex","dex","vanna","charm","speed","color","theta","vrp","rnd","call_delta_iv","put_delta_iv"};
            if (!bucketed.count(channel)) return;
            std::smatch m;
            bool has_label = std::regex_search(payload, m, std::regex("\"bucket\":\"([^\"]+)\""));
            assert(has_label);
            std::string label = m[1].str();
            std::set<int> dtes;
            static const std::regex dte_re("\"dte\":(\\d+)");
            for (auto it = std::sregex_iterator(payload.begin(), payload.end(), dte_re); it != std::sregex_iterator(); ++it)
                dtes.insert(std::stoi((*it)[1]));
            if (label == "0DTE") { assert(dtes.size() == 1 && *dtes.begin() == 0); }
            else if (label == "1DTE") { assert(dtes.size() == 1 && *dtes.begin() == 1); }
            else if (label == "2DTE") { assert(dtes.size() == 1 && *dtes.begin() == 2); }
            else { assert(label == "term"); assert(dtes.size() > 1); }
            ++checked[channel + "/" + label];
        };
        vt::core::Scheduler s_dte(cadapt, badapt, hadapt, check_sink);
        s_dte.run_cycle();
        assert(checked.size() == 11 * 4);   // 11 channels x 4 buckets, each seen exactly under its own label
        std::cout << "[ok] DTE-bucket integrity: " << checked.size() << " channel/bucket payloads correctly labelled and isolated\n";
    }

    std::cout << "\nALL CORE SMOKE TESTS PASSED\n";
    return 0;
}
