#pragma once
// chart_snapshot.h - Chart Factors ModelSnapshot (schema/chart_factors.fbs, JSON mirror). The chart renders ONLY what the existing models emit:
// levels (flip / walls / top |GEX| / expected-move), clustered with stable ids and confluence tags; probability markers from the strike
// insight cards; the strike field (per-strike metric already computed by the models); the hedge curve from the dealer ladder; HUD chips and
// "The Read" assembled from slots that exist (an unavailable slot is listed as unavailable, never invented). Same epoch as the section payloads.
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include "../multibook/engine.h"
namespace vt::cf {
using namespace vt::mb;
struct AxisCtx { std::string symbol; double spot = 0, tick = 0.01; bool fam = false; std::string primary; const LensBook* pb = nullptr; };
inline std::optional<double> to_axis(const AxisCtx& A, const LensBook& b, double k) {
    if (A.fam) { if (!b.convertible) return std::nullopt; return round_tick(k * b.ratio + b.basis, A.tick); }
    if (b.id == A.primary) return k;
    if (!b.convertible || !A.pb || !A.pb->convertible || A.pb->ratio == 0) return std::nullopt;
    return (to_futures(b, k, 0) - A.pb->basis) / A.pb->ratio;
}
inline std::string px(double v, double tick) { std::ostringstream o; o.setf(std::ios::fixed); int d = tick >= 0.25 ? 0 : 2; o.precision(d); double a = std::fabs(v); long long ip = (long long)a; std::string s = std::to_string(ip); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ","); o << (v < 0 ? "-" : "") << s; if (d) { std::ostringstream f; f.setf(std::ios::fixed); f.precision(d); f << (a - ip); s = f.str().substr(1); o << s; } return o.str(); }
inline std::string money(double v) { double a = std::fabs(v); std::ostringstream o; o.setf(std::ios::fixed); o.precision(a >= 1e9 ? 2 : a >= 1e6 ? 0 : 0); const char* sg = v < 0 ? "-" : ""; if (a >= 1e9) o << "$" << sg << a / 1e9 << "B"; else if (a >= 1e6) o << "$" << sg << a / 1e6 << "M"; else o << "$" << sg << a / 1e3 << "K"; return o.str(); }
inline std::string fix1(double v) { std::ostringstream o; o.setf(std::ios::fixed); o.precision(1); o << v; return o.str(); }
struct LSrc { std::string book; double k = 0, v = 0; };
inline std::string fmtk(double k) { std::ostringstream o; o.setf(std::ios::fixed); bool ig = std::fabs(k - std::round(k)) < 1e-9; o.precision(ig ? 0 : 2); long long ip = (long long)std::fabs(k); std::string s = std::to_string(ip); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ","); if (!ig) { std::ostringstream f; f.setf(std::ios::fixed); f.precision(2); f << (std::fabs(k) - ip); s += f.str().substr(1); } return (k < 0 ? "-" : "") + s; }
struct Lvl { std::vector<LSrc> src; double price = 0; std::vector<std::string> tags, books; double gex = 0, p_touch = -1, importance = 0; std::string verdict; bool has_gex = false; };
inline std::string chart_json(const AggregateResult& res, const reg::Registry& r) {
    (void)r; if (!res.lens || res.scope.mode == scope::Mode::Single) return "{\"epoch\":\"" + res.epoch + "\",\"ok\":false,\"reason\":\"Chart Factors reads Multi-Book / Family scopes (Single-Book keeps the original single_v1 reading)\"}";
    const Lens& L = *res.lens; const auto& sc = res.scope; AxisCtx A; A.fam = sc.mode == scope::Mode::Family; A.primary = sc.ticker; A.tick = A.fam ? L.tick : 0.01;
    for (auto& b : L.books) if (b.id == sc.ticker) A.pb = &b;
    A.symbol = A.fam ? L.anchor : sc.ticker; A.spot = A.fam ? L.anchor_px : (A.pb ? A.pb->spot : 0);
    std::ostringstream o; auto n = [](double v) { return json::num(v); }; auto q = [](const std::string& s) { return "\"" + json::esc(s) + "\""; };
    o << "{\"epoch\":\"" << res.epoch << "\",\"ok\":" << (A.spot > 0 ? "true" : "false") << ",\"as_of\":" << q(res.books.empty() ? "" : res.books.begin()->second.insight.ses.as_of) << ",\"axis\":{\"symbol\":" << q(A.symbol) << ",\"spot\":" << n(A.spot) << ",\"tick\":" << n(A.tick) << ",\"family\":" << (A.fam ? "true" : "false") << "},\"scope\":{\"mode\":\"" << scope::mode_str(sc.mode) << "\",\"model_variant\":\"" << sc.model_variant() << "\"}";
    if (A.spot <= 0) { o << ",\"reason\":\"no axis price (anchor or primary book unavailable)\"}"; return o.str(); }
    std::set<std::string> included; for (auto& w : sc.books) if (w.included) included.insert(w.id);
    // ---------- collect raw levels in axis space
    std::vector<Lvl> raw; double em_up = 0, em_dn = 0; bool has_em = false;
    for (auto& b : L.books) { if (!included.count(b.id)) continue;
        for (auto& lv : b.levels) { auto ax = to_axis(A, b, lv.strike); if (!ax) continue; Lvl x; x.price = *ax; x.books = {b.id}; x.src = {{b.id, lv.strike, 0}};
            if (lv.label == "Gamma Flip") x.tags = {"Flip"}; else if (lv.label == "Call Wall") { x.tags = {"CallWall"}; x.gex = lv.value; x.has_gex = true; } else if (lv.label == "Put Wall") { x.tags = {"PutWall"}; x.gex = lv.value; x.has_gex = true; } else { x.tags = {"GEX"}; x.gex = lv.value; x.has_gex = true; }
            for (auto& sr : x.src) { sr.v = std::fabs(x.gex); }
            raw.push_back(x); }
        if (b.id == (A.fam ? (L.books.empty() ? "" : L.books[0].id) : A.primary) || (A.fam && !has_em)) for (auto& pl : b.pricing) { auto ax = to_axis(A, b, pl.strike); if (!ax) continue;
            if (pl.label == "Exp Move +1s") { em_up = *ax; has_em = true; }
            if (pl.label == "Exp Move -1s") { em_dn = *ax; } } }
    // card-derived: magnets / p_touch per strike (primary book's horizon cards)
    struct Cd { double price, p_touch, imp, gamma_share; std::string verdict, hz; bool contested; double sigma; std::string book; double gamma; int dte; double oi; };
    std::vector<Cd> cds;
    for (auto& b : L.books) { if (!included.count(b.id)) continue; for (auto hz : {0, 1}) { const Horizon& H = hz ? b.insight.swinging : b.insight.intraday; if (!H.ok) continue;
        for (auto& c : H.cards) { auto ax = to_axis(A, b, c.m.strike); if (!ax) continue; cds.push_back({*ax, c.m.p_touch, c.m.importance, c.m.gamma_share, c.verdict, hz ? "Swinging" : "Intraday", c.contested, c.m.sigma_dist, b.id, c.m.gamma, c.m.dom_dte, c.m.oi}); } } }
    // ---------- cluster levels (tolerance = max(2 ticks, 0.04% of spot)), stable id = kind-tags@rounded price
    double tol = std::max(2 * A.tick, 0.0004 * A.spot); std::sort(raw.begin(), raw.end(), [](auto& a, auto& b) { return a.price < b.price; });
    std::vector<Lvl> cl; for (auto& x : raw) { if (!cl.empty() && x.price - cl.back().price <= tol) { auto& c = cl.back(); double w = c.books.size(); c.price = (c.price * w + x.price) / (w + 1); for (auto& t : x.tags) if (std::find(c.tags.begin(), c.tags.end(), t) == c.tags.end()) c.tags.push_back(t); for (auto& bk : x.books) if (std::find(c.books.begin(), c.books.end(), bk) == c.books.end()) c.books.push_back(bk); for (auto& sr : x.src) c.src.push_back(sr); if (x.has_gex) { c.gex += x.gex; c.has_gex = true; } }
        else cl.push_back(x); }
    double maxg = 0; for (auto& c : cl) maxg = std::max(maxg, std::fabs(c.gex));
    o << ",\"levels\":[";
    for (size_t i = 0; i < cl.size(); ++i) { auto& c = cl[i]; c.price = round_tick(c.price, A.tick); bool above = c.price > A.spot;
        int ldte = -1; double lexp = 0, bimp = -1;
        for (auto& cd : cds) if (std::fabs(cd.price - c.price) <= tol) { c.verdict = cd.verdict; c.p_touch = std::max(c.p_touch, cd.p_touch); c.importance = std::max(c.importance, cd.imp); if (cd.imp > bimp) { bimp = cd.imp; ldte = cd.dte; lexp = cd.gamma; } }
        double sc2 = c.has_gex && maxg > 0 ? std::fabs(c.gex) / maxg : c.importance; std::string strength = sc2 >= 0.66 ? "STRONG" : sc2 >= 0.33 ? "MODERATE" : "WEAK";
        std::string tg; for (size_t k = 0; k < c.tags.size(); ++k) tg += (k ? "+" : "") + c.tags[k];
        std::string dir = above ? "v" : "^"; std::string side = c.has_gex ? (c.gex < 0 ? "support" : "resistance") : (above ? "resistance" : "support");
        std::stable_sort(c.src.begin(), c.src.end(), [](auto& a, auto& b2) { return a.v > b2.v; });
        std::string asset, srcj = "["; { const LensBook* db = nullptr; if (!c.src.empty()) for (auto& lb : L.books) if (lb.id == c.src[0].book) db = &lb;
            if (!c.src.empty()) { auto& s0 = c.src[0]; asset = (s0.book == A.symbol ? A.symbol : s0.book + " $" + fmtk(s0.k) + " – " + A.symbol) + (s0.book == A.symbol ? " $" + px(c.price, A.tick) : " $" + px(c.price, A.tick)); if (c.books.size() > 1) asset += " +" + std::to_string(c.books.size() - 1); }
            for (size_t k2 = 0; k2 < c.src.size(); ++k2) { double fv = 0; for (auto& lb : L.books) if (lb.id == c.src[k2].book) { auto ax2 = to_axis(A, lb, c.src[k2].k); if (ax2) fv = *ax2; } srcj += std::string(k2 ? "," : "") + "{\"book\":" + q(c.src[k2].book) + ",\"k\":" + n(c.src[k2].k) + ",\"f\":" + n(fv) + "}"; } (void)db; srcj += "]"; }
        // outcome of an approach to this level (rule-based on the models' own outputs; assumes dealers long calls / short puts)
        double emw = has_em ? std::fabs(em_up - A.spot) : 0.01 * A.spot; bool nearS = std::fabs(c.price - A.spot) <= 0.6 * emw; bool isFlip = std::find(c.tags.begin(), c.tags.end(), "Flip") != c.tags.end();
        std::string outc, why;
        if (c.verdict == "Magnet") { outc = "MAGNET"; why = "price is drawn in; touch likely"; }
        else if (isFlip || (c.has_gex && c.gex < 0) || c.verdict == "Acceleration") { outc = "AMPLIFIER"; why = isFlip ? "regime flips beyond; moves extend" : "short-gamma: a break accelerates"; }
        else if (c.has_gex && c.gex > 0 && c.p_touch >= 0.55 && nearS) { outc = "PINNING"; why = "long-gamma near spot: price pins"; }
        else { outc = "REJECTION"; why = "long-gamma wall: approach tends to reject"; }
        std::string pill = asset + " · REVERT " + (above ? "▼" : "▲") + " [" + tg + "]";
        std::string label = std::string("REVERT ") + (above ? "▼" : "▲") + " " + px(c.price, A.tick) + " [" + tg + "]";
        std::string bks; for (size_t k = 0; k < c.books.size(); ++k) bks += (k ? ", " : "") + c.books[k];
        std::string desc = tg + " at " + px(c.price, A.tick) + (c.has_gex ? " carries " + money(c.gex) + " dealer exposure per 1%" : "") + ". " + (c.verdict.empty() ? "" : "Strike insight: " + c.verdict + (c.p_touch >= 0 ? ", " + std::to_string((int)std::lround(c.p_touch * 100)) + "% risk-neutral touch chance. " : ". ")) + "Strength " + strength + ". Books: " + bks + ".";
        o << (i ? "," : "") << "{\"id\":\"L" << (long long)std::llround(c.price / std::max(A.tick, 1e-9)) << "\",\"price\":" << n(c.price) << ",\"kind\":\"" << (c.tags.size() ? c.tags[0] : "") << "\",\"side\":\"" << side << "\",\"dir\":\"" << dir << "\",\"tags\":["; for (size_t k = 0; k < c.tags.size(); ++k) o << (k ? "," : "") << q(c.tags[k]);
        o << "],\"label\":" << q(label) << ",\"pill\":" << q(pill) << ",\"asset\":" << q(asset) << ",\"src\":" << srcj << ",\"outcome\":\"" << outc << "\",\"why\":" << q(why) << ",\"desc\":" << q(desc) << ",\"gex\":" << (c.has_gex ? n(c.gex) : "null") << ",\"score\":" << n(sc2) << ",\"p_touch\":" << (c.p_touch >= 0 ? n(c.p_touch) : "null") << ",\"strength\":\"" << strength << "\",\"verdict\":" << q(c.verdict) << ",\"band\":" << n(tol) << ",\"exp\":" << n(c.has_gex ? c.gex : lexp) << ",\"dte\":" << (ldte >= 0 ? std::to_string(ldte) : std::string("null")) << ",\"books\":["; for (size_t k = 0; k < c.books.size(); ++k) o << (k ? "," : "") << q(c.books[k]);
        o << "],\"chip\":" << q(px(c.price, A.tick) + (c.has_gex ? " · " + money(c.gex) : "") + " · " + (c.price >= A.spot ? "+" : "") + px(c.price - A.spot, A.tick)) << "}"; }
    o << "],\"em\":{\"ok\":" << (has_em ? "true" : "false") << ",\"up\":" << n(em_up) << ",\"dn\":" << n(em_dn) << "}";
    // ---------- probability markers: LIKELY = highest touch chance away from spot; MAGNET = Magnet verdicts
    o << ",\"markers\":["; bool first = true; const Cd* best = nullptr; for (auto& cd : cds) if (std::fabs(cd.price - A.spot) > tol && (!best || cd.p_touch > best->p_touch)) best = &cd;
    auto tagsAt = [&](double price) { for (auto& c : cl) if (std::fabs(c.price - price) <= tol) { std::string t; for (size_t k = 0; k < c.tags.size(); ++k) t += (k ? "+" : "") + c.tags[k]; return t; } return std::string(); };
    auto magnetAt = [&](double price) { double m = -1; for (auto& cd : cds) if (cd.verdict == "Magnet" && !cd.contested && std::fabs(cd.price - price) <= tol) m = std::max(m, cd.p_touch); return m; };
    if (best) { bool up = best->price > A.spot; double mg = magnetAt(best->price); std::string lh = best->hz == "Swinging" ? "swinging" : "intraday";
        o << "{\"kind\":\"likely\",\"price\":" << n(best->price) << ",\"p\":" << n(best->p_touch) << ",\"up\":" << (up ? "true" : "false") << ",\"label\":" << q(std::string("LIKELY ") + (up ? "▲ " : "▼ ") + px(best->price, A.tick) + " · " + std::to_string((int)std::lround(best->p_touch * 100)) + "% touch (" + lh + (mg >= 0 ? "; magnet " + std::to_string((int)std::lround(mg * 100)) + "%" : "") + ")") << "}"; first = false; }
    std::set<long long> seenm; for (auto& cd : cds) if (cd.verdict == "Magnet" && !cd.contested) { long long key = std::llround(cd.price / std::max(A.tick, 1e-9)); if (!seenm.insert(key).second) continue; bool up = cd.price > A.spot; std::string st = cd.imp >= 0.66 ? "STRONG" : cd.imp >= 0.33 ? "MODERATE" : "WEAK";
        std::string tg2 = tagsAt(cd.price); o << (first ? "" : ",") << "{\"kind\":\"magnet\",\"price\":" << n(cd.price) << ",\"p\":" << n(cd.p_touch) << ",\"up\":" << (up ? "true" : "false") << ",\"strength\":\"" << st << "\",\"label\":" << q(std::string("◎ MAGNET ") + (up ? "▲ " : "▼ ") + px(cd.price, A.tick) + " ±" + px(tol, A.tick) + " · " + std::to_string((int)std::lround(cd.p_touch * 100)) + "%" + (tg2.empty() ? "" : " · " + tg2) + " · " + st) << "}"; first = false; }
    o << "]";
    // ---------- strike field: per metric, per strike (axis space), signed values from the models
    double volk = 1.0; std::string volid; if (res.volctx.ok && !res.volctx.idx.empty()) { volk = 0.8 + 0.6 * std::min(100.0, std::max(0.0, res.volctx.idx.front().pct)) / 100.0; volid = res.volctx.idx.front().id; }
    o << ",\"field\":{\"default\":\"struct\",\"vol_k\":" << n(volk) << ",\"vol_id\":" << q(volid) << ",\"metrics\":[";
    bool firstM = true;
    // gex: every strike of the horizon's dealer gamma; other Greeks: structural (card) strikes only (that is all the models emit per strike)
    for (int hz = 0; hz < 2; ++hz) {
        std::map<std::string, std::vector<std::pair<double, double>>> ms; std::map<std::string, std::vector<std::string>> mbk; std::vector<std::pair<double, double>> gx, sx; std::vector<std::string> gb;
        for (auto& b : L.books) { if (!included.count(b.id)) continue; auto bi = res.books.find(b.id); if (bi == res.books.end()) continue; const HorizonMetrics& HM = hz ? bi->second.insight.swinging : bi->second.insight.intraday; if (!HM.ok) continue;
            const Dist& DD = bi->second.dist; double hmax = 0; if (DD.ok) for (double h : DD.hist) hmax = std::max(hmax, h);
            for (auto& kv : HM.gamma_by_strike) { auto ax = to_axis(A, b, kv.first); if (ax) { gx.push_back({*ax, kv.second}); gb.push_back(b.id);
                // structural liquidity: dealer exposure x risk-neutral touch chance x RND density (neutral 0.5 when the distribution is unavailable)
                double dn = 0.5, pt = 0.5; if (DD.ok && DD.dx > 0 && hmax > 0 && !DD.hist.empty()) { double ix = (kv.first - DD.x0) / DD.dx; if (ix >= 0 && ix < (double)DD.hist.size()) { dn = DD.hist[(size_t)ix] / hmax; double cdf = 0; for (size_t i2 = 0; i2 < (size_t)ix; ++i2) cdf += DD.hist[i2]; pt = std::min(1.0, 2.0 * std::min(cdf, 1.0 - cdf)); } else { dn = 0; pt = 0.02; } }
                sx.push_back({*ax, kv.second * (0.5 + 0.5 * pt) * (0.6 + 0.4 * dn)}); } }
            for (auto& c : HM.cards) { auto ax = to_axis(A, b, c.strike); if (!ax) continue;
                ms["vanna"].push_back({*ax, c.vanna_up}); ms["charm"].push_back({*ax, c.charm_day}); ms["vomma"].push_back({*ax, c.vomma}); ms["vega"].push_back({*ax, c.vega}); ms["delta"].push_back({*ax, c.delta_sh}); ms["ptouch"].push_back({*ax, c.p_touch}); ms["oi"].push_back({*ax, c.oi}); for (auto* k : {"vanna", "charm", "vomma", "vega", "delta", "ptouch", "oi"}) mbk[k].push_back(b.id); } }
        auto emit = [&](const std::string& id, const std::string& label, const std::vector<std::pair<double, double>>& v, const std::vector<std::string>& bk, bool all) {
            if (v.empty()) { return; }
            std::vector<double> mag; for (auto& p : v) mag.push_back(std::fabs(p.second)); std::sort(mag.begin(), mag.end()); double sc3 = mag[std::min(mag.size() - 1, (size_t)std::floor(0.98 * (mag.size() - 1) + 0.5))]; if (sc3 <= 0) { sc3 = mag.back(); }
            if (sc3 <= 0) { return; }
            o << (firstM ? "" : ",") << "{\"id\":\"" << id << "\",\"hz\":\"" << (hz ? "swinging" : "intraday") << "\",\"label\":" << q(label) << ",\"all\":" << (all ? "true" : "false") << ",\"scale\":" << n(sc3) << ",\"strikes\":["; firstM = false;
            for (size_t i = 0; i < v.size(); ++i) { o << (i ? "," : "") << "{\"price\":" << n(v[i].first) << ",\"v\":" << n(v[i].second) << ",\"book\":" << q(bk[i]) << "}"; } o << "]}"; };
        emit("struct", "Structural liquidity $ (dealer flow x touch prob x distribution)", sx, gb, true); emit("gex", "Net GEX $/1% (every strike)", gx, gb, true);
        emit("vanna", "Vanna (structural strikes)", ms["vanna"], mbk["vanna"], false); emit("charm", "Charm sh/day (structural strikes)", ms["charm"], mbk["charm"], false); emit("vomma", "Vomma (structural strikes)", ms["vomma"], mbk["vomma"], false);
        emit("vega", "Vega $/vol pt (structural strikes)", ms["vega"], mbk["vega"], false); emit("delta", "Dealer delta hedge sh (structural strikes)", ms["delta"], mbk["delta"], false); emit("ptouch", "Touch probability (structural strikes)", ms["ptouch"], mbk["ptouch"], false); emit("oi", "Open interest (structural strikes)", ms["oi"], mbk["oi"], false);
    }
    o << "]}";
    // ---------- hedge curve from the aggregate ladder (axis price, $ hedge flow, cost per 1%)
    o << ",\"hedge\":";
    if (res.hedge && res.hedge->books > 0 && res.hedge->shock.size() >= 3) { auto& h = *res.hedge; o << "{\"prices\":["; for (size_t i = 0; i < h.shock.size(); ++i) o << (i ? "," : "") << n(A.spot * (1 + h.shock[i] / 100)); o << "],\"notional\":["; for (size_t i = 0; i < h.notional.size(); ++i) o << (i ? "," : "") << n(h.notional[i]);
        o << "],\"cost1pct\":["; for (size_t i = 0; i < h.shock.size(); ++i) { size_t a = i ? i - 1 : 0, b = i + 1 < h.shock.size() ? i + 1 : i; double ds = h.shock[b] - h.shock[a]; o << (i ? "," : "") << n(ds != 0 ? std::fabs((h.notional[b] - h.notional[a]) / ds) : 0); } o << "]}"; }
    else o << "null";
    // ---------- HUD chips + Read (only slots that exist)
    const Horizon* H = nullptr; const BookAnalytics* ab = nullptr; { auto it = res.books.find(A.fam ? (L.books.empty() ? "" : L.books[0].id) : A.primary); if (it != res.books.end()) ab = &it->second; }
    o << ",\"hud\":[";
    double netg = res.raw_net_gex; std::string tone = netg < 0 ? "neg" : "pos";
    o << "{\"k\":\"gamma\",\"tone\":\"" << tone << "\",\"text\":" << q(std::string("GAMMA ") + (netg < 0 ? "NEGATIVE " : "POSITIVE ") + money(netg) + (netg < 0 ? " amplified — trends run" : " dampened — moves fade")) << ",\"tip\":" << q("Summed dealer GEX per 1% of the included books; the 30-day percentile needs a stored history and is unavailable") << "}";
    if (ab && ab->pricing.ok) o << ",{\"k\":\"em\",\"tone\":\"neu\",\"text\":" << q("EM ±" + px(A.spot * ab->pricing.em_pct, A.tick) + " (" + std::to_string(ab->pricing.ref_dte) + "d)") << ",\"tip\":\"1-sigma implied move from ATM IV; the 'used %' needs the session range from the bar feed\"}";
    o << ",{\"k\":\"vrp\",\"tone\":\"off\",\"text\":\"VRP n/a\",\"tip\":\"needs a realised-volatility feed (not connected)\"}";
    o << ",{\"k\":\"bias\",\"tone\":\"off\",\"text\":\"BIAS n/a\",\"tip\":\"no directional-bias model is defined in the terminal yet\"}";
    o << ",{\"k\":\"event\",\"tone\":\"off\",\"text\":\"EVENT n/a\",\"tip\":\"no event-calendar feed connected\"}";
    if (res.volctx.ok) { auto& v = res.volctx.idx.front(); o << ",{\"k\":\"vol\",\"tone\":\"" << (v.regime == "stress" ? "neg" : v.regime == "elevated" ? "warn" : "neu") << "\",\"text\":" << q(v.id + " " + fix1(v.level) + " " + v.regime) << ",\"tip\":" << q(res.volctx.headline) << "}"; }
    o << ",{\"k\":\"conf\",\"tone\":\"neu\",\"text\":" << q("CONF " + std::to_string((int)std::lround(sc.confidence * 100)) + "%") << ",\"tip\":\"data / model confidence\"}]";
    o << ",\"read\":["; bool fr = true; auto tok = [&](const std::string& t, bool e) { o << (fr ? "" : ",") << "{\"t\":" << q(t) << ",\"e\":" << (e ? "true" : "false") << "}"; fr = false; };
    if (netg < 0) { tok("Dealers are ", false); tok("amplifying", true); tok(" moves — trends can run; don't step in front of momentum. ", false); } else { tok("Dealers are ", false); tok("dampening", true); tok(" moves — rallies and dips tend to fade toward the largest strikes. ", false); }
    if (ab && ab->pricing.ok) { tok("Options price ", false); tok("±" + px(A.spot * ab->pricing.em_pct, A.tick) + " pts", true); tok(" to " + std::to_string(ab->pricing.ref_dte) + " DTE. ", false); }
    if (res.hedge && res.hedge->books > 0) { double ch = res.hedge->charm_1d_c; tok("Time decay is a ", false); tok(ch >= 0 ? "tailwind up" : "tailwind down", true); tok(" (charm " + std::string(ch >= 0 ? "+" : "") + std::to_string((long long)std::llround(ch)) + " contracts/day). ", false); }
    if (res.volctx.ok) { tok(res.volctx.idx.front().id + " is ", false); tok(res.volctx.idx.front().regime, true); tok(res.volctx.idx.front().trend > 0 ? ", rising." : res.volctx.idx.front().trend < 0 ? ", falling." : ".", false); }
    o << "],\"unavail\":[\"VRP (no realised-vol feed)\",\"range used vs EM (no bar feed)\",\"resting-book lean (no L2 depth feed)\",\"30d gamma percentile (no stored history)\"]";
    (void)H; o << "}"; return o.str();
}
}
