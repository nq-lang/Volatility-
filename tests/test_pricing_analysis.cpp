#include <cassert>
#include <cstdio>
#include "../src/multibook/strike_lens.h"
#include "../src/multibook/vol_context.h"
using namespace vt; using namespace vt::mb;
int main() {
    ChainSnapshot c; c.underlying = "T"; c.spot = 600; c.ts = TimePoint(std::chrono::seconds(1792505400LL)); c.risk_free_rate = 0.04; c.contract_multiplier = 100;
    for (int dte : {0, 1, 7, 14, 30}) for (int i = -30; i <= 30; ++i) { double K = 600 + i * 2.5; for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = K; q.dte_days = dte; q.t_years = (dte == 0 ? 0.25 : dte) / 365.0; q.iv = 0.16 + 0.0012 * std::abs(i) + (t == OptionType::Put && i < 0 ? 0.01 : 0); q.mid = bs::price(t, 600, K, 0.04, 0.0, q.iv, q.t_years); q.open_interest = (t == OptionType::Call ? 1500 * std::exp(-std::pow((K - 610) / 12, 2)) : 1800 * std::exp(-std::pow((K - 585) / 12, 2))) + 50; c.quotes.push_back(q); } }
    auto a = compute_book("T", c); assert(a.dist.ok && a.dist.q05 < a.dist.q50 && a.dist.q50 < a.dist.q95); double s = 0; for (double h : a.dist.hist) s += h; assert(s > 0.9 && s <= 1.0 + 1e-9);
    LensBook b; b.id = "T"; b.spot = 600; b.convertible = true; b.ratio = 10; b.basis = 5; b.insight = finalize(a.insight);
    for (auto& cd : b.insight.swinging.cards) { LensLevel l{"Top |GEX|", cd.m.strike, 0, std::nullopt}; l.fut = cd.m.strike * 10 + 5; b.levels.push_back(l); }
    std::vector<const LensBook*> sib{&b}; std::map<std::string, CrossIn> none;
    auto A = analyze_book(b, a, "NQ", VolIn{}, none, sib); assert(A.ok && A.asset_class == "equity_index");
    assert(A.risk.ok && A.dist.ok && A.options.ok && A.fund.ok && A.risk.rows.size() >= 6 && !A.risk.inputs.empty() && !A.options.inputs.empty());
    bool unavail = false; for (auto& r : A.fund.rows) if (r.v == "unavailable") unavail = true; assert(unavail);           // no feed => stated, not inferred
    assert(A.fund.headline.find("No fundamental input") != std::string::npos);
    assert(A.factors.size() == 3 && !A.factors[0].live && A.pairs.size() == 4);
    assert(!A.trig.empty()); for (auto& t : A.trig) { assert(t.stages.size() >= 3 && !t.stops.empty() && !t.downstream.empty() && t.has_fut); assert(t.dir == "dn" ? t.strike < 600 : t.strike > 600); }
    std::map<std::string, CrossIn> live; CrossIn x; x.available = true; x.trend = 1; x.level = 104; live["DXY"] = x; auto A2 = analyze_book(b, a, "NQ", VolIn{}, live, sib);
    assert(A2.factors[0].live && A2.factors[0].live_state == "rising" && A2.fund.headline.find("US dollar") != std::string::npos);
    auto g = analysis_json(A); assert(g.find("\"casc\":{\"triggers\":[") != std::string::npos && g.find("\"hist\":{\"ok\":true") != std::string::npos);
    auto Ag = analyze_book(b, a, "XX", VolIn{}, none, sib); assert(Ag.pairs.empty() && Ag.asset_class == "other");
    // ---- shared VIX/VXN context (C-3)
    std::map<std::string, BookAnalytics> bm; bm["T"] = a; std::map<std::string, VolIn> vols; VolIn vx; vx.available = true; vx.id = "VXN"; vx.regime = "stress"; vx.trend = 1; vx.level = 26.1; vx.pct_rank = 0.91; vols["T"] = vx;
    VolIn vv; vv.available = true; vv.id = "VIX"; vv.regime = "normal"; vv.trend = -1; vv.level = 17; vv.pct_rank = 0.45; vols["S"] = vv;
    AggLadder h; h.vanna_up_c = -300; h.vanna_dn_c = 280;
    auto vcx = build_volctx(vols, {"T"}, &h, bm); assert(vcx.ok && vcx.headline.find("VXN 26.10 (stress, 91st percentile, rising) governs T") == 0);
    assert(vcx.hedge.find("STRESS") != std::string::npos && vcx.vanna.find("SELL 300") != std::string::npos && vcx.divergence.find("diverge") != std::string::npos && vcx.divergence.find("Nasdaq") != std::string::npos);
    assert(!vcx.vomma.empty() && vcx.sidebar.size() == 4);
    auto none2 = build_volctx({}, {"T"}, &h, bm); assert(!none2.ok && !none2.reason.empty());
    VolIn off; off.id = "VXN"; off.reason = "no VXN feed connected"; std::map<std::string, VolIn> ov{{"T", off}}; auto off2 = build_volctx(ov, {"T"}, &h, bm); assert(!off2.ok && off2.reason == "no VXN feed connected");
    assert(volctx_json(vcx).find("\"divergence\":\"VXN") != std::string::npos);
    std::puts("test_pricing_analysis OK"); return 0;
}
