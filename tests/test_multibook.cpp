#include <cassert>
#include <cstdio>
#include <set>
#include <random>
#include "../src/core/scheduler.h"
#include "../src/multibook/mb_bridge.h"
#include "../src/multibook/cross_block.h"
#include "../src/registry/mini_json.h"
using namespace vt;
using namespace vt::mb;
struct Fake : adapters::OptionsChainAdapter {
    std::map<std::string, double> spot; std::map<std::string, double> mult; std::set<std::string> down; std::vector<std::string> log; long long ts = 1000;
    ChainSnapshot fetch_snapshot(const std::string& u) override {
        log.push_back(u); if (down.count(u)) throw std::runtime_error("feed down: " + u);
        ChainSnapshot c; c.underlying = u; c.spot = spot.at(u); c.contract_multiplier = mult.count(u) ? mult[u] : 100.0; c.ts = TimePoint(std::chrono::seconds(ts)); c.risk_free_rate = 0.04;
        if (u == "NQ" || u == "ES" || u == "GC" || u == "YM" || u == "CL" || u == "SI") return c;   // anchor: price only
        double s = c.spot; double step = std::max(0.5, std::round(s * 0.005));
        for (int dte : {1, 7, 30}) for (int i = -20; i <= 20; ++i) { double K = std::round(s / step) * step + i * step; double d = std::abs(i);
            for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = K; q.dte_days = dte; q.t_years = dte / 365.0; q.iv = 0.2 + 0.002 * d; q.mid = std::max(0.01, bs::price(t, s, K, 0.04, 0.0, q.iv, q.t_years)); q.bid = .9; q.ask = 1.1;
                q.open_interest = 1000.0 * std::exp(-d / 8.0) * (t == OptionType::Call ? (i >= 0 ? 1.3 : 0.7) : (i <= 0 ? 1.3 : 0.7)); q.volume = 100; c.quotes.push_back(q); } }
        return c; }
};
struct StubBar : adapters::BarFeedAdapter { std::vector<Bar> recent_bars(const std::string&, std::chrono::minutes) override { return {}; } };
struct StubHist : adapters::HistoricalBaselineAdapter {
    model::IvHistoryBucket iv_history(const std::string&, int dte, double delta) override { model::IvHistoryBucket b; b.dte_days = dte; b.delta = delta; std::mt19937 g(1); std::normal_distribution<double> d(0.15, 0.02); for (int i = 0; i < 60; ++i) b.history.push_back(d(g)); return b; }
    model::TodBaseline tod_baseline(const std::string&, const model::TodBucketKey& key) override { model::TodBaseline b; b.key = key; std::mt19937 g(2); std::normal_distribution<double> d(0, .01); for (int i = 0; i < 60; ++i) b.history.push_back(d(g)); return b; }
};
static bool eq(double a, double b, double t = 1e-9) { return std::fabs(a - b) <= t * std::max(1.0, std::fabs(b)); }
static scope::Scope mk(const reg::Registry& r, const std::string& t, scope::Mode m, const std::string& fam = "") { return scope::build(r, t, m, fam, {}, "t"); }
int main() {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json");
    Fake f; f.spot = {{"NQ", 20100}, {"NDX", 20000}, {"QQQ", 430}, {"NVDA", 130}, {"AAPL", 230}, {"MSFT", 420}, {"TQQQ_SQQQ", 70}, {"VX", 17},
                      {"GC", 2300}, {"SPX", 5000}, {"OG", 2290}, {"GLD", 230}, {"GDX", 45}, {"IAU", 46}, {"GDXJ", 40}};
    f.mult = {{"OG", 100}};
    // ---- (1) Single View is bit-identical to the existing model functions
    BookCache cache; auto single = run_scope(r, mk(r, "QQQ", scope::Mode::Single), f, cache);
    { auto chain = f.fetch_snapshot("QQQ"); auto curve = model::gex(chain, DteBucket::Term); auto fl = model::solve_gamma_flip(chain);
      auto& a = single.books.at("QQQ"); assert(a.net_gex == exposure::net(curve)); assert(a.flip.has_value() == fl.flip_level.has_value() && (!a.flip || *a.flip == *fl.flip_level)); }
    assert(single.scope.mode == scope::Mode::Single && !single.lens);
    // ---- (2) scope guard: Single fetched exactly one book; NQ family never touches ES/SPX
    { Fake g = f; g.log.clear(); BookCache c2; run_scope(r, mk(r, "QQQ", scope::Mode::Single), g, c2); assert(g.log.size() == 1 && g.log[0] == "QQQ"); }
    BookCache cache2; Fake g = f; g.log.clear(); auto fam = run_scope(r, mk(r, "NQ", scope::Mode::Family), g, cache2);
    for (auto& id : g.log) assert(id != "SPX" && id != "ES" && id != "SPY");
    assert(fam.books.size() == 7 && fam.lens && fam.lens->anchor_px == 20100);
    // ---- (3) consistency invariant: QQQ alone == QQQ inside the family (field-wise identical)
    { auto& a = single.books.at("QQQ"); auto& b = fam.books.at("QQQ"); assert(a.net_gex == b.net_gex && a.spot == b.spot && a.oi_notional == b.oi_notional);
      assert((a.flip && b.flip && *a.flip == *b.flip) || (!a.flip && !b.flip)); assert(a.top.size() == b.top.size()); for (size_t i = 0; i < a.top.size(); ++i) assert(a.top[i].strike == b.top[i].strike && a.top[i].value == b.top[i].value); }
    // ---- (4) cache: same snapshot reused
    { BookCache c3; run_scope(r, mk(r, "NQ", scope::Mode::Family), f, c3); int m0 = c3.misses; run_scope(r, mk(r, "NQ", scope::Mode::Family), f, c3); assert(c3.misses == m0 && c3.hits >= 7);
      f.ts += 30; run_scope(r, mk(r, "NQ", scope::Mode::Family), f, c3); assert(c3.misses == m0 + 7); f.ts -= 30; }
    // ---- (5) weights, partial data
    { double t = 0; for (auto& w : fam.scope.books) t += w.weight; assert(eq(t, 1.0)); }
    { Fake d = f; d.down = {"NDX"}; BookCache c; auto x = run_scope(r, mk(r, "NQ", scope::Mode::Family), d, c); double t = 0; bool seen = false;
      for (auto& w : x.scope.books) { t += w.weight; if (w.id == "NDX") { assert(!w.included && w.excluded_reason.find("feed down") != std::string::npos); seen = true; } }
      assert(seen && eq(t, 1.0) && x.scope.confidence < 1 && !x.notes.empty()); for (auto& lb : x.lens->books) assert(lb.id != "NDX"); }
    { Fake d = f; d.down = {"NQ"}; BookCache c; auto x = run_scope(r, mk(r, "NQ", scope::Mode::Family), d, c); for (auto& lb : x.lens->books) assert(!lb.convertible); }
    // ---- (6) lens conversions
    auto find = [](const Lens& L, const std::string& id) -> const LensBook& { for (auto& b : L.books) if (b.id == id) return b; throw std::runtime_error("no " + id); };
    { auto& q = find(*fam.lens, "QQQ"); assert(q.convertible && eq(q.ratio, 20000.0 / 430.0) && eq(q.basis, 100)); assert(eq(to_futures(q, 430, 0.25), 20100.0)); assert(eq(to_futures(q, 431, 0.25), round_tick(431 * 20000.0 / 430 + 100, .25)));
      auto& n = find(*fam.lens, "NDX"); assert(eq(n.ratio, 1) && eq(n.basis, 100)); assert(eq(to_futures(n, 20250, .25), 20350));
      assert(!find(*fam.lens, "NVDA").convertible && !find(*fam.lens, "TQQQ_SQQQ").convertible && !find(*fam.lens, "VX").convertible); }
    { BookCache c; auto x = run_scope(r, mk(r, "GC", scope::Mode::Family), f, c); auto& g = find(*x.lens, "GLD"); assert(g.convertible && eq(g.ratio, 10) && g.basis == 0 && g.note.find("ratio only") == 0); assert(eq(to_futures(g, 231, .1), 2310));
      assert(!find(*x.lens, "GDX").convertible && !find(*x.lens, "GDXJ").convertible);
      auto& o = find(*x.lens, "OG"); assert(o.convertible && eq(o.basis, 10)); assert(eq(to_futures(o, 2300, .1), 2310)); }
    // levels carry futures equivalents consistent with the formula
    for (auto& b : fam.lens->books) for (auto& l : b.levels) { assert(b.convertible == l.fut.has_value()); if (l.fut) assert(eq(*l.fut, to_futures(b, l.strike, fam.lens->tick))); }
    // ---- (7) JSON validity
    { auto j = mj::parse(lens_json(*fam.lens)); assert(j.at("books").a.size() == 7 && j.at("anchor").str() == "NQ"); for (auto& b : j.at("books").a) { assert(b.has("insight") && b.at("insight").has("intraday") && b.at("insight").has("swinging")); } }
    // aggregate sanity
    assert(std::fabs(fam.sign_agreement) <= 1.0 + 1e-12 && fam.flip_confluence >= 0 && fam.flip_confluence <= 1);
    // ---- (7b) pricing levels: flat 20% IV chain => known expected move, RND ordered, 25D strikes straddle spot, conversion applied
    { auto& q = fam.books.at("QQQ"); auto& pr = q.pricing; assert(pr.ok && pr.atm_iv > 0.19 && pr.atm_iv < 0.22);
      double sig = q.spot * pr.atm_iv * std::sqrt(pr.ref_dte / 365.0); auto lv = [&](const char* l) { for (auto& x : pr.levels) if (x.label == l) return x.strike; throw std::runtime_error(l); };
      assert(eq(lv("Exp Move +1s"), q.spot + sig) && eq(lv("Exp Move -1s"), q.spot - sig));
      assert(lv("RND 5%") < lv("RND Median") && lv("RND Median") < lv("RND 95%")); assert(lv("25D Put") < q.spot && lv("25D Call") > q.spot);
      auto& lq = find(*fam.lens, "QQQ"); assert(!lq.pricing.empty()); for (auto& l : lq.pricing) assert(l.fut && eq(*l.fut, to_futures(lq, l.strike, fam.lens->tick)));
      auto j = mj::parse(lens_json(*fam.lens)); assert(!j.at("books").a[0].at("pricing").a.empty()); }
    // ---- (7c) hedge: per-book flows cached with the book, aggregate additive in anchor contracts, JSON on the multibook channel
    { auto& hq = fam.books.at("QQQ").hedge; assert(hq.ok && hq.ladder.size() == 7);
      assert(fam.hedge && fam.hedge->books >= 1); double den = 20100.0 * 20.0;   // NQ $20/pt
      std::vector<const HedgeAnalytics*> hs; for (auto& lb : fam.lens->books) if (lb.convertible && fam.books.count(lb.id) && fam.books.at(lb.id).hedge.ok) hs.push_back(&fam.books.at(lb.id).hedge);
      assert(fam.hedge->books == (int)hs.size()); double sum = 0; for (auto* h : hs) sum += h->ladder[5].flow_notional; assert(eq(fam.hedge->notional[5], sum)); (void)den;
      assert(!single.hedge); auto j = mj::parse(multibook_json(fam)); assert(j.at("hedge").at("books").num(0) >= 1 && j.at("hedge").at("shock_pct").a.size() == 7); }
    // ---- (7d) vol/risk: cached with the book, realized vol attaches without recompute, JSON on the lens
    { auto& vq = fam.books.at("QQQ").vol; assert(vq.ok && vq.term.size() >= 2 && !vq.vrp); BookCache c2; EngineOptions eo; eo.realized["QQQ"] = {0.10, 0.004};
      auto x = run_scope(r, mk(r, "NQ", scope::Mode::Family), f, c2, eo); auto& v2 = x.books.at("QQQ").vol; double ref = 0; for (auto& t : v2.term) if (t.dte == v2.ref_dte) ref = t.atm_iv;
      assert(v2.vrp && v2.em_ratio && eq(*v2.vrp, ref - 0.10));
      assert(!x.books.at("NDX").vol.vrp); auto j = mj::parse(lens_json(*x.lens)); bool seen = false; for (auto& b : j.at("books").a) if (b.at("id").str() == "QQQ") { seen = true; assert(b.at("vol").at("scen").a.size() == 15 && !b.at("vol").at("term").a.empty()); } assert(seen); }
    // ---- (7e) cross-book block + model service (multibook_v1 inputs); honest fallback when history is short
    { auto cb = cross_features(fam); assert(cb && cb->size() == cross_names().size()); assert(std::fabs((*cb)[0]) <= 1 && std::fabs((*cb)[1]) <= 1 + 1e-12 && (*cb)[3] >= 0 && (*cb)[4] >= 0 && (*cb)[6] > 0 && (*cb)[6] <= 1);
      assert(!cross_features(single)); { Fake d = f; BookCache c3; auto one = run_scope(r, mk(r, "NQ", scope::Mode::Family), d, c3); one.lens->books.erase(one.lens->books.begin() + 1, one.lens->books.end()); assert(!cross_features(one)); }
      MultibookModelService svc; auto st = mj::parse(svc.status_json("NQ", "QQQ", "t")); assert(!st.at("available").b && st.at("reason").str().find("insufficient history") == 0);
      std::mt19937 g(3); std::normal_distribution<double> N(0, 1); for (int i = 0; i < 160; ++i) { Vec a(17), x(7); for (auto& v : a) v = N(g); for (auto& v : x) v = N(g); svc.append_day("NQ", "QQQ", a, x); }
      assert(svc.days("NQ", "QQQ") == 160); auto ok = mj::parse(svc.status_json("NQ", "QQQ", "t")); assert(ok.at("available").b && ok.at("n_features").num(0) == 24 && ok.at("unavailable_features").a.size() == 3); }
    // ---- (8) scheduler: multibook + strike_lens channels only for non-Single scope; Single publishes neither
    { Fake sf = f; StubBar bb; StubHist hh; std::map<std::string, std::string> last; int n_mb = 0;
      core::Scheduler sch(sf, bb, hh, [&](const std::string& ch, const std::string& pl) { last[ch] = pl; if (ch == "multibook") ++n_mb; });
      sch.attach_multibook(r); sch.run_cycle(); assert(n_mb == 0 && !last.count("strike_lens"));
      sch.set_scope(mk(r, "QQQ", scope::Mode::Single)); sch.run_cycle(); assert(n_mb == 0);
      sch.set_scope(mk(r, "NQ", scope::Mode::Family)); sf.log.clear(); sch.run_cycle();
      assert(n_mb == 1 && last.count("strike_lens")); auto j = mj::parse(last["multibook"]); assert(j.at("scope").at("mode").str() == "family" && j.at("books").a.size() == 7);
      auto lj = mj::parse(last["strike_lens"]); assert(lj.at("anchor_px").num() == 20100);
      sch.run_cycle(); assert(sch.book_cache().hits >= 7); }
    std::puts("test_multibook OK"); return 0;
}
