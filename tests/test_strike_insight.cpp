#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/multibook/strike_insight.h"
#include "../src/multibook/vol_bridge.h"
using namespace vt; using namespace vt::mb;
// chain: expiries `dtes`; call/put OI profile per expiry given by callbacks
template <class FC, class FP> static ChainSnapshot mk(const std::vector<int>& dtes, FC call_oi, FP put_oi, long long utc = 1792505400LL) {
    ChainSnapshot c; c.underlying = "T"; c.spot = 600; c.ts = TimePoint(std::chrono::seconds(utc)); c.risk_free_rate = 0.04; c.contract_multiplier = 100;
    for (int dte : dtes) for (int i = -20; i <= 20; ++i) { double K = 600 + i * 2.5;
        for (auto t : {OptionType::Call, OptionType::Put}) { OptionQuote q{}; q.type = t; q.strike = K; q.dte_days = dte; q.t_years = (dte == 0 ? 0.25 : dte) / 365.0; q.iv = 0.17 + 0.001 * std::abs(i); q.mid = 1; q.open_interest = t == OptionType::Call ? call_oi(dte, K) : put_oi(dte, K); c.quotes.push_back(q); } }
    return c; }
static double bump(double K, double at, double w) { return std::exp(-std::pow((K - at) / w, 2)); }
int main() {
    // ---- session / DST
    auto s = sess::from_utc(1792505400LL); assert(s.rth && s.et_min == 610 && s.weekday == 1 && s.to_close == 350 && s.since_open == 40 && s.phase == sess::Phase::Open && s.as_of == "Tue 20 Oct 10:10 ET");
    assert(!sess::us_dst(1772953200LL - 1) && sess::us_dst(1772953200LL) && sess::us_dst(1793512800LL - 1) && !sess::us_dst(1793512800LL));
    assert(sess::from_utc(1792505400LL + 3600 * 7).phase == sess::Phase::Closed && sess::expiry_label(s, 3) == "Fri 23 Oct");
    // ---- no 0DTE/1DTE -> Intraday honestly unavailable, Swinging fine
    auto none = mk({7, 14}, [](int, double K) { return 1000 * bump(K, 600, 20); }, [](int, double K) { return 800 * bump(K, 590, 20); });
    auto M0 = compute_insight(none); assert(!M0.intraday.ok && M0.intraday.reason.find("0DTE/1DTE") != std::string::npos && M0.swinging.ok);
    auto I0 = finalize(M0); assert(!I0.intraday.ok && I0.intraday.cards.empty() && I0.swinging.ok && !I0.swinging.cards.empty());
    // ---- consistency: per-strike gamma sums to the horizon net gamma (full revaluation, same convention)
    auto ch = mk({0, 1, 7, 14}, [](int d, double K) { return (d <= 1 ? 3000 : 1500) * bump(K, 605, 6); }, [](int d, double K) { return (d <= 1 ? 2500 : 1200) * bump(K, 592.5, 6); });
    auto M = compute_insight(ch); assert(M.intraday.ok && M.swinging.ok);
    for (auto* H : {&M.intraday, &M.swinging}) { double sum = 0; for (auto& kv : H->gamma_by_strike) sum += kv.second; assert(std::fabs(sum - H->net_gamma) <= 1e-6 * std::fabs(H->net_gamma) + 1e-6); }
    assert(M.intraday.dte_hi == 1 && M.intraday.expiries.size() == 2 && M.swinging.expiries.size() == 2 && M.swinging.expiries[0].dte == 7);
    // ---- cards: sorted by importance, each carries all Greeks, validity and invalidation
    auto I = finalize(M);
    for (auto* H : {&I.intraday, &I.swinging}) { assert(!H->cards.empty()); double prev = 1e300; for (auto& c : H->cards) { assert(c.m.importance <= prev + 1e-12 && c.m.importance >= 0.15); prev = c.m.importance;
        assert(!c.verdict.empty() && c.votes.size() == 5 && c.invalidation.size() >= 3 && !c.validity_until.empty() && !c.validity_limit.empty() && !c.regime_note.empty() && !c.mechanism.empty());
        assert(c.m.gamma > 0 ? c.mechanism.find("LONG gamma") != std::string::npos : c.mechanism.find("SHORT gamma") != std::string::npos);
        assert(c.m.gamma > 0 ? (c.verdict == "Magnet" || c.verdict == "Support" || c.verdict == "Resistance" || c.verdict == "Weak") : (c.verdict == "Acceleration" || c.verdict == "Pivot")); } }
    // playbook + extended Greeks/distribution on every card
    for (auto* H : {&I.intraday, &I.swinging}) for (auto& c : H->cards) { auto& pb = c.pb; assert(!pb.style.empty() && !pb.entry.empty() && !pb.stop.empty() && !pb.target.empty() && !pb.hold.empty() && pb.edge.size() >= 5 && pb.rr >= 0 && pb.p_target >= 0 && pb.p_target <= 1 && pb.capture_pts >= 0);
        assert(c.m.p_touch >= c.m.p_beyond && c.m.p_beyond >= 0 && c.m.p_touch <= 1 && c.m.gamma_share > 0 && c.m.gamma_share <= 1 && c.m.iv_k > 0);
        if (c.contested) assert(pb.style.find("Stand aside") != std::string::npos);
        if (c.verdict == "Acceleration" && !c.contested) assert(pb.style.find("Momentum") != std::string::npos);
        if (c.verdict == "Magnet" && !c.contested) assert(pb.style.find("Pin") != std::string::npos);
        if (c.verdict == "Resistance" && !c.contested && pb.style.find("rejection") != std::string::npos) assert(pb.target_px < c.m.strike && pb.stop_px > c.m.strike);
        if (c.verdict == "Support" && !c.contested && pb.style.find("rejection") != std::string::npos) assert(pb.target_px > c.m.strike && pb.stop_px < c.m.strike); }
    // call-heavy strike above spot -> long gamma; put-heavy below -> short gamma (dealers long calls / short puts)
    bool pos_up = false, neg_dn = false; for (auto& c : I.swinging.cards) { if (c.m.strike > 600 && c.m.gamma > 0) pos_up = true; if (c.m.strike < 600 && c.m.gamma < 0) neg_dn = true; } assert(pos_up && neg_dn);
    // ---- vol regime changes the vanna vote only through the supplied feed
    auto& v0 = I.intraday.cards[0].votes[3]; assert(v0.greek == "Vanna" && v0.v == 0 && v0.text.find("no vol index feed") != std::string::npos);
    VolIn vi; vi.available = true; vi.id = "VIX"; vi.regime = "stress"; vi.trend = 1; auto I2 = finalize(M, vi);
    bool voted = false; for (auto& c : I2.intraday.cards) if (c.votes[3].v != 0) voted = true; assert(voted);
    assert(I2.intraday.cards[0].regime_note.find("VIX regime: stress") != std::string::npos && I2.intraday.regime_headline.find("VIX stress, rising") != std::string::npos);
    // metrics are vol-feed independent (cache-safe): same metrics, different finalize
    assert(I.intraday.cards[0].m.gamma == I2.intraday.cards[0].m.gamma);
    // ---- cross-tab reconciliation: opposite-sign gamma at the same strike must be explained, not hidden
    auto cx = mk({1, 7}, [](int d, double K) { return d == 1 ? 200 * bump(K, 600, 5) : 3000 * bump(K, 600, 5); }, [](int d, double K) { return d == 1 ? 3000 * bump(K, 600, 5) : 200 * bump(K, 600, 5); });
    auto Ix = finalize(compute_insight(cx)); assert(!Ix.reconcile.empty() && Ix.reconcile[0].find("differ") != std::string::npos);
    // ---- time passing: gamma of a 0DTE strike fades within the session (color negative, short half-life)
    for (auto& c : I.intraday.cards) if (c.m.dom_dte == 0 && c.m.gamma > 0) { assert(c.m.half_life_min > 0 && c.m.half_life_min <= 390 && c.validity_until.find("ET") != std::string::npos); }
    // ---- after hours: no charm-to-close, still valid
    auto ah = finalize(compute_insight(mk({1, 7}, [](int, double K) { return 1000 * bump(K, 605, 6); }, [](int, double K) { return 900 * bump(K, 595, 6); }, 1792505400LL + 3600 * 8)));
    assert(!ah.intraday.cards.empty() && ah.intraday.cards[0].m.charm_close == 0);
    auto js = insight_json(I2); assert(js.find("\"intraday\":{\"name\":\"Intraday\",\"ok\":true") != std::string::npos && js.find("\"swinging\"") != std::string::npos && js.find("\"votes\":[") != std::string::npos && js.find("\"invalidation\":[") != std::string::npos);
    // registry -> VolIn mapping: QQQ/NQ -> VXN, SPX/ES -> VIX, unlinked -> none; missing feed is explicit
    { auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json"); assert(r.vol_index_for("QQQ") == "VXN" && r.vol_index_for("nq") == "VXN" && r.vol_index_for("SPX") == "VIX" && r.vol_index_for("ES") == "VIX" && r.vol_index_for("GC").empty());
      auto none = vols_for_books(r, {}); assert(!none.at("QQQ").available && none.at("QQQ").reason.find("no VXN feed") != std::string::npos && none.count("GC") == 0);
      volidx::Analysis a; a.id = "VXN"; a.available = true; a.level = 22; a.trend = 1; a.regime = volidx::Regime::Elevated; auto m = vols_for_books(r, {{"VXN", a}}); assert(m.at("NDX").available && m.at("NQ").regime == "elevated" && !m.at("SPX").available); }
    std::puts("test_strike_insight OK"); return 0;
}
