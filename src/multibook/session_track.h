#pragma once
// session_track.h - tracks dealer-hedging Greeks through the regular session (workfile B2 observed rows, B3 sidebar).
// One point per book per snapshot (>= 1 per minute), reset each ET session day, RTH only. Nothing is back-filled: before the first
// snapshot of the day there is no history and the views say so. Flows are in ANCHOR-equivalent contracts (same unit as the hedge ladder).
// "Observed" = change in the dealers' net hedge between snapshots (price, time and vol effects combined) next to the price move over the
// same window; no causal claim is made beyond that.
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include "book_analytics.h"
#include "strike_insight.h"
namespace vt::mb {
struct TrackPoint { long long utc = 0; double spot = 0, hedge_sh = 0, charm_1d_sh = 0, charm_close_sh = 0, vanna_up_sh = 0, vanna_dn_sh = 0, net_gex = 0, atm_iv = 0; bool has_close = false; };
class SessionTracker {
public:
    void record(const std::string& id, const BookAnalytics& a) {
        std::lock_guard<std::mutex> g(m_); const auto& ses = a.insight.ses; if (a.insight.spot <= 0 || !ses.rth) return;
        int day = ses.year * 10000 + ses.month * 100 + ses.day; if (day != day_) { s_.clear(); day_ = day; }
        auto& v = s_[id]; if (!v.empty() && ses.utc < v.back().utc) return;                               // never go backwards
        TrackPoint p; p.utc = ses.utc; p.spot = a.spot; p.hedge_sh = a.hedge.hedge_delta0; p.charm_1d_sh = a.hedge.charm_1d; p.has_close = a.hedge.zero_dte_oi_share > 0; p.charm_close_sh = a.hedge.charm_to_close;
        p.vanna_up_sh = a.hedge.vanna_up; p.vanna_dn_sh = a.hedge.vanna_dn; p.net_gex = a.net_gex; p.atm_iv = a.pricing.atm_iv;
        if (!v.empty() && p.utc / 60 == v.back().utc / 60) v.back() = p; else v.push_back(p);
    }
    std::vector<TrackPoint> series(const std::string& id) const { std::lock_guard<std::mutex> g(m_); auto it = s_.find(id); return it == s_.end() ? std::vector<TrackPoint>{} : it->second; }
    void clear() { std::lock_guard<std::mutex> g(m_); s_.clear(); day_ = 0; }
private:
    mutable std::mutex m_; std::map<std::string, std::vector<TrackPoint>> s_; int day_ = 0;
};
struct SeriesPt { long long utc = 0; int min_since_open = 0; double spot = 0, hedge_c = 0, charm_1d_c = 0, charm_close_c = 0, vanna_up_c = 0, vanna_dn_c = 0, gex = 0; bool has_close = false; };
struct Obs { std::string window, from, to, reading; double move_pct = 0, flow_c = 0, flow_notional = 0; };
struct GreekRead { std::string name, state, text; double first = 0, last = 0; };
struct SessionView {
    bool ok = false; std::string reason, as_of, window_text, charm_headline, interaction, closing, vol_text; int n = 0, to_close_min = -1; bool into_close = false;
    std::vector<SeriesPt> pts; std::vector<Obs> observed; std::vector<GreekRead> reads; double charm_ratio = -1;
};
inline std::string fcon(double c) { std::ostringstream o; o.setf(std::ios::fixed); double a = std::fabs(c); const char* sg = c < 0 ? "-" : "+"; if (a >= 1e4) o.precision(0); else if (a >= 100) o.precision(0); else o.precision(1); o << sg << a; return o.str(); }
// ids: books that join the aggregate (index-convertible, included). den = anchor_px * anchor_mult. ref_1pct = |anchor contracts| of a +-1% move (scale for "how large").
inline SessionView build_session_view(const SessionTracker& T, const std::vector<std::string>& ids, double den, double ref_1pct, const VolIn& vol, const sess::Session& now, const std::string& anchor) {
    SessionView V; V.as_of = now.as_of; V.window_text = "09:30 ET to " + sess::hhmm(std::min(now.et_min, sess::CLOSE_MIN - 60)) + " ET (tracking stops one hour before the close; the final hour is read as the handoff)";
    if (den <= 0 || ids.empty()) { V.reason = "no index-convertible books in scope"; return V; }
    std::vector<std::vector<TrackPoint>> ss; for (auto& id : ids) ss.push_back(T.series(id));
    if (ss[0].empty()) { V.reason = "no snapshot recorded yet this session"; return V; }
    for (auto& p0 : ss[0]) { SeriesPt q; q.utc = p0.utc; bool all = true;
        for (size_t k = 0; k < ss.size() && all; ++k) { const TrackPoint* f = nullptr; for (auto& p : ss[k]) if (p.utc / 60 == p0.utc / 60) f = &p; if (!f) { all = false; break; }
            double s = f->spot / den; q.hedge_c += f->hedge_sh * s; q.charm_1d_c += f->charm_1d_sh * s; q.vanna_up_c += f->vanna_up_sh * s; q.vanna_dn_c += f->vanna_dn_sh * s; q.gex += f->net_gex; if (f->has_close) { q.charm_close_c += f->charm_close_sh * s; q.has_close = true; } if (k == 0) q.spot = f->spot; }
        if (all) { q.min_since_open = (sess::from_utc(q.utc).et_min - sess::OPEN_MIN); V.pts.push_back(q); } }
    V.n = (int)V.pts.size(); if (V.n == 0) { V.reason = "snapshots of the in-scope books never aligned in time"; return V; }
    V.to_close_min = now.rth ? now.to_close : -1; V.ok = true; V.into_close = now.rth && now.to_close <= 60;
    const auto& a = V.pts.front(); const auto& b = V.pts.back();
    // ---- observed rows: net dealer hedge change vs the price move over the same window
    auto row = [&](const char* name, const SeriesPt& p0, const SeriesPt& p1) { if (p1.utc <= p0.utc) return; Obs o; o.window = name; o.from = sess::hhmm(sess::from_utc(p0.utc).et_min); o.to = sess::hhmm(sess::from_utc(p1.utc).et_min);
        o.move_pct = p0.spot > 0 ? (p1.spot / p0.spot - 1) * 100 : 0; o.flow_c = p1.hedge_c - p0.hedge_c; o.flow_notional = o.flow_c * den;
        bool same = (o.flow_c > 0) == (o.move_pct > 0); bool tiny = std::fabs(o.move_pct) < 0.02;
        bool negl = ref_1pct > 0 && std::fabs(o.flow_c) < 0.05 * ref_1pct;
        o.reading = negl ? "Net dealer hedge change is negligible (" + fcon(o.flow_c) + " contracts vs " + fcon(ref_1pct).substr(1) + " for a 1% move): hedging did not drive this window." : tiny ? "Price barely moved; dealers' net hedge changed " + fcon(o.flow_c) + " contracts from time and vol effects."
                         : std::string("Dealers net ") + (o.flow_c > 0 ? "BOUGHT " : "SOLD ") + fcon(std::fabs(o.flow_c)).substr(1) + " " + anchor + "-equivalent contracts while price " + (o.move_pct > 0 ? "rose " : "fell ") + std::to_string(std::fabs(o.move_pct)).substr(0, 4) + "%: hedging moved " + (same ? "WITH price (it amplified the move)." : "AGAINST price (it damped the move).");
        V.observed.push_back(o); };
    row("Since open", a, b);
    for (int w : {30, 60}) { const SeriesPt* s = nullptr; for (auto& p : V.pts) if (b.utc - p.utc >= w * 60) s = &p; if (s && s->utc != a.utc) row(w == 30 ? "Last 30 min" : "Last 60 min", *s, b); }
    // ---- Greek trends through the session (first point -> latest; "steady" if within 10% of the larger magnitude)
    auto trend = [&](const char* nm, double f, double l, const std::string& what) { GreekRead g; g.name = nm; g.first = f; g.last = l; double sc = std::max(std::fabs(f), std::fabs(l)); double rel = sc > 0 ? (l - f) / sc : 0;
        bool grow = std::fabs(l) > std::fabs(f); g.state = std::fabs(rel) < 0.10 ? "steady" : grow ? "strengthening" : "fading";
        g.text = what + ": " + fcon(f) + " at the open -> " + fcon(l) + " now (" + g.state + ")."; V.reads.push_back(g); };
    if (V.n >= 2) { if (a.has_close && b.has_close) trend("Charm to close", a.charm_close_c, b.charm_close_c, "dealer hedge still to do by the close from time alone (contracts)"); trend("Charm per day", a.charm_1d_c, b.charm_1d_c, "one-day time-decay hedge (contracts)");
        trend("Vanna (+1 vol pt)", a.vanna_up_c, b.vanna_up_c, "hedge flow per +1 vol pt (contracts)"); trend("Net gamma", a.gex / 1e6, b.gex / 1e6, "net dealer GEX ($M per 1%)"); }
    // ---- charm headline + interaction + closing consequence
    const double cc = b.has_close ? b.charm_close_c : b.charm_1d_c; const char* basis = b.has_close ? "to the close" : "per day";
    V.charm_ratio = ref_1pct > 0 ? std::fabs(cc) / ref_1pct : -1;
    std::string tier = V.charm_ratio < 0 ? "of unknown size (no 1% reference)" : V.charm_ratio >= 0.5 ? "LARGE (at least half of what a 1% move would force)" : V.charm_ratio >= 0.15 ? "moderate" : "small";
    V.charm_headline = std::string("Charm is ") + (cc < 0 ? "negative" : "positive") + " " + basis + ": " + fcon(cc) + " " + anchor + " contracts, " + tier + ". Time alone makes dealers " + (cc < 0 ? "SELL" : "BUY") + " into the close.";
    if (V.n >= 2) { const double c0 = a.has_close ? a.charm_close_c : a.charm_1d_c; V.charm_headline += std::string(" Since the open it has gone from ") + fcon(c0) + " to " + fcon(cc) + " (" + (std::fabs(cc) > std::fabs(c0) * 1.1 ? "building" : std::fabs(cc) < std::fabs(c0) * 0.9 ? "fading" : "steady") + ")."; }
    else V.charm_headline += " Only one snapshot so far: no through-the-session trend yet.";
    V.vol_text = vol.available ? vol.id + " " + (vol.regime == "unknown" ? "regime withheld" : vol.regime) + (vol.trend > 0 ? ", rising" : vol.trend < 0 ? ", falling" : ", flat") : "vol index unavailable";
    if (vol.available && vol.trend != 0) { double vf = vol.trend > 0 ? b.vanna_up_c : b.vanna_dn_c; bool same = (vf > 0) == (cc > 0); double net = cc + vf;
        V.interaction = vol.id + " is " + (vol.trend > 0 ? "rising" : "falling") + ": one more vol point makes dealers " + (vf > 0 ? "BUY " : "SELL ") + fcon(std::fabs(vf)).substr(1) + " contracts. That " + (same ? "ADDS TO charm (same direction)" : "OFFSETS charm (opposite direction)") + "; combined drift into the close ~" + fcon(net) + " contracts (" + (net < 0 ? "downward" : "upward") + ") if vol keeps moving 1 pt.";
    } else V.interaction = std::string("Vanna interaction conditional: ") + fcon(b.vanna_up_c) + " contracts if vol +1 pt / " + fcon(b.vanna_dn_c) + " if -1 pt (" + (vol.available ? "vol direction flat" : "no vol index feed") + ").";
    V.closing = std::string(b.gex >= 0 ? "Net gamma is positive (" : "Net gamma is negative (") + std::to_string((int)std::lround(b.gex / 1e6)) + "M/1%): " + (b.gex >= 0 ? "dealers absorb the charm drift, so it shows up as a slow pin rather than a move." : "dealers amplify the charm drift, so the move into the close can extend.") + (now.rth ? " " + std::to_string(now.to_close) + " min to the close." : "");
    return V;
}
inline std::string session_json(const SessionView& V) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); }; auto s = [](const std::string& t) { return "\"" + json::esc(t) + "\""; };
    o << "{\"ok\":" << (V.ok ? "true" : "false") << ",\"reason\":" << s(V.reason) << ",\"as_of\":" << s(V.as_of) << ",\"window\":" << s(V.window_text) << ",\"n\":" << V.n << ",\"to_close_min\":" << V.to_close_min << ",\"into_close\":" << (V.into_close ? "true" : "false") << ",\"charm_ratio\":" << n(V.charm_ratio)
      << ",\"charm_headline\":" << s(V.charm_headline) << ",\"interaction\":" << s(V.interaction) << ",\"closing\":" << s(V.closing) << ",\"vol\":" << s(V.vol_text) << ",\"pts\":[";
    for (size_t i = 0; i < V.pts.size(); ++i) { auto& p = V.pts[i]; o << (i ? "," : "") << "{\"min\":" << p.min_since_open << ",\"spot\":" << n(p.spot) << ",\"hedge\":" << n(p.hedge_c) << ",\"charm_1d\":" << n(p.charm_1d_c) << ",\"charm_close\":" << (p.has_close ? n(p.charm_close_c) : "null") << ",\"vanna_up\":" << n(p.vanna_up_c) << ",\"vanna_dn\":" << n(p.vanna_dn_c) << ",\"gex\":" << n(p.gex) << "}"; }
    o << "],\"observed\":[";
    for (size_t i = 0; i < V.observed.size(); ++i) { auto& r = V.observed[i]; o << (i ? "," : "") << "{\"window\":" << s(r.window) << ",\"from\":" << s(r.from) << ",\"to\":" << s(r.to) << ",\"move_pct\":" << n(r.move_pct) << ",\"flow_c\":" << n(r.flow_c) << ",\"flow_notional\":" << n(r.flow_notional) << ",\"reading\":" << s(r.reading) << "}"; }
    o << "],\"reads\":[";
    for (size_t i = 0; i < V.reads.size(); ++i) { auto& r = V.reads[i]; o << (i ? "," : "") << "{\"name\":" << s(r.name) << ",\"state\":" << s(r.state) << ",\"first\":" << n(r.first) << ",\"last\":" << n(r.last) << ",\"text\":" << s(r.text) << "}"; }
    o << "]}"; return o.str();
}
}
