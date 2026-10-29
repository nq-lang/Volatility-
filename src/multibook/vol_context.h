#pragma once
// vol_context.h - one terminal-wide reading of the volatility indices for the books in scope (workfile C-3): Hedge Flow, Dealer Flow
// sidebar and Pricing all print THIS object, so they cannot disagree. VIX applies to S&P books, VXN to Nasdaq books (registry vol_links).
// Nothing is guessed: no feed -> available=false with the stated reason; interpretation uses the aggregate hedge ladder and dealer vomma.
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include "../bridge/json_util.h"
#include "strike_insight.h"
#include "hedge_analytics.h"
namespace vt::mb {
struct VolCtxIdx { std::string id, regime; double level = 0, pct = 0; int trend = 0; std::vector<std::string> books; };
struct VolCtx {
    bool ok = false; std::string reason, headline, hedge, vanna, vomma, divergence; std::vector<VolCtxIdx> idx;
    std::vector<std::string> sidebar;
};
namespace vc {
inline std::string ord(int n) { int m = n % 100; const char* s = (m >= 11 && m <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th"; return std::to_string(n) + s; }
inline std::string trend_s(int t) { return t > 0 ? "rising" : t < 0 ? "falling" : "flat"; }
}
// vols: book id -> VolIn (all registry-linked books). ids: books in scope. h: aggregate hedge (anchor-equivalent contracts), may be null.
template <class BookMap> inline VolCtx build_volctx(const std::map<std::string, VolIn>& vols, const std::vector<std::string>& ids, const AggLadder* h, const BookMap& books) {
    VolCtx c; std::map<std::string, VolCtxIdx> by; std::string missing;
    for (auto& id : ids) { auto it = vols.find(id); if (it == vols.end()) continue; const VolIn& v = it->second;
        if (!v.available) { if (missing.empty()) missing = v.reason.empty() ? "no " + v.id + " feed connected" : v.reason; continue; }
        auto& x = by[v.id]; x.id = v.id; x.regime = v.regime; x.level = v.level; x.pct = v.pct_rank * 100; x.trend = v.trend; x.books.push_back(id); }
    for (auto& kv : by) c.idx.push_back(kv.second);
    if (c.idx.empty()) { c.reason = missing.empty() ? "no volatility index is linked to the books in scope" : missing; return c; }
    c.ok = true; const VolCtxIdx& p = c.idx.front(); std::string bl; for (size_t i = 0; i < p.books.size(); ++i) bl += (i ? "/" : "") + p.books[i];
    { std::ostringstream o; o.setf(std::ios::fixed); o.precision(2); o << p.id << " " << p.level << " (" << p.regime << ", " << vc::ord((int)std::lround(p.pct)) << " percentile, " << vc::trend_s(p.trend) << ") governs " << bl; c.headline = o.str(); }
    double vanna_up = h ? h->vanna_up_c : 0, vanna_dn = h ? h->vanna_dn_c : 0;
    std::string& s = c.vanna;
    if (!h) s = "Vanna flow unavailable: no aggregate hedge for this scope.";
    else if (p.trend > 0) s = "Vol is rising: a +1 pt IV move makes dealers " + std::string(vanna_up < 0 ? "SELL" : "BUY") + " " + std::to_string((long long)std::llround(std::fabs(vanna_up))) + " contracts, " + (vanna_up < 0 ? "adding to downside pressure." : "which cushions the move.");
    else if (p.trend < 0) s = "Vol is falling: a -1 pt IV move makes dealers " + std::string(vanna_dn > 0 ? "BUY" : "SELL") + " " + std::to_string((long long)std::llround(std::fabs(vanna_dn))) + " contracts, " + (vanna_dn > 0 ? "a supportive flow while vol keeps bleeding." : "which leans against the rally.");
    else s = "Vol is flat: vanna flow is dormant until " + p.id + " moves (±1 pt = " + std::to_string((long long)std::llround(std::fabs(vanna_up))) + " contracts).";
    double vom = 0, veg = 0; for (auto& id : ids) { auto it = books.find(id); if (it == books.end()) continue; if (!it->second.insight.swinging.ok) continue; for (auto& m : it->second.insight.swinging.cards) { vom += m.vomma; veg += m.vega; } }
    if (veg != 0) c.vomma = vom > 0 ? "Vomma positive: dealer vega grows as IV rises, so hedging gets larger per vol point in a vol spike (convex)." : "Vomma negative: dealer vega shrinks as IV rises, so a vol spike self-limits dealer vega hedging.";
    else c.vomma = "Vomma unavailable: no swing-horizon strikes with vega.";
    const std::string& r = p.regime;
    if (r == "stress") c.hedge = p.id + " is in STRESS: hedge flows are at their largest per move; gamma and vanna flows both scale, so levels break more easily and rebounds come from vol falling back (vanna bid), not from walls.";
    else if (r == "elevated") c.hedge = p.id + " is ELEVATED: dealer hedging is active; a further vol rise would speed it up, a fall would turn vanna/charm supportive.";
    else if (r == "compressed") c.hedge = p.id + " is COMPRESSED: vanna is small and gamma/pinning dominates; the risk is a vol jump (high vomma) re-pricing hedges fast.";
    else c.hedge = p.id + " is in a NORMAL regime: hedge flows follow gamma; vanna adds only on a vol move.";
    c.hedge += " " + c.vanna;
    // VIX vs VXN divergence (both live anywhere in the terminal)
    const VolIn *vix = nullptr, *vxn = nullptr; for (auto& kv : vols) { if (!kv.second.available) continue; if (kv.second.id == "VIX") vix = &kv.second; if (kv.second.id == "VXN") vxn = &kv.second; }
    if (vix && vxn) { double dp = (vxn->pct_rank - vix->pct_rank) * 100; bool opp = vix->trend * vxn->trend < 0;
        if (std::fabs(dp) >= 25 || opp) c.divergence = std::string("VXN (") + vc::ord((int)std::lround(vxn->pct_rank * 100)) + " pct, " + vc::trend_s(vxn->trend) + ") and VIX (" + vc::ord((int)std::lround(vix->pct_rank * 100)) + " pct, " + vc::trend_s(vix->trend) + ") diverge: " + (dp > 0 ? "risk is concentrated in Nasdaq/growth names rather than the broad market, so Nasdaq books (QQQ/NQ) carry the stress and SPX/ES hedging is calmer." : dp < 0 ? "risk is concentrated in the broad market rather than Nasdaq: SPX/ES hedging is under more stress than QQQ/NQ." : "the two indices are moving in opposite directions: treat the book families separately.");
        else c.divergence = "VXN and VIX agree (percentiles within 25 pts, same direction): no relative-stress signal between Nasdaq and S&P books."; }
    else c.divergence = "";
    c.sidebar = {c.headline, c.hedge, c.vomma}; if (!c.divergence.empty()) c.sidebar.push_back(c.divergence);
    return c;
}
inline std::string volctx_json(const VolCtx& c) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); };
    o << "{\"ok\":" << (c.ok ? "true" : "false") << ",\"reason\":\"" << json::esc(c.reason) << "\",\"headline\":\"" << json::esc(c.headline) << "\",\"hedge\":\"" << json::esc(c.hedge) << "\",\"vanna\":\"" << json::esc(c.vanna) << "\",\"vomma\":\"" << json::esc(c.vomma) << "\",\"divergence\":\"" << json::esc(c.divergence) << "\",\"idx\":[";
    for (size_t i = 0; i < c.idx.size(); ++i) { auto& x = c.idx[i]; o << (i ? "," : "") << "{\"id\":\"" << x.id << "\",\"regime\":\"" << x.regime << "\",\"level\":" << n(x.level) << ",\"pct\":" << n(x.pct) << ",\"trend\":" << x.trend << ",\"books\":["; for (size_t j = 0; j < x.books.size(); ++j) o << (j ? "," : "") << "\"" << json::esc(x.books[j]) << "\""; o << "]}"; }
    o << "],\"sidebar\":["; for (size_t i = 0; i < c.sidebar.size(); ++i) o << (i ? "," : "") << "\"" << json::esc(c.sidebar[i]) << "\""; o << "]}"; return o.str(); }
}
