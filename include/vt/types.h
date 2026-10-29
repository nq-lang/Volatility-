#pragma once
// vt/types.h — shared plain-data types for the Volatility Analysis terminal.
// Pure data + pure math live here. No UI, no I/O, no networking.

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace vt {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

enum class OptionType { Call, Put };

// DTE buckets per the build spec: 0DTE, 1DTE, 2DTE, and an aggregated "term" view.
// "Term" = everything from 2DTE+ (or 3DTE+, configurable) weighted by OI/weekly-monthly/opex.
enum class DteBucket : uint8_t { D0 = 0, D1 = 1, D2 = 2, Term = 3, Count = 4 };

inline const char* to_string(DteBucket b) {
    switch (b) {
        case DteBucket::D0:   return "0DTE";
        case DteBucket::D1:   return "1DTE";
        case DteBucket::D2:   return "2DTE";
        case DteBucket::Term: return "term";
        default:              return "unknown";
    }
}

// Classifies a raw days-to-expiry count into a bucket.
// NOTE: "Term" is not exclusive of D0/D1/D2 — every model that needs an
// aggregated/term view should compute it separately as an OI-weighted roll-up
// across ALL live expiries (see dte.h::term_weight), not just >=3DTE contracts.
inline DteBucket classify_dte(int dte_days) {
    if (dte_days <= 0) return DteBucket::D0;
    if (dte_days == 1) return DteBucket::D1;
    if (dte_days == 2) return DteBucket::D2;
    return DteBucket::Term;
}

// A single option contract's live market state.
struct OptionQuote {
    OptionType type;
    double strike{};
    int    dte_days{};          // calendar days to expiry
    double t_years{};           // time to expiry in years (trading-day convention applied upstream)
    double bid{};
    double ask{};
    double mid{};
    double iv{};                // mid IV (solved upstream or supplied by adapter)
    double open_interest{};     // EOD OI (see gamma_flip.h / oi_momentum.h re: intraday proxy)
    double volume{};            // running session volume, used as intraday OI proxy driver
    bool   is_opex{false};      // true if this expiry is a monthly/quarterly opex date
};

// One full snapshot of the chain for a single underlying, one instant in time.
struct ChainSnapshot {
    std::string underlying;     // e.g. "SPX"
    TimePoint   ts;
    double      spot{};
    double      risk_free_rate{0.0};
    double      dividend_yield{0.0};
    double      contract_multiplier{100.0};
    std::vector<OptionQuote> quotes;
};

// Full Black-Scholes Greek set for one contract at one instant.
struct Greeks {
    double delta{};
    double gamma{};
    double vega{};
    double theta{};   // per-day, not per-year (see blackscholes.h)
    double rho{};
    double vanna{};   // d(delta)/d(vol)  == d(vega)/d(spot) (normalized)
    double charm{};   // d(delta)/d(time), per-day
    double speed{};   // d(gamma)/d(spot)
    double color{};   // d(gamma)/d(time), per-day
};

// A single point on a dealer-exposure curve (GEX/DEX/Vanna/Charm/Theta/Speed/Color),
// keyed by (strike, expiry). Carries its own dte_days so the Term bucket -
// which spans every live expiry - can expose a genuine per-(strike, expiry)
// grid instead of one curve pre-summed across all expiries. D0/D1/D2 buckets
// naturally only ever populate one dte_days value, same as before.
struct ExposurePoint {
    double strike{};
    int    dte_days{};
    double value{};             // signed, aggregated exposure at this (strike, expiry)
};

struct ExposureCurve {
    DteBucket bucket{};
    TimePoint ts{};
    std::vector<ExposurePoint> points;   // sorted by strike ascending
};

// One point on an IV surface: (strike-or-delta, dte_days) -> iv.
struct SurfacePoint {
    double x{};      // strike OR delta, depending on surface type
    int    dte_days{};
    double iv{};
};

struct Surface {
    DteBucket bucket{DteBucket::Term}; // which DTE bucket this surface was computed for. Carried
                                       // explicitly: inferring it from the first point's dte is
                                       // wrong for Term (which spans every expiry).
    std::string name;            // "VRP", "call_delta_iv", "put_delta_iv", "RND", ...
    std::string x_axis_label;    // "strike" | "delta"
    TimePoint   ts{};
    std::vector<SurfacePoint> points;
};

// Historical bar, used for realized-vol estimators and time-of-day baselines.
struct Bar {
    TimePoint ts;
    double open{}, high{}, low{}, close{};
};

} // namespace vt
