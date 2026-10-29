#pragma once
// Tiny JSON-escaping helper. Serialization itself is done with plain
// ostringstream concatenation in ui_bridge.h — deliberately not a generic
// JSON-writer abstraction, to keep the bridge auditable at a glance. Swap in
// nlohmann::json here later if you want structured parsing on the way in too.
#include <string>
#include <cmath>

namespace vt::json {

inline std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out;
}

// JSON-safe number formatting: NaN/Inf become null (JSON has no such literals).
inline std::string num(double v) {
    if (std::isnan(v) || std::isinf(v)) return "null";
    return std::to_string(v);
}

} // namespace vt::json
