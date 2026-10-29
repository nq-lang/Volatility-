#pragma once
// vt/dte.h — shared DTE-bucketing logic. Every model in Section 3 of the build
// spec is computed once per DteBucket (D0, D1, D2, Term) using these helpers,
// so the bucketing rule lives in exactly one place.

#include <vector>
#include <functional>
#include <array>
#include "vt/types.h"

namespace vt::dte {

// Filters quotes into a bucket. D0/D1/D2 are exact-DTE matches; Term is an
// OI-weighted aggregate across ALL expiries (weekly/monthly/opex-weighted),
// per the spec's "0-60 day term axis" convention — opex expiries get a mild
// weight bump since OI accumulates there over days rather than resetting nightly.
inline std::vector<const OptionQuote*> select_bucket(const ChainSnapshot& chain, DteBucket bucket) {
    std::vector<const OptionQuote*> out;
    out.reserve(chain.quotes.size());
    for (const auto& q : chain.quotes) {
        switch (bucket) {
            case DteBucket::D0:   if (q.dte_days == 0) out.push_back(&q); break;
            case DteBucket::D1:   if (q.dte_days == 1) out.push_back(&q); break;
            case DteBucket::D2:   if (q.dte_days == 2) out.push_back(&q); break;
            case DteBucket::Term: if (q.dte_days >= 0 && q.dte_days <= 60) out.push_back(&q); break;
            default: break;
        }
    }
    return out;
}

// Per-quote weight multiplier used only by the Term bucket's aggregations,
// so persistent multi-session opex OI counts for more than a same-week
// non-opex expiry with equal raw OI. Tune opex_bump per house convention.
inline double term_weight(const OptionQuote& q, double opex_bump = 1.25) {
    return q.is_opex ? opex_bump : 1.0;
}

// Runs `fn` for every bucket and collects results — convenience for the
// scheduler, which needs "compute every model, every bucket" each cycle.
template <typename Result>
inline std::array<Result, 4> for_each_bucket(const ChainSnapshot& chain,
                                              std::function<Result(const ChainSnapshot&, DteBucket)> fn) {
    std::array<Result, 4> out;
    for (uint8_t i = 0; i < static_cast<uint8_t>(DteBucket::Count); ++i) {
        out[i] = fn(chain, static_cast<DteBucket>(i));
    }
    return out;
}

} // namespace vt::dte
