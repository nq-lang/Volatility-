#pragma once
// Structural insight engine — Section 2 ("VOL STRUCTURE"). Consumes the
// CURRENT STATE of every Section-1 model plus a short recent history (to
// detect deformation, not just level), sends it to an LLM using
// prompt_template.h's system prompt, and publishes the structured JSON read
// to the "vol_structure" UI channel each cycle. The LLM call itself is behind
// a small abstract interface so this file has no dependency on any specific
// provider/SDK — plug in your Anthropic/OpenAI/local-model client.
#include <string>
#include <sstream>
#include <deque>
#include "vt/types.h"
#include "../core/scheduler.h"
#include "../bridge/ui_bridge.h"
#include "../bridge/json_util.h"
#include "prompt_template.h"

namespace vt::insight {

// Minimal abstraction over "send these two strings (system, user), get text
// back." Wire this to whatever LLM client/SDK you're using; the engine only
// needs synchronous request/response semantics (run it on its own thread if
// the call is slow — this file makes no threading assumptions).
class LlmClient {
public:
    virtual ~LlmClient() = default;
    virtual std::string complete(const std::string& system_prompt, const std::string& user_message) = 0;
};

// Serializes one ExposureCurve compactly (strike:value pairs) for the LLM
// input — deliberately terser than the UI bridge's JSON since this goes into
// a prompt, not a renderer.
inline std::string compact(const ExposureCurve& c) {
    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < c.points.size(); ++i) {
        if (i) o << ",";
        o << "[" << c.points[i].strike << "," << c.points[i].value << "]";
    }
    o << "]";
    return o.str();
}

inline std::string compact(const Surface& s) {
    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < s.points.size(); ++i) {
        if (i) o << ",";
        o << "[" << s.points[i].x << "," << s.points[i].dte_days << "," << s.points[i].iv << "]";
    }
    o << "]";
    return o.str();
}

// Keeps a short rolling history of headline scalars (gamma flip distance, ATM
// VRP, call OI ratio) so the engine can describe DEFORMATION, not just level,
// per the build spec's explicit requirement.
struct RecentHistory {
    explicit RecentHistory(size_t window = 12) : window_(window) {}
    std::deque<double> gamma_flip_distance;
    std::deque<double> atm_vrp;
    std::deque<double> call_oi_ratio;

    void push(double flip_dist, double vrp, double oi_ratio) {
        push_one(gamma_flip_distance, flip_dist);
        push_one(atm_vrp, vrp);
        push_one(call_oi_ratio, oi_ratio);
    }
    static std::string series(const std::deque<double>& d) {
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < d.size(); ++i) { if (i) o << ","; o << d[i]; }
        o << "]"; return o.str();
    }
private:
    size_t window_;
    void push_one(std::deque<double>& d, double v) { d.push_back(v); if (d.size() > window_) d.pop_front(); }
};

class StructuralInsightEngine {
public:
    StructuralInsightEngine(LlmClient& llm, bridge::Sink sink) : llm_(llm), sink_(std::move(sink)) {}

    // Runs one Section-2 cycle from a completed Section-1 ModelState. Call
    // this less frequently than the fast cadence (e.g. every 3rd-5th
    // scheduler cycle, or on a fixed ~1-2min timer) — this is an LLM call and
    // does not need 15-60s freshness the way raw Greeks do.
    std::string run_cycle(const core::ModelState& state, double spot) {
        auto atm_vrp_now = model::atm_vrp(state.vrp[static_cast<uint8_t>(DteBucket::D0)], spot).value_or(0.0);
        history_.push(state.gamma_flip.distance, atm_vrp_now, state.call_oi_ratio_term);

        std::ostringstream user;
        user << "{"
             << "\"spot\":" << spot << ","
             << "\"dealer_convention\":\"" << (state.convention_inverted
                    ? "inverted (dealers short calls / long puts)"
                    : "default (dealers long calls / short puts)") << "\","
             << "\"gamma_flip\":{"
                 << "\"distance\":" << state.gamma_flip.distance << ","
                 << "\"regime\":\"" << (state.gamma_flip.regime_long_gamma ? "long_gamma" : "short_gamma") << "\","
                 << "\"flip_level\":" << (state.gamma_flip.flip_level ? std::to_string(*state.gamma_flip.flip_level) : "null") << ","
                 << "\"distance_history\":" << RecentHistory::series(history_.gamma_flip_distance)
             << "},"
             << "\"call_oi_ratio\":{"
                 << "\"current\":" << state.call_oi_ratio_term << ","
                 << "\"momentum\":" << state.call_oi_momentum.value_or(0.0) << ","
                 << "\"history\":" << RecentHistory::series(history_.call_oi_ratio)
             << "},"
             << "\"put_oi_ratio\":{"
                 << "\"current\":" << state.put_oi_ratio_term << ","
                 << "\"momentum\":" << state.put_oi_momentum.value_or(0.0)
             << "},"
             << "\"atm_vrp_0dte\":{"
                 << "\"current\":" << atm_vrp_now << ","
                 << "\"history\":" << RecentHistory::series(history_.atm_vrp)
             << "},"
             << "\"gex_term\":" << compact(state.gex[static_cast<uint8_t>(DteBucket::Term)]) << ","
             << "\"dex_term\":" << compact(state.dex[static_cast<uint8_t>(DteBucket::Term)]) << ","
             << "\"vanna_0dte\":" << compact(state.vanna[static_cast<uint8_t>(DteBucket::D0)]) << ","
             << "\"charm_0dte\":" << compact(state.charm[static_cast<uint8_t>(DteBucket::D0)]) << ","
             << "\"charm_1dte\":" << compact(state.charm[static_cast<uint8_t>(DteBucket::D1)]) << ","
             << "\"theta_0dte\":" << compact(state.theta[static_cast<uint8_t>(DteBucket::D0)]) << ","
             << "\"rnd\":" << compact(state.rnd) << ","
             << "\"call_delta_iv_1dte\":" << compact(state.call_iv[static_cast<uint8_t>(DteBucket::D1)]) << ","
             << "\"put_delta_iv_1dte\":" << compact(state.put_iv[static_cast<uint8_t>(DteBucket::D1)])
             << "}";

        std::string response = llm_.complete(SYSTEM_PROMPT, user.str());
        bridge::publish(sink_, "vol_structure", response);
        return response;
    }

private:
    LlmClient& llm_;
    bridge::Sink sink_;
    RecentHistory history_;
};

} // namespace vt::insight
