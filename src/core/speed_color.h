#pragma once
// Speed -- SEX = Sigma[Speed_c*OI_c + Speed_p*OI_p] * m * S^2   (04_speed.md).
// Color  -- COLEX = Sigma[Color_c*OI_c + Color_p*OI_p] * m       (05_color.md).
// Both are plain addition across calls/puts, per the KB. Speed: how fast GEX
// itself will reshape as spot approaches a wall/flip -- high Speed = fragile
// level, low Speed = stable level. Color: how the gamma profile decays purely
// from time passing (a "melting" wall independent of any price move); combine
// with Charm to project the whole hedging book forward assuming no price/vol
// change, and compare actual intraday deviation against that baseline.
#include "vt/exposure.h"

namespace vt::model {

inline ExposureCurve speed_exposure(const ChainSnapshot& chain, DteBucket bucket, bool use_volume_proxy = false) {
    auto raw = exposure::accumulate_raw(chain, bucket, use_volume_proxy,
                                         [](const Greeks& g) { return g.speed; });
    ExposureCurve curve;
    curve.bucket = bucket;
    curve.ts = chain.ts;
    curve.points.reserve(raw.size());
    double s2 = chain.spot * chain.spot;
    for (const auto& [key, acc] : raw)
        curve.points.push_back({key.first, key.second, (acc.call_raw + acc.put_raw) * s2});
    return curve;
}

inline ExposureCurve color_exposure(const ChainSnapshot& chain, DteBucket bucket, bool use_volume_proxy = false) {
    auto raw = exposure::accumulate_raw(chain, bucket, use_volume_proxy,
                                         [](const Greeks& g) { return g.color; });
    ExposureCurve curve;
    curve.bucket = bucket;
    curve.ts = chain.ts;
    curve.points.reserve(raw.size());
    for (const auto& [key, acc] : raw)
        curve.points.push_back({key.first, key.second, acc.call_raw + acc.put_raw});
    return curve;
}

} // namespace vt::model
