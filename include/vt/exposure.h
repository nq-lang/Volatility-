#pragma once
// vt/exposure.h — shared per-strike accumulation used by GEX/DEX/VEX/CEX/SEX/
// COLEX (Section 4 of the supplied Extended Options-Futures-Microstructure
// notation glossary). CORRECTED from the original draft: the house convention
// is NOT "apply one uniform dealer sign to every contract" — GEX and DEX use
// an explicit, TYPE-DEPENDENT combination (calls and puts combine with
// different signs), while VEX/CEX/SEX/COLEX use plain, unsigned addition
// across calls and puts (the sign is already carried by the raw Greek itself
// for delta/vanna/charm, or is intentionally a magnitude sum for the others).
// This file only accumulates raw per-strike, per-type sums; each named model
// (gex.h, dex.h, vanna.h, ...) applies its own documented formula on top.
#include <map>
#include <functional>
#include "vt/types.h"
#include "vt/blackscholes.h"
#include "vt/dte.h"

namespace vt::exposure {

// The dealer-positioning convention is NOT directly observable from public OI
// data — state it explicitly wherever a number derived from it is shown (per
// the KB's repeated, explicit warning). Default here matches the supplied
// library's stated industry-standard convention: dealers are modeled net LONG
// calls and net SHORT puts (i.e. customers are net put-buyers/hedgers and net
// call-sellers/overwriters). Flip `invert` to model the opposite assumption.
struct DealerConvention {
    bool invert = false; // false = "dealers long calls, short puts" (the default); true = the opposite
};

using GreekSelector = std::function<double(const Greeks&)>;

struct StrikeAccum { double call_raw = 0.0, put_raw = 0.0; };

// Key: (strike, dte_days). D0/D1/D2 buckets naturally collapse to one
// dte_days value per strike (identical to the old strike-only behavior); the
// Term bucket spans every live expiry, so THIS is what gives GEX/DEX/etc. a
// real per-(strike, expiry) grid instead of one curve pre-summed across
// expiries -- the actual surface, matching the reference visualization's DTE axis.
using StrikeDteKey = std::pair<double, int>;

// Accumulates raw (unsigned, per-contract-weighted) Greek*OI sums per
// (strike, expiry), split by option type. Every named model combines
// call_raw/put_raw itself per its own formula — see gex.h/dex.h (signed,
// type-dependent) vs. vanna.h/charm.h/speed_color.h/theta.h (plain addition).
inline std::map<StrikeDteKey, StrikeAccum> accumulate_raw(const ChainSnapshot& chain,
                                                            DteBucket bucket,
                                                            bool use_volume_proxy,
                                                            const GreekSelector& select) {
    std::map<StrikeDteKey, StrikeAccum> out;
    for (const auto* qp : dte::select_bucket(chain, bucket)) {
        const auto& q = *qp;
        Greeks g = bs::greeks(q.type, chain.spot, q.strike, chain.risk_free_rate,
                               chain.dividend_yield, q.iv, q.t_years);
        double size = use_volume_proxy ? q.volume : q.open_interest;
        if (bucket == DteBucket::Term) size *= dte::term_weight(q, 1.25);
        double contribution = size * select(g) * chain.contract_multiplier;
        auto& acc = out[{q.strike, q.dte_days}];
        if (q.type == OptionType::Call) acc.call_raw += contribution;
        else                            acc.put_raw  += contribution;
    }
    return out;
}

inline double net(const ExposureCurve& curve) {
    double total = 0.0;
    for (const auto& p : curve.points) total += p.value;
    return total;
}

} // namespace vt::exposure

