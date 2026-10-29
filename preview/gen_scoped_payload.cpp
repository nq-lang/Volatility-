// MOCK fixture: scoped_insight payloads produced by the REAL ScopedAnalysisEngine (validator + cache) with a scripted stand-in for the LLM.
#include <fstream>
#include "../src/insight/scoped_analysis.h"
using namespace vt; using namespace vt::mb; using namespace vt::insight;
struct Fake : adapters::OptionsChainAdapter { std::map<std::string, double> spot;
    ChainSnapshot fetch_snapshot(const std::string& u) override { ChainSnapshot c; c.underlying = u; c.spot = spot.at(u); c.contract_multiplier = 100; c.ts = TimePoint(std::chrono::seconds(1000)); c.risk_free_rate = 0.04; if (u == "NQ") return c;
        double s = c.spot, step = std::max(0.5, std::round(s * 0.005)); for (int dte : {1, 7, 30}) for (int i = -20; i <= 20; ++i) { double K = std::round(s / step) * step + i * step, d = std::abs(i);
            for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = K; q.dte_days = dte; q.t_years = dte / 365.0; q.iv = 0.2 + 0.002 * d; q.mid = 1; q.open_interest = 1000.0 * std::exp(-d / 8.0) * (t == OptionType::Call ? (i >= 0 ? 1.3 : 0.7) : (i <= 0 ? 1.3 : 0.7)); q.volume = 100; c.quotes.push_back(q); } } return c; } };
struct Say : LlmClient { std::string t; std::string complete(const std::string&, const std::string&) override { return t; } };
static std::string f2(double v, int d = 2) { char b[64]; std::snprintf(b, sizeof b, "%.*f", d, v); return b; }
int main(int, char** argv) {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json"); Fake f; f.spot = {{"NQ", 20100}, {"NDX", 20000}, {"QQQ", 430}, {"NVDA", 130}, {"AAPL", 230}, {"MSFT", 420}, {"TQQQ_SQQQ", 75}, {"VX", 17.2}};
    RegimeBrief rg; rg.probs = {{"Trending / Directional-Bias", 0.64}, {"Transitional / Breakout", 0.24}, {"Mean-Reversion", 0.07}}; rg.model_variant = "multibook_v1";
    BookCache c1, c2; auto fam = run_scope(r, scope::build(r, "NQ", scope::Mode::Family, "", {}, "MOCK-ts1"), f, c1); auto sg = run_scope(r, scope::build(r, "QQQ", scope::Mode::Single, "", {}, "MOCK-ts1"), f, c2); RegimeBrief rs = rg; rs.model_variant = "single_v1";
    std::string pub_f, pub_s; Say a, b;
    auto& q = fam.books.at("QQQ"); auto& w = fam.scope.books;
    a.t = "MOCK DATA - illustrative only\n1. Scope: family, anchor NQ; heaviest weights NDX " + f2(w[1].weight * 100, 0) + "% and QQQ " + f2(w[0].weight * 100, 0) + "%; snapshot MOCK-ts1.\n2. Regime: Trending / Directional-Bias 64%, Transitional / Breakout 24%, Mean-Reversion 7%.\n3. If dealers are long calls and short puts as assumed, QQQ gamma flip sits at " + f2(*q.flip) + " and sign agreement across books is " + f2(fam.sign_agreement) + ".\n4. QQQ ATM implied volatility is " + f2(q.pricing.atm_iv * 100, 1) + "%.\n5. Top-3 strikes hold " + f2(q.vol.top3_share * 100, 0) + "% of QQQ gamma.\n6. Flip confluence is " + f2(fam.flip_confluence * 100, 0) + "%.\n7. Positioning is inferred, so every flow estimate is conditional.\n8. Confidence " + f2(fam.scope.confidence * 100, 0) + "%; assumes dealers long calls / short puts.\n";
    b.t = "MOCK DATA - illustrative only\n1. Scope: single QQQ; snapshot MOCK-ts1.\n2. Regime: Trending / Directional-Bias 64%, Transitional / Breakout 24%.\n3. If dealers are long calls and short puts as assumed, QQQ gamma flip is " + f2(*sg.books.at("QQQ").flip) + ".\n4. ATM implied volatility " + f2(sg.books.at("QQQ").pricing.atm_iv * 100, 1) + "%.\n5. Structure.\n6. Risk is conditional on inferred positioning.\n7. Confidence limited to one book.\n";
    ScopedAnalysisEngine e1(a, [&](const std::string&, const std::string& p) { pub_f = p; }, r), e2(b, [&](const std::string&, const std::string& p) { pub_s = p; }, r);
    auto rf = e1.run(fam, rg, true), rsr = e2.run(sg, rs, true); if (!rf.validated || !rsr.validated) { std::fprintf(stderr, "scripted text failed validation: %s | %s\n", rf.reason.c_str(), rsr.reason.c_str()); return 1; }
    std::ofstream(argv[1]) << "{\"family\":" << pub_f << ",\"single\":" << pub_s << "}"; return 0; }
