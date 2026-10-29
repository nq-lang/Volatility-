#pragma once
// vol_bridge.h - turns the Volatility section's analysis (VIX / VXN) into the per-book regime input used terminal-wide (workfile C-3):
// Strike Lens cards, Hedge Flow reading, Dealer Flow sidebar and Pricing risk all read the same VolIn, so they cannot disagree.
#include "../volidx/vol_index.h"
#include "strike_insight.h"
namespace vt::mb {
inline VolIn volin_from(const volidx::Analysis& a) {
    VolIn v; v.id = a.id; v.available = a.available; v.reason = a.reason;
    if (a.available) { v.level = a.level; v.trend = a.trend; v.pct_rank = a.pct_rank; v.regime = volidx::regime_name(a.regime); if (a.chg_1d) v.chg_1d = *a.chg_1d; }
    return v;
}
// book id -> VolIn for every registry-linked book. A linked index with no feed yields available=false (stated, never guessed).
inline std::map<std::string, VolIn> vols_for_books(const reg::Registry& r, const std::map<std::string, volidx::Analysis>& analysed) {
    std::map<std::string, VolIn> out;
    for (auto& kv : r.vol_books) { VolIn v; auto it = analysed.find(kv.first); if (it != analysed.end()) v = volin_from(it->second); else { v.id = kv.first; v.reason = "no " + kv.first + " feed connected"; }
        for (auto& b : kv.second) out[b] = v; }
    return out;
}
}
