#include <cassert>
#include <cstdio>
#include <set>
#include "../src/insight/scoped_analysis.h"
#include "../src/registry/mini_json.h"
using namespace vt; using namespace vt::mb; using namespace vt::insight;
struct Fake : adapters::OptionsChainAdapter {
    std::map<std::string, double> spot; long long ts = 1000;
    ChainSnapshot fetch_snapshot(const std::string& u) override {
        ChainSnapshot c; c.underlying = u; c.spot = spot.at(u); c.contract_multiplier = 100; c.ts = TimePoint(std::chrono::seconds(ts)); c.risk_free_rate = 0.04;
        if (u == "NQ" || u == "ES" || u == "GC") return c;
        double s = c.spot, step = std::max(0.5, std::round(s * 0.005));
        for (int dte : {1, 7, 30}) for (int i = -20; i <= 20; ++i) { double K = std::round(s / step) * step + i * step, d = std::abs(i);
            for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = K; q.dte_days = dte; q.t_years = dte / 365.0; q.iv = 0.2 + 0.002 * d; q.mid = 1; q.open_interest = 1000.0 * std::exp(-d / 8.0) * (t == OptionType::Call ? (i >= 0 ? 1.3 : 0.7) : (i <= 0 ? 1.3 : 0.7)); q.volume = 100; c.quotes.push_back(q); } }
        return c; } };
struct Script : LlmClient { std::vector<std::string> outs; size_t i = 0; int calls = 0; std::vector<std::string> users, systems;
    std::string complete(const std::string& sys, const std::string& user) override { ++calls; users.push_back(user); systems.push_back(sys); std::string o = outs[std::min(i, outs.size() - 1)]; ++i; return o; } };
static std::string fmt(double v, int d = 2) { char b[64]; std::snprintf(b, sizeof b, "%.*f", d, v); return b; }
int main() {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json"); Fake f;
    f.spot = {{"NQ", 20100}, {"NDX", 20000}, {"QQQ", 430}, {"NVDA", 130}, {"AAPL", 230}, {"MSFT", 420}, {"TQQQ_SQQQ", 75}, {"VX", 17.2}};
    RegimeBrief rg; rg.probs = {{"Trending / Directional-Bias", 0.64}, {"Transitional / Breakout", 0.24}, {"Mean-Reversion", 0.07}}; rg.drivers = {"Net GEX (z)", "Speed (z)"}; rg.trigger = "soft trigger active"; rg.model_variant = "multibook_v1";
    BookCache c1, c2; auto fam = run_scope(r, scope::build(r, "NQ", scope::Mode::Family, "", {}, "ts1"), f, c1);
    auto sgl = run_scope(r, scope::build(r, "QQQ", scope::Mode::Single, "", {}, "ts1"), f, c2); RegimeBrief rs = rg; rs.model_variant = "single_v1";
    // ---- (1) the system prompt is spec section 9 verbatim (header, all 8 hard rules, 8 output sections) and the addendum is separate
    { std::string p = SCOPED_SYSTEM_PROMPT; assert(p.rfind("ROLE\nYou are the analysis engine of a structural options-flow terminal.", 0) == 0);
      for (auto s : {"1. Never state a number, level, or probability that is not in the input.", "2. Respect scope. In single mode, discuss ONLY the selected book.", "3. Dealer positioning is inferred.", "4. Report regime as a probability distribution",
                     "5. If meta.mock_data is true, begin with \"MOCK DATA - illustrative only\"", "6. Surface disagreement", "7. Match tone to confidence.", "8. No trade instructions, price targets, or guarantees.", "6. Cross-book structure (multi_book/family only)",
                     "Keep each section to 2-4 sentences; lead with the most decision-relevant item."}) assert(p.find(s) != std::string::npos);
      assert(p.find("NUMBER FORMAT") == std::string::npos && std::string(SCOPED_ADDENDUM).find("NUMBER FORMAT") != std::string::npos); }
    // ---- (2) structured input is JSON and carries ONLY in-scope books (scope-leak test on the INPUT)
    std::string in_f = build_input(fam, rg, false), in_s = build_input(sgl, rs, false);
    { auto jf = mj::parse(in_f), js = mj::parse(in_s); assert(jf.at("scope").at("mode").str() == "family" && jf.at("per_book").a.size() == 7 && jf.at("aggregate").at("confidence").num(0) > 0);
      assert(js.at("scope").at("mode").str() == "single" && js.at("per_book").a.size() == 1 && js.at("per_book").a[0].at("id").str() == "QQQ" && js.at("scope").at("anchor").str().empty());
      assert(detail::leaks_in(in_s, r, sgl.scope).empty());                                      // single QQQ: no NQ / NDX / NVDA ... anywhere in the model input
      assert(detail::leaks_in(in_f, r, fam.scope).empty());                                      // NQ family: nothing from ES / GC / ... families
      auto l = detail::leaks_in(in_f, r, sgl.scope); assert(!l.empty()); }                       // sanity: the checker does see foreign symbols
    // ---- (3) validator: numeric grounding
    double flipQ = *fam.books.at("QQQ").flip, w0 = fam.scope.books[0].weight; auto ok_text = [&](const std::string& extra) { return "1. Scope: family, anchor NQ, QQQ " + fmt(w0 * 100, 0) + "%.\n2. Regime: Trending / Directional-Bias 64%, Transitional / Breakout 24%.\n3. If dealers are net long gamma as assumed, QQQ flip " + fmt(flipQ) + ". " + extra + "\n4. Vol.\n5. Structure.\n6. Cross-book.\n7. Risk.\n8. Confidence " + fmt(fam.scope.confidence * 100, 0) + "%.\n"; };
    { auto v = validate(ok_text(""), in_f, r, fam.scope, false); assert(v.ok);
      auto bad = validate(ok_text("Price could reach 24,475 by Friday."), in_f, r, fam.scope, false); assert(!bad.ok && bad.untraceable.size() == 1 && bad.untraceable[0] == "24,475");
      auto rnd = validate(ok_text("Flip rounded " + fmt(flipQ, 0) + "."), in_f, r, fam.scope, false); assert(rnd.ok);               // rounding to fewer decimals is allowed
      auto pc = validate(ok_text("Trending carries 64% and 0.64 of the mass."), in_f, r, fam.scope, false); assert(pc.ok);          // percent <-> fraction
      auto lab = validate(ok_text("0DTE and 25D strikes and 1σ move matter; a 2x factor."), in_f, r, fam.scope, false); assert(lab.ok);    // conventional labels
      auto sum = validate(ok_text("Combined weight " + fmt((w0 + fam.scope.books[1].weight) * 100, 1) + "%."), in_f, r, fam.scope, false); assert(!sum.ok); }   // derived/summed numbers are NOT traceable
    // ---- (4) validator: scope leak, mock disclaimer, structure
    { auto v = validate(ok_text("ES futures also look heavy."), in_f, r, fam.scope, false); assert(!v.ok && !v.leaks.empty() && v.leaks[0] == "ES");
      std::string st = "1. Scope: single QQQ.\n2. Regime: Trending / Directional-Bias 64%.\n3. QQQ flip " + fmt(*sgl.books.at("QQQ").flip) + ".\n4. a\n5. b\n6. c\n7. d\n";
      assert(validate(st, in_s, r, sgl.scope, false).ok); auto l1 = validate(st + "NQ equivalent shown.\n", in_s, r, sgl.scope, false); assert(!l1.ok && l1.leaks.size() == 1 && l1.leaks[0] == "NQ");
      auto l2 = validate(st + "SPY agrees.\n", in_s, r, sgl.scope, false); assert(!l2.ok);
      assert(!validate(ok_text(""), build_input(fam, rg, true), r, fam.scope, true).ok);                                             // mock: missing disclaimer
      assert(validate("MOCK DATA - illustrative only\n" + ok_text(""), build_input(fam, rg, true), r, fam.scope, true).ok);
      assert(!validate("1. only one section " + fmt(flipQ) + "\n", in_f, r, fam.scope, false).ok); }
    // ---- (5) engine: reject -> regenerate with reasons -> accept; cache by (scope, snapshot_ts)
    { std::vector<std::string> pub; Script llm; llm.outs = {ok_text("Price could reach 24,475."), ok_text("")}; ScopedAnalysisEngine eng(llm, [&](const std::string& ch, const std::string& p) { if (ch == "scoped_insight") pub.push_back(p); }, r);
      auto a = eng.run(fam, rg, false); assert(a.validated && a.attempts == 2 && !a.fallback && llm.calls == 2 && llm.users[1].find("REJECTED") != std::string::npos && llm.users[1].find("24,475") != std::string::npos);
      assert(llm.systems[0].rfind("ROLE", 0) == 0);
      auto b = eng.run(fam, rg, false); assert(b.cached && llm.calls == 2 && b.text == a.text);                                      // same (scope, snapshot_ts): no LLM call
      f.ts = 2000; BookCache c3; auto fam2 = run_scope(r, scope::build(r, "NQ", scope::Mode::Family, "", {}, "ts2"), f, c3); Script l2; l2.outs = {ok_text("")}; ScopedAnalysisEngine e2(l2, [&](auto, auto) {}, r);
      e2.run(fam, rg, false); e2.run(fam, rg, false); assert(l2.calls == 1); e2.run(fam2, rg, false); assert(l2.calls == 2);   // new snapshot_ts regenerates (text may fail validation: counted per attempt, still cached)
      f.ts = 1000; auto j = mj::parse(pub.back()); assert(j.at("scope").at("mode").str() == "family" && j.at("scope").at("model_variant").str() == "multibook_v1" && j.at("scope").at("books").a.size() == 7 && j.at("cached").b && j.at("validated").b); }
    // ---- (6) engine: persistent bad output -> deterministic fallback (validates by itself), published flagged, scope-badged
    { std::vector<std::string> pub; Script llm; llm.outs = {"Targets: 99,999 and ES too."}; ScopedAnalysisEngine eng(llm, [&](const std::string&, const std::string& p) { pub.push_back(p); }, r, 3);
      auto a = eng.run(fam, rg, true); assert(a.fallback && !a.validated && a.attempts == 3 && llm.calls == 3); assert(a.text.rfind("MOCK DATA - illustrative only", 0) == 0);
      assert(validate(a.text, build_input(fam, rg, true), r, fam.scope, true).ok);
      auto s = eng.run(sgl, rs, false); assert(s.fallback); auto vs = validate(s.text, build_input(sgl, rs, false), r, sgl.scope, false); assert(vs.ok);   // single-scope fallback names only QQQ
      auto j = mj::parse(pub.back()); assert(j.at("fallback").b && !j.at("validated").b && j.at("scope").at("mode").str() == "single" && j.at("scope").at("books").a.size() == 1 && !j.at("reason").str().empty()); }
    // ---- (7) scope change with the same snapshot re-generates (different cache key)
    { Script llm; llm.outs = {ok_text("")}; ScopedAnalysisEngine eng(llm, [](auto, auto) {}, r); eng.run(fam, rg, false); size_t after = eng.cached(); eng.run(sgl, rs, false); assert(eng.cached() == after + 1); }
    std::puts("test_llm OK"); return 0;
}
