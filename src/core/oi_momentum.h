#pragma once
// Call OI ratio momentum — d/dt of (call OI / total OI), NOT the static ratio.
// Per the Framework doc Section 4: a static ratio tells you positioning has
// accumulated; the rate of change tells you it's actively building right now.
//
// APPROXIMATION NOTE (per build spec Section 5): true OI is EOD-only. Intraday,
// this model must use a volume-weighted OI proxy: proxy_OI = last_EOD_OI +
// signed_running_volume, where signed_running_volume is the session's net new
// volume attributed to opening interest (an adapter-level estimate — exact
// open/close attribution requires order-flow tagging most retail feeds don't
// expose). This file documents the approximation; the adapter is responsible
// for supplying `proxy_open_interest` per contract, however it derives it.
#include <deque>
#include <optional>
#include "vt/types.h"
#include "vt/dte.h"

namespace vt::model {

inline double call_oi_ratio(const ChainSnapshot& chain, DteBucket bucket, bool use_volume_proxy) {
    double call_oi = 0.0, total_oi = 0.0;
    for (const auto* q : dte::select_bucket(chain, bucket)) {
        double size = use_volume_proxy ? q->volume : q->open_interest;
        total_oi += size;
        if (q->type == OptionType::Call) call_oi += size;
    }
    return total_oi > 0.0 ? call_oi / total_oi : 0.0;
}

// Put OI Ratio -- the direct complement (Put OI Ratio = 1 - Call OI Ratio in
// aggregate, but tracked as its own series per 13_put_oi_ratio_momentum.md
// since by-strike/by-band breakdowns are NOT simply 1-minus-the-call version
// once you isolate a specific moneyness band, e.g. the far-OTM "crash hedge"
// band). Rising put OI ratio momentum, especially concentrated far-OTM and
// combined with rising negative Vanna exposure in the same strikes, is one of
// the more structurally important combinations in the library (fresh tail
// -hedging demand that would compound downside pressure if IV also rises).
inline double put_oi_ratio(const ChainSnapshot& chain, DteBucket bucket, bool use_volume_proxy) {
    double put_oi = 0.0, total_oi = 0.0;
    for (const auto* q : dte::select_bucket(chain, bucket)) {
        double size = use_volume_proxy ? q->volume : q->open_interest;
        total_oi += size;
        if (q->type == OptionType::Put) put_oi += size;
    }
    return total_oi > 0.0 ? put_oi / total_oi : 0.0;
}

// Rolling momentum tracker: caller feeds one ratio reading per cycle; this
// returns the rate of change over `window` most recent readings (per-cycle slope),
// which is the actual live signal per the Framework doc — not the raw ratio.
// Reusable for either the call-side or put-side ratio series (see aliases below).
class OiRatioMomentumTracker {
public:
    explicit OiRatioMomentumTracker(size_t window = 20) : window_(window) {}

    // Call once per scheduler cycle with the latest ratio; returns momentum
    // (ratio units per cycle) once enough history exists, else nullopt.
    std::optional<double> update(double ratio) {
        history_.push_back(ratio);
        if (history_.size() > window_) history_.pop_front();
        if (history_.size() < 2) return std::nullopt;
        // simple linear slope over the window via first/last, normalized by count
        return (history_.back() - history_.front()) / static_cast<double>(history_.size() - 1);
    }

private:
    size_t window_;
    std::deque<double> history_;
};

using CallOiRatioMomentum = OiRatioMomentumTracker;
using PutOiRatioMomentum  = OiRatioMomentumTracker;

} // namespace vt::model
