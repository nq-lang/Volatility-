#pragma once
// scoped_analysis.h - scope-aware LLM analysis layer (T2-8).
//   * SCOPED_SYSTEM_PROMPT is spec section 9 VERBATIM; our own numeric-format addendum is a separate string appended after it.
//   * build_input() serialises ONLY in-scope data as structured JSON (never scraped text).
//   * validate() = numeric grounding (every number in the output must trace to the input) + scope-leak (no out-of-scope symbol) + mock disclaimer + section structure.
//   * ScopedAnalysisEngine regenerates with the rejection reasons, falls back to a deterministic computed-values summary if the model keeps failing,
//     caches per (scope key, snapshot_ts) and publishes a scope-badged payload on the `scoped_insight` channel.
// The LLM never computes or invents: it narrates the numbers it was given.
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include "../multibook/engine.h"
#include "structural_insight_engine.h"
namespace vt::insight {
inline const char* SCOPED_SYSTEM_PROMPT = R"PROMPT(ROLE
You are the analysis engine of a structural options-flow terminal. You explain dealer hedging,
hedging flows, regime state, options-market structure, volatility, and risk for the selected
asset, using ONLY the structured data supplied to you in this request.

INPUT YOU RECEIVE
- scope: { mode: single | multi_book | family, anchor, family, books[], weights, snapshot_ts,
           data_quality, model_variant }
- per_book: computed analytics for each included book (GEX profile, flip, walls, delta/vanna/charm
            exposure, IV term structure, skew, OI/volume stats, flows)
- aggregate: family-level structural signals and confidence
- regime: probability distribution across regimes, dominant drivers, trigger states
- meta: sign conventions used, positioning uncertainty, mock_data flag

HARD RULES
1. Never state a number, level, or probability that is not in the input. If a value is missing, say it is unavailable.
2. Respect scope. In single mode, discuss ONLY the selected book. In multi_book/family mode, state
   which books contributed and mention the heaviest-weighted ones. Never import context from books
   not in scope.
3. Dealer positioning is inferred. Always phrase hedging conclusions conditionally
   ("if dealers are net long gamma as assumed ..."). Never claim certainty about who holds what.
4. Report regime as a probability distribution; do not collapse it to a single label without the probabilities.
5. If meta.mock_data is true, begin with "MOCK DATA - illustrative only" and do not present it as actionable.
6. Surface disagreement: if books or signals conflict, say so before giving a synthesis.
7. Match tone to confidence. Low confidence or stale books = tentative language and an explicit caveat.
8. No trade instructions, price targets, or guarantees. Describe structure, levels, conditional behavior, and risks.

OUTPUT STRUCTURE
1. Scope line: mode, anchor, books included (with weights), data freshness.
2. Regime read: top regimes with probabilities, main drivers, trigger status.
3. Dealer hedging & flows: gamma regime vs. flip, nearest walls, hedging-flow ladder, vanna/charm notes, expiry effects.
4. Volatility: term structure, skew, IV vs. realized, event premium.
5. Options-market structure: OI/volume concentration, notable shifts.
6. Cross-book structure (multi_book/family only): confluence, divergence, leveraged-ETF or sector/component effects.
7. Risk: key scenarios, tail/gap risk, liquidity.
8. Confidence & caveats: what would change the read; assumptions used.
Keep each section to 2-4 sentences; lead with the most decision-relevant item.)PROMPT";
// Terminal-specific addendum (kept OUT of the verbatim block): how to quote numbers so post-hoc validation can trace them.
inline const char* SCOPED_ADDENDUM = R"PROMPT(

NUMBER FORMAT (terminal requirement)
Quote numbers exactly as they appear in the input (rounding to fewer decimals is fine; percentages may be written from fractions).
Do not add, subtract, scale or convert values yourself; every derived value you may need is already provided. Number each section "1." to "8." on its own line.)PROMPT";
struct RegimeBrief { std::vector<std::pair<std::string, double>> probs; std::vector<std::string> drivers; std::string trigger; std::string model_variant = "single_v1"; };
inline std::string brief_json(const RegimeBrief& r) {
    std::ostringstream o; o << "{\"model_variant\":\"" << json::esc(r.model_variant) << "\",\"probabilities\":{"; for (size_t i = 0; i < r.probs.size(); ++i) o << (i ? "," : "") << "\"" << json::esc(r.probs[i].first) << "\":" << json::num(r.probs[i].second);
    o << "},\"drivers\":["; for (size_t i = 0; i < r.drivers.size(); ++i) o << (i ? "," : "") << "\"" << json::esc(r.drivers[i]) << "\""; o << "],\"trigger\":\"" << json::esc(r.trigger) << "\"}"; return o.str(); }
// ---- structured input: ONLY in-scope books
inline std::string build_input(const mb::AggregateResult& a, const RegimeBrief& regime, bool mock) {
    using namespace mb; std::ostringstream o; auto n = [](double v) { return json::num(v); }; const auto& s = a.scope; bool single = s.mode == scope::Mode::Single;
    o << "{\"scope\":{\"mode\":\"" << scope::mode_str(s.mode) << "\",\"ticker\":\"" << json::esc(s.ticker) << "\",\"anchor\":\"" << (single ? "" : json::esc(s.anchor)) << "\",\"family\":\"" << (single ? "" : json::esc(s.family))
      << "\",\"snapshot_ts\":\"" << json::esc(s.snapshot_ts) << "\",\"model_variant\":\"" << s.model_variant() << "\",\"data_quality\":{\"confidence\":" << n(single ? 1.0 : s.confidence) << ",\"notes\":[";
    for (size_t i = 0; i < a.notes.size(); ++i) { o << (i ? "," : "") << "\"" << json::esc(a.notes[i]) << "\""; }
    o << "]},\"books\":[";
    if (single) o << "{\"id\":\"" << json::esc(s.ticker) << "\",\"weight\":1}"; else { bool f = true; for (auto& w : s.books) { if (!w.included) continue; o << (f ? "" : ",") << "{\"id\":\"" << json::esc(w.id) << "\",\"priority\":" << w.priority << ",\"weight\":" << n(w.weight) << "}"; f = false; } }
    o << "],\"excluded_books\":["; if (!single) { bool f = true; for (auto& w : s.books) if (!w.included) { o << (f ? "" : ",") << "{\"id\":\"" << json::esc(w.id) << "\",\"reason\":\"" << json::esc(w.excluded_reason) << "\"}"; f = false; } }
    o << "]},\"per_book\":[";
    bool first = true;
    for (auto& kv : a.books) { bool inc = single || [&] { for (auto& w : s.books) if (w.id == kv.first && w.included) return true; return false; }(); if (!inc) continue; const auto& b = kv.second; if (!first) o << ","; first = false;
        o << "{\"id\":\"" << json::esc(b.id) << "\",\"spot\":" << n(b.spot) << ",\"net_gex\":" << n(b.net_gex) << ",\"gamma_flip\":" << (b.flip ? n(*b.flip) : "null") << ",\"call_wall\":" << (b.call_wall ? n(*b.call_wall) : "null") << ",\"put_wall\":" << (b.put_wall ? n(*b.put_wall) : "null")
          << ",\"open_interest_notional\":" << n(b.oi_notional) << ",\"pricing\":{\"atm_iv\":" << n(b.pricing.atm_iv) << ",\"one_sigma_move_pct\":" << n(b.pricing.em_pct * 100) << ",\"rr25\":" << n(b.pricing.rr25) << "}";
        if (b.vol.ok) { o << ",\"vol\":{\"term_slope\":" << n(b.vol.term_slope) << ",\"term_state\":\"" << (b.vol.backwardated ? "backwardated" : "contango") << "\",\"put_wing\":" << n(b.vol.put_wing) << ",\"vrp\":" << (b.vol.vrp ? n(*b.vol.vrp) : "null") << ",\"top3_gamma_share\":" << n(b.vol.top3_share) << "}"; }
        if (b.hedge.ok) { o << ",\"hedge\":{\"vanna_up_shares\":" << n(b.hedge.vanna_up) << ",\"charm_1d_shares\":" << n(b.hedge.charm_1d) << ",\"zero_dte_gamma_share\":" << n(b.hedge.zero_dte_gamma_share) << ",\"ladder\":["; for (size_t i = 0; i < b.hedge.ladder.size(); ++i) o << (i ? "," : "") << "{\"shock_pct\":" << n(b.hedge.ladder[i].shock_pct) << ",\"flow_shares\":" << n(b.hedge.ladder[i].flow_shares) << "}"; o << "]}"; }
        o << "}"; }
    o << "],\"aggregate\":";
    if (single) o << "null"; else { o << "{\"net_gex_sum\":" << n(a.raw_net_gex) << ",\"sign_agreement\":" << n(a.sign_agreement) << ",\"flip_confluence\":" << n(a.flip_confluence) << ",\"confidence\":" << n(s.confidence);
        if (a.hedge && a.hedge->books > 0) { o << ",\"anchor_hedge\":{\"anchor\":\"" << json::esc(s.anchor) << "\",\"unit\":\"anchor contracts\",\"ladder\":["; for (size_t i = 0; i < a.hedge->shock.size(); ++i) o << (i ? "," : "") << "{\"shock_pct\":" << n(a.hedge->shock[i]) << ",\"contracts\":" << n(a.hedge->anchor_contracts[i]) << "}"; o << "]}"; }
        o << "}"; }
    o << ",\"regime\":" << brief_json(regime) << ",\"meta\":{\"dealer_convention\":\"dealers long calls / short puts (assumed, not observed)\",\"positioning\":\"inferred\",\"mock_data\":" << (mock ? "true" : "false") << "}}"; return o.str(); }
// ---- validation
struct Verdict { bool ok = true; std::vector<std::string> untraceable, leaks, problems; std::string reason() const {
    std::string r; if (!untraceable.empty()) { r += "numbers not in the input:"; for (auto& u : untraceable) r += " " + u; r += ". "; } if (!leaks.empty()) { r += "out-of-scope symbols mentioned:"; for (auto& u : leaks) r += " " + u; r += ". "; } for (auto& p : problems) r += p + ". "; return r; } };
namespace detail {
struct Num { double v; int dec; bool pct; double scale; std::string txt; };
inline std::vector<Num> numbers_in(const std::string& t, bool for_output) {
    static const std::regex re(R"((?:^|[^A-Za-z0-9_.])([-+]?[0-9][0-9,]*(?:\.[0-9]+)?)(%|[KMB]\b|bn\b|mm\b)?(DTE|D|d|Δ|σ|x|sigma)?)"); std::vector<Num> out; auto b = std::sregex_iterator(t.begin(), t.end(), re), e = std::sregex_iterator();
    for (auto it = b; it != e; ++it) { std::string tok = (*it)[1].str(), suf = (*it)[2].str(), lab = (*it)[3].str(); if (for_output && !lab.empty()) continue;   // conventional labels: 0DTE, 25D, 1σ, 2x
        std::string clean; for (char c : tok) if (c != ',') clean += c; size_t dot = clean.find('.'); int dec = dot == std::string::npos ? 0 : int(clean.size() - dot - 1); double sc = 1; if (suf == "K") sc = 1e3; else if (suf == "M" || suf == "mm") sc = 1e6; else if (suf == "B" || suf == "bn") sc = 1e9;
        try { out.push_back({std::fabs(std::stod(clean)), dec, suf == "%", sc, tok + suf}); } catch (...) {} }
    return out; }
inline bool matches(const Num& n, const std::vector<double>& pool) {
    double tol = 0.5 * std::pow(10.0, -n.dec) + 1e-9;
    for (double c : pool) { double a = std::fabs(c); for (double f : {1.0, 100.0, 0.01, 1e-3, 1e-6, 1e-9}) { double x = a * f; if (n.pct && f != 100.0 && f != 1.0) continue; double target = n.v * n.scale; double tl = tol * n.scale; if (std::fabs(x - target) <= tl) return true; if (n.scale == 1 && std::fabs(x - n.v) <= tol) return true; } }
    return false; }
inline std::vector<std::string> symbol_tokens(const reg::Registry& r, const std::string& id) { std::vector<std::string> v{id}; for (auto& kv : r.aliases) if (kv.second == id) v.push_back(kv.first); size_t p = id.find('_'); if (p != std::string::npos) { v.push_back(id.substr(0, p)); v.push_back(id.substr(p + 1)); v.push_back(id.substr(0, p) + "/" + id.substr(p + 1)); } return v; }
inline std::set<std::string> allowed_symbols(const reg::Registry& r, const scope::Scope& s) {
    std::set<std::string> a; auto add = [&](const std::string& id) { for (auto& t : symbol_tokens(r, id)) a.insert(t); }; add(s.ticker);
    if (s.mode != scope::Mode::Single) { add(s.anchor); if (auto* f = r.family(s.family)) for (auto& b : f->books) add(b.id); } return a; }
inline std::vector<std::string> leaks_in(const std::string& text, const reg::Registry& r, const scope::Scope& s) {
    auto ok = allowed_symbols(r, s); std::set<std::string> all; for (auto& kv : r.symbols) for (auto& t : symbol_tokens(r, kv.first)) all.insert(t); for (auto& kv : r.aliases) all.insert(kv.first);
    std::vector<std::string> bad; for (auto& sym : all) { if (ok.count(sym) || sym.size() < 2) continue; size_t pos = 0; while ((pos = text.find(sym, pos)) != std::string::npos) { auto isw = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
            bool lb = pos == 0 || !isw(text[pos - 1]), rb = pos + sym.size() >= text.size() || !isw(text[pos + sym.size()]); if (lb && rb) { bad.push_back(sym); break; } pos += sym.size(); } }
    return bad; }
}
inline Verdict validate(const std::string& text, const std::string& input_json, const reg::Registry& r, const scope::Scope& s, bool mock) {
    Verdict v; std::vector<double> pool; for (auto& n : detail::numbers_in(input_json, false)) pool.push_back(n.v);
    std::string body; { std::istringstream is(text); std::string line; while (std::getline(is, line)) { static const std::regex sec(R"(^\s*([1-8])[.)]\s)"); line = std::regex_replace(line, sec, "  "); body += line + "\n"; } }   // section ordinals are structure, not claims
    for (auto& ph : {"S&P 500", "Nasdaq-100", "Nasdaq 100", "Dow 30"}) { size_t p; while ((p = body.find(ph)) != std::string::npos) body.erase(p, std::string(ph).size()); }
    for (auto& n : detail::numbers_in(body, true)) if (!detail::matches(n, pool)) v.untraceable.push_back(n.txt);
    v.leaks = detail::leaks_in(text, r, s);
    if (mock && text.rfind("MOCK DATA - illustrative only", 0) != 0) v.problems.push_back("mock_data is true: the text must begin with \"MOCK DATA - illustrative only\"");
    int found = 0; for (int i = 1; i <= 8; ++i) { std::regex sec("(^|\\n)\\s*" + std::to_string(i) + "[.)]\\s"); if (std::regex_search(text, sec)) ++found; }
    int need = s.mode == scope::Mode::Single ? 7 : 8; if (found < need) v.problems.push_back("expected " + std::to_string(need) + " numbered sections, found " + std::to_string(found));
    v.ok = v.untraceable.empty() && v.leaks.empty() && v.problems.empty(); return v; }
// ---- deterministic fallback: only values taken straight from the computed aggregate (passes validate() by construction; covered by a test)
inline std::string num2(double v) { char b[64]; std::snprintf(b, sizeof b, "%.2f", v); return b; }
inline std::string fallback_text(const mb::AggregateResult& a, const RegimeBrief& rg, bool mock) {
    std::ostringstream o; o.setf(std::ios::fixed); o.precision(2); const auto& s = a.scope; bool single = s.mode == scope::Mode::Single; if (mock) o << "MOCK DATA - illustrative only\n";
    o << "1. Scope: " << scope::mode_str(s.mode) << (single ? "" : ", anchor " + s.anchor) << ", books:"; if (single) o << " " << s.ticker << " 100%"; else for (auto& w : s.books) if (w.included) { o.precision(0); o << " " << w.id << " " << w.weight * 100 << "%"; o.precision(2); }
    o << "; snapshot " << s.snapshot_ts << ". Computed-values summary (language model output did not pass numeric checks).\n2. Regime:"; if (rg.probs.empty()) o << " unavailable"; for (auto& p : rg.probs) { o.precision(0); o << " " << p.first << " " << p.second * 100 << "%"; o.precision(2); } o << ". Probabilities, not a single label.\n";
    o << "3. Dealer hedging: if dealers are long calls / short puts as assumed,"; for (auto& kv : a.books) { bool inc = single || [&] { for (auto& w : s.books) if (w.id == kv.first && w.included) return true; return false; }(); if (!inc) continue; o << " " << kv.first << " net GEX " << kv.second.net_gex << (kv.second.flip ? ", flip " + num2(*kv.second.flip) : std::string(", flip unavailable")) << ";"; } o << "\n";
    o << "4. Volatility: see per-book ATM IV and term state in the panel; values unavailable in this summary.\n5. Options-market structure: concentration figures are in the panel.\n";
    if (!single) { o << "6. Cross-book: sign agreement " << a.sign_agreement << ", flip confluence "; o.precision(0); o << a.flip_confluence * 100 << "%.\n"; o.precision(2); }
    o << (single ? "6" : "7") << ". Risk: positioning is inferred, so every conclusion is conditional.\n" << (single ? "7" : "8") << ". Confidence:"; if (!single) { o.precision(0); o << " " << s.confidence * 100 << "%"; } else o << " single book"; o << ". Assumptions: dealers long calls / short puts, not observed.\n"; return o.str(); }
// ---- engine: generate -> validate -> regenerate with reasons -> fallback; cache by (scope, snapshot_ts)
struct ScopedResult { std::string text; bool validated = false, cached = false, fallback = false; int attempts = 0; std::string reason; };
class ScopedAnalysisEngine {
    LlmClient& llm_; bridge::Sink sink_; const reg::Registry& reg_; std::map<std::string, ScopedResult> cache_; int max_attempts_;
public:
    int llm_calls = 0;
    ScopedAnalysisEngine(LlmClient& l, bridge::Sink s, const reg::Registry& r, int max_attempts = 3) : llm_(l), sink_(std::move(s)), reg_(r), max_attempts_(max_attempts) {}
    static std::string cache_key(const scope::Scope& s) { return s.key(); }        // already includes mode|ticker|family|snapshot_ts|included books
    ScopedResult run(const mb::AggregateResult& a, const RegimeBrief& rg, bool mock) {
        std::string key = cache_key(a.scope); auto it = cache_.find(key); if (it != cache_.end()) { auto r = it->second; r.cached = true; publish(a, r, mock); return r; }
        std::string input = build_input(a, rg, mock), sys = std::string(SCOPED_SYSTEM_PROMPT) + SCOPED_ADDENDUM, user = input; ScopedResult res;
        for (int k = 1; k <= max_attempts_; ++k) { ++llm_calls; res.attempts = k; std::string out = llm_.complete(sys, user); auto v = validate(out, input, reg_, a.scope, mock);
            if (v.ok) { res.text = out; res.validated = true; break; } res.reason = v.reason(); user = input + "\n\nYOUR PREVIOUS ANSWER WAS REJECTED: " + res.reason + "Rewrite it using only the input above."; }
        if (!res.validated) { res.fallback = true; res.text = fallback_text(a, rg, mock); }
        cache_[key] = res; publish(a, res, mock); return res; }
    size_t cached() const { return cache_.size(); }
private:
    void publish(const mb::AggregateResult& a, const ScopedResult& r, bool mock) {
        std::ostringstream o; const auto& s = a.scope; o << "{\"scope\":{\"mode\":\"" << scope::mode_str(s.mode) << "\",\"ticker\":\"" << json::esc(s.ticker) << "\",\"family\":\"" << json::esc(s.family) << "\",\"anchor\":\"" << json::esc(s.anchor) << "\",\"snapshot_ts\":\"" << json::esc(s.snapshot_ts) << "\",\"model_variant\":\"" << s.model_variant() << "\",\"books\":[";
        bool f = true; if (s.mode == scope::Mode::Single) o << "\"" << json::esc(s.ticker) << "\""; else for (auto& w : s.books) if (w.included) { o << (f ? "" : ",") << "\"" << json::esc(w.id) << "\""; f = false; }
        o << "]},\"text\":\"" << json::esc(r.text) << "\",\"validated\":" << (r.validated ? "true" : "false") << ",\"fallback\":" << (r.fallback ? "true" : "false") << ",\"cached\":" << (r.cached ? "true" : "false") << ",\"attempts\":" << r.attempts << ",\"mock\":" << (mock ? "true" : "false") << ",\"reason\":\"" << json::esc(r.reason) << "\"}";
        bridge::publish(sink_, "scoped_insight", o.str()); }
};
}
