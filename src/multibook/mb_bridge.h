#pragma once
// mb_bridge.h - JSON for the "multibook" channel (scope, weights, per-book summaries, aggregate signals).
#include <sstream>
#include "engine.h"
namespace vt::mb {
inline std::string multibook_json(const AggregateResult& a) {
    std::ostringstream o; auto n = [](double v) { return json::num(v); }; auto opt = [&](const std::optional<double>& v) { return v ? n(*v) : std::string("null"); };
    const auto& s = a.scope;
    o << "{\"epoch\":\"" << a.epoch << "\",\"scope\":{\"mode\":\"" << scope::mode_str(s.mode) << "\",\"ticker\":\"" << json::esc(s.ticker) << "\",\"family\":\"" << json::esc(s.family) << "\",\"anchor\":\"" << json::esc(s.anchor)
      << "\",\"model_variant\":\"" << s.model_variant() << "\",\"weighting\":\"" << (s.weighting == reg::Weighting::PriorityOnly ? "priority_only" : "priority_x_liquidity") << "\",\"snapshot_ts\":\"" << json::esc(s.snapshot_ts) << "\"},"
      << "\"confidence\":" << n(s.confidence) << ",\"raw_net_gex\":" << n(a.raw_net_gex) << ",\"sign_agreement\":" << n(a.sign_agreement) << ",\"flip_confluence\":" << n(a.flip_confluence)
      << ",\"dealer_convention\":\"dealers long calls / short puts (assumed, not observed)\",\"books\":[";
    for (size_t i = 0; i < s.books.size(); ++i) { auto& w = s.books[i]; if (i) o << ",";
        o << "{\"id\":\"" << json::esc(w.id) << "\",\"priority\":" << w.priority << ",\"prior\":" << n(w.prior) << ",\"liquidity\":" << n(w.liquidity) << ",\"weight\":" << n(w.weight)
          << ",\"included\":" << (w.included ? "true" : "false") << ",\"reason\":\"" << json::esc(w.excluded_reason) << "\"";
        auto it = a.books.find(w.id);
        if (w.included && it != a.books.end()) { auto& b = it->second; o << ",\"spot\":" << n(b.spot) << ",\"net_gex\":" << n(b.net_gex) << ",\"flip\":" << opt(b.flip) << ",\"call_wall\":" << opt(b.call_wall) << ",\"put_wall\":" << opt(b.put_wall); }
        o << "}"; }
    o << "],\"hedge\":";
    if (a.hedge && a.hedge->books > 0) { auto& h = *a.hedge; auto arr = [&](const std::vector<double>& v) { std::string r = "["; for (size_t i = 0; i < v.size(); ++i) r += (i ? "," : "") + n(v[i]); return r + "]"; };
        o << "{\"books\":" << h.books << ",\"shock_pct\":" << arr(h.shock) << ",\"anchor_contracts\":" << arr(h.anchor_contracts) << ",\"notional\":" << arr(h.notional)
          << ",\"vanna_up_c\":" << n(h.vanna_up_c) << ",\"vanna_dn_c\":" << n(h.vanna_dn_c) << ",\"charm_1d_c\":" << n(h.charm_1d_c) << ",\"zero_dte_up_c\":" << n(h.zero_dte_1pct_up_c) << ",\"zero_dte_dn_c\":" << n(h.zero_dte_1pct_dn_c)
          << ",\"per_book\":["; bool first = true; for (auto& w : s.books) { auto it = a.books.find(w.id); if (!w.included || it == a.books.end() || !it->second.hedge.ok) continue; auto& hh = it->second.hedge; if (!first) o << ","; first = false;
            o << "{\"id\":\"" << json::esc(w.id) << "\",\"zero_dte_gamma_share\":" << n(hh.zero_dte_gamma_share) << ",\"zero_dte_oi_share\":" << n(hh.zero_dte_oi_share) << ",\"vanna_up\":" << n(hh.vanna_up) << ",\"charm_1d\":" << n(hh.charm_1d) << "}"; }
        o << "]}"; } else o << "null";
    o << ",\"volctx\":" << volctx_json(a.volctx) << ",\"session\":" << (a.session ? session_json(*a.session) : std::string("null"));
    o << ",\"notes\":[";
    for (size_t i = 0; i < a.notes.size(); ++i) { if (i) o << ","; o << "\"" << json::esc(a.notes[i]) << "\""; }
    o << "]}"; return o.str();
}
}
