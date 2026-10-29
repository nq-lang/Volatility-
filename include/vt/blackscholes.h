#pragma once
// vt/blackscholes.h — Black-Scholes pricing + first/second/third-order Greeks.
// Pure functions, no state, unit-testable in isolation per the build spec.

#include <cmath>
#include <numbers>
#include "vt/types.h"

namespace vt::bs {

inline double norm_pdf(double x) {
    static const double inv_sqrt_2pi = 1.0 / std::sqrt(2.0 * std::numbers::pi);
    return inv_sqrt_2pi * std::exp(-0.5 * x * x);
}

inline double norm_cdf(double x) {
    return 0.5 * std::erfc(-x / std::numbers::sqrt2);
}

// d1, d2 for the standard Black-Scholes-Merton formula (continuous dividend yield q).
inline double d1(double S, double K, double r, double q, double sigma, double T) {
    if (sigma <= 0.0 || T <= 0.0) return 0.0;
    return (std::log(S / K) + (r - q + 0.5 * sigma * sigma) * T) / (sigma * std::sqrt(T));
}
inline double d2(double S, double K, double r, double q, double sigma, double T) {
    return d1(S, K, r, q, sigma, T) - sigma * std::sqrt(T);
}

inline double price(OptionType type, double S, double K, double r, double q, double sigma, double T) {
    if (T <= 0.0 || sigma <= 0.0) {
        double intrinsic = (type == OptionType::Call) ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
        return intrinsic;
    }
    const double D1 = d1(S, K, r, q, sigma, T);
    const double D2 = D1 - sigma * std::sqrt(T);
    const double disc_q = std::exp(-q * T);
    const double disc_r = std::exp(-r * T);
    if (type == OptionType::Call) {
        return S * disc_q * norm_cdf(D1) - K * disc_r * norm_cdf(D2);
    }
    return K * disc_r * norm_cdf(-D2) - S * disc_q * norm_cdf(-D1);
}

// Full Greek set. theta/charm/color are returned PER-DAY (divided by 365) since
// that's the natural unit for intraday dealer-flow interpretation; vega/vanna
// are per 1.00 (100 vol points) of sigma, matching standard convention — the
// UI bridge scales as needed for display.
inline Greeks greeks(OptionType type, double S, double K, double r, double q, double sigma, double T) {
    Greeks g{};
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0 || K <= 0.0) return g;

    const double sqrtT   = std::sqrt(T);
    const double D1      = d1(S, K, r, q, sigma, T);
    const double D2      = D1 - sigma * sqrtT;
    const double pdf_d1  = norm_pdf(D1);
    const double disc_q  = std::exp(-q * T);
    const double disc_r  = std::exp(-r * T);
    const int    sign    = (type == OptionType::Call) ? 1 : -1;

    // First order
    g.delta = sign * disc_q * norm_cdf(sign * D1);
    g.gamma = disc_q * pdf_d1 / (S * sigma * sqrtT);
    g.vega  = S * disc_q * pdf_d1 * sqrtT;                 // per 1.00 vol
    g.rho   = sign * K * T * disc_r * norm_cdf(sign * D2);

    // Theta (per year, standard formula), then converted to per-day.
    double theta_year;
    if (type == OptionType::Call) {
        theta_year = -(S * disc_q * pdf_d1 * sigma) / (2.0 * sqrtT)
                     - r * K * disc_r * norm_cdf(D2)
                     + q * S * disc_q * norm_cdf(D1);
    } else {
        theta_year = -(S * disc_q * pdf_d1 * sigma) / (2.0 * sqrtT)
                     + r * K * disc_r * norm_cdf(-D2)
                     - q * S * disc_q * norm_cdf(-D1);
    }
    g.theta = theta_year / 365.0;

    // Vanna: d(delta)/d(vol) = -disc_q * pdf(d1) * d2 / sigma
    g.vanna = -disc_q * pdf_d1 * D2 / sigma;

    // Charm: d(delta)/d(time), converted to per-day.
    double charm_year;
    if (type == OptionType::Call) {
        charm_year = -disc_q * ( pdf_d1 * ( (r - q) / (sigma * sqrtT) - D2 / (2.0 * T) )
                                  + (-q) * norm_cdf(D1) );
    } else {
        charm_year = -disc_q * ( pdf_d1 * ( (r - q) / (sigma * sqrtT) - D2 / (2.0 * T) )
                                  - (-q) * norm_cdf(-D1) );
    }
    g.charm = charm_year / 365.0;

    // Speed: d(gamma)/d(spot) = -gamma/S * (d1/(sigma*sqrtT) + 1)
    g.speed = -g.gamma / S * (D1 / (sigma * sqrtT) + 1.0);

    // Color: d(gamma)/d(time), converted to per-day.
    double color_year = -disc_q * pdf_d1 / (2.0 * S * T * sigma * sqrtT)
                         * ( 2.0 * q * T + 1.0 + (2.0 * (r - q) * T - D2 * sigma * sqrtT) / (sigma * sqrtT) * D1 );
    g.color = color_year / 365.0;

    return g;
}

} // namespace vt::bs
