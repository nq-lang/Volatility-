#pragma once
// UI bridge — serializes each Section-1 model's current state to JSON and
// pushes it into the embedded web view. Decoupled from the webview library
// itself: `Sink` is a function the app layer wires to webview.eval()/postMessage
// (or a local WS/HTTP push) so this file has zero GUI dependencies and stays
// unit-testable (assert on the JSON string) without a display attached.
#include <string>
#include <sstream>
#include <functional>
#include "vt/types.h"
#include "vt/dte.h"
#include "json_util.h"

namespace vt::bridge {

using Sink = std::function<void(const std::string& channel, const std::string& json_payload)>;

inline std::string to_json(const ExposureCurve& c, const std::string& model_name) {
    std::ostringstream o;
    o << "{\"model\":\"" << json::esc(model_name) << "\","
      << "\"bucket\":\"" << to_string(c.bucket) << "\","
      << "\"points\":[";
    for (size_t i = 0; i < c.points.size(); ++i) {
        if (i) o << ",";
        o << "{\"strike\":" << json::num(c.points[i].strike)
          << ",\"dte\":" << c.points[i].dte_days
          << ",\"value\":" << json::num(c.points[i].value) << "}";
    }
    o << "]}";
    return o.str();
}

inline std::string to_json(const Surface& s) {
    std::ostringstream o;
    o << "{\"model\":\"" << json::esc(s.name) << "\","
      << "\"x_axis\":\"" << json::esc(s.x_axis_label) << "\",";
    o << "\"bucket\":\"" << to_string(s.bucket) << "\",";
    o << "\"points\":[";
    for (size_t i = 0; i < s.points.size(); ++i) {
        if (i) o << ",";
        o << "{\"x\":" << json::num(s.points[i].x)
          << ",\"dte\":" << s.points[i].dte_days
          << ",\"y\":" << json::num(s.points[i].iv) << "}";
    }
    o << "]}";
    return o.str();
}

// Generic scalar/gauge push (gamma flip distance, OI ratio momentum, z-scores) —
// these render as compact time-series/gauge widgets per Section 4, not surfaces.
inline std::string to_json_scalar(const std::string& model_name, double value,
                                   size_t sample_n = 0, const std::string& extra_fields = "") {
    std::ostringstream o;
    o << "{\"model\":\"" << json::esc(model_name) << "\","
      << "\"value\":" << json::num(value)
      << ",\"n\":" << sample_n;
    if (!extra_fields.empty()) o << "," << extra_fields; // caller-supplied raw JSON fragment, no leading/trailing braces
    o << "}";
    return o.str();
}

// Publishes one model's payload on its named channel. The web front end
// listens per-channel and reshapes its existing mesh/series in place
// (Section 4: "reshape, don't rebuild") rather than re-rendering from scratch.
inline void publish(const Sink& sink, const std::string& channel, const std::string& payload) {
    if (sink) sink(channel, payload);
}

} // namespace vt::bridge
