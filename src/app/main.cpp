// main.cpp — desktop shell. Embeds a native window hosting a Chromium/
// WebView2/WKWebView surface via the `webview/webview` single-header library
// (https://github.com/webview/webview), loads web/index.html, and wires the
// Scheduler + StructuralInsightEngine + RegimeModelService's outputs to
// webview.eval() calls into window.onModelUpdate / window.onInsightUpdate /
// window.onRegimeStatic / window.onRegimeLive (see web/js/app.js).
//
// *** THIS FILE IS THE ONE PLACE THAT NEEDS EXTERNAL, PLATFORM-SPECIFIC DEPS ***
// It is deliberately the thinnest file in the project: everything it touches
// (Scheduler, StructuralInsightEngine, RegimeModelService, the concrete
// adapters) is unit-testable without it -- see:
//   tests/test_core.cpp   -- quant core + scheduler, headless, incl. the
//                            scheduler-attached regime model end-to-end
//   tests/test_regime.cpp -- PCA/GMM regime model standalone
//
// NOT buildable in this sandbox: no network to fetch webview/webview or a
// GUI toolchain (no X11/Cocoa/Win32 headers, no WebView2 runtime) is available
// here. This file is the integration point to build once you have a real
// desktop environment and your API credentials -- everything upstream of it
// (quant core, bridge, insight engine, regime model) is already compiled and
// tested headlessly.
//
// To build for real:
//   1. vendor webview/webview.h (https://github.com/webview/webview) -- single header, MIT.
//   2. Implement concrete subclasses of OptionsChainAdapter / BarFeedAdapter /
//      HistoricalBaselineAdapter / RegimeHistoryAdapter against your API
//      (see include/vt/adapters.h).
//   3. Implement a concrete LlmClient (see src/insight/structural_insight_engine.h)
//      against your model provider of choice.
//   4. `cmake --preset desktop && cmake --build build` (see CMakeLists.txt;
//      the `vt_app` target is gated behind -DVT_BUILD_APP=ON since it needs
//      webview.h vendored locally first).

#include <cstdlib>
#include "../scope/scope_io.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>

#include "webview.h"  // vendor this: https://github.com/webview/webview (single header)

#include "vt/adapters.h"
#include "src/core/scheduler.h"
#include "src/core/regime_service.h"
#include "src/insight/structural_insight_engine.h"

// ---- Fill these in against your live API once it's available ----
#include "your_api_adapters.h"   // YourChainAdapter, YourBarAdapter, YourHistAdapter, YourRegimeHistoryAdapter
#include "your_llm_client.h"     // YourLlmClient

int main() {
    webview::webview w(true, nullptr);
    w.set_title("VOLATILITY ANALYSIS");
    w.set_size(1600, 1000, WEBVIEW_HINT_NONE);
    w.navigate("file://" WEB_ROOT "/index.html"); // WEB_ROOT injected by CMake

    YourChainAdapter chain_adapter;
    YourBarAdapter bar_adapter;
    YourHistAdapter hist_adapter;
    YourRegimeHistoryAdapter regime_hist_adapter;
    YourLlmClient llm_client;

    // A single eval-based sink handles every channel: each JS-side handler
    // (onModelUpdate/onInsightUpdate/onRegimeStatic/onRegimeLive) ignores the
    // channel argument it doesn't need, so one sink per bridge consumer is
    // enough -- see web/js/app.js for how each channel is dispatched.
    auto make_eval_sink = [&w](const std::string& js_fn, bool pass_channel) {
        return vt::bridge::Sink([&w, js_fn, pass_channel](const std::string& channel, const std::string& payload) {
            // Pushes into the page. webview::eval marshals a JS string; escaping
            // the payload (already valid JSON) as a JS string literal is the
            // only tricky part -- a real build should base64 or otherwise
            // safely encode this rather than naive string concatenation.
            std::string js = pass_channel
                ? js_fn + "('" + channel + "', " + payload + ")"
                : js_fn + "(" + payload + ")";
            w.eval(js);
        });
    };
    vt::bridge::Sink ui_sink       = make_eval_sink("window.onModelUpdate", true);
    vt::bridge::Sink insight_sink  = make_eval_sink("window.onInsightUpdate", false);
    vt::bridge::Sink regime_sink   = [&w](const std::string& channel, const std::string& payload) {
        std::string js_fn = channel == "regime_static" ? "window.onRegimeStatic"
                          : channel == "regime_live"   ? "window.onRegimeLive"
                          : channel == "regime_viz"    ? "window.onRegimeViz"
                          : channel == "multibook"     ? "window.onMultibook"
                          : channel == "strike_lens"   ? "window.onStrikeLens"
                          : channel == "multibook_model" ? "window.onMultibookModel"
                                                       : "window.onRegimeVizLive";
        w.eval(js_fn + "(" + payload + ")");
    };
    // The scheduler is constructed with `ui_sink` for its Section-1 model
    // payloads; regime_static/regime_live channels need routing to the
    // regime-specific JS entry points, so we wrap sink dispatch by channel name.
    vt::bridge::Sink combined_sink = [&](const std::string& channel, const std::string& payload) {
        if (channel == "regime_static" || channel == "regime_live" || channel == "regime_viz" || channel == "regime_viz_live" || channel == "multibook" || channel == "strike_lens" || channel == "multibook_model") regime_sink(channel, payload);
        else ui_sink(channel, payload);
    };

    vt::core::Scheduler scheduler(chain_adapter, bar_adapter, hist_adapter, combined_sink);
    vt::insight::StructuralInsightEngine insight_engine(llm_client, insight_sink);

    vt::regime::RegimeModelService regime_service;
    scheduler.attach_regime_model(regime_service, &regime_hist_adapter);
    // Initial batch fit before the first live cycle -- §9 requires the live
    // path never fit anything itself, so this must happen at least once
    // before run_cycle() can publish a regime_live payload.
    scheduler.refit_regime_model();
    // Pre-market baseline (spec section 5): handled inside Scheduler::run_cycle(), which
    // calls establish_baseline() on the first feature vector of each trading date and
    // after every refit or underlying switch. Nothing to wire here.

    // Header ticker selector (SPX / SPY / NDX / QQQ). The page calls
    // window.vtSetUnderlying("NDX"); webview marshals the args as a JSON array
    // string, e.g. ["NDX"]. Only whitelisted symbols are accepted.
    w.bind("vtSetUnderlying", [&](const std::string& req) -> std::string {
        for (const char* sym : {"SPX", "SPY", "NDX", "QQQ"})
            if (req.find(std::string("\"") + sym + "\"") != std::string::npos) { scheduler.set_underlying(sym); return "true"; }
        return "false";
    });

    // Scope (Task 2): registry + the page's Single-Book / Multi-Book / Family choice. Out-of-scope books are never
    // fetched (ScopedChainSource). Set VT_REGISTRY to override the path. Single mode publishes nothing extra.
    static const vt::reg::Registry registry = vt::reg::load(std::getenv("VT_REGISTRY") ? std::getenv("VT_REGISTRY") : "config/registry.json");
    scheduler.attach_multibook(registry);
    w.bind("vtSetScope", [&](const std::string& req) -> std::string {
        auto sc = vt::scope::from_ui_request(registry, req);
        if (!sc) return "false";
        scheduler.set_scope(*sc); return "true";
    });

    std::atomic<bool> running{true};
    std::thread worker([&]() {
        int cycle = 0;
        while (running) {
            auto state = scheduler.run_cycle();          // fast cadence: every ~30s; also runs the
                                                           // regime model's live layer if attached
            if (cycle % 4 == 0) {                          // insight engine: every ~2min
                insight_engine.run_cycle(state, state.spot);
            }
            // Regime model batch refit cadence: hourly here as a starting
            // point (§9/§11.3 -- recommend calibrating during validation;
            // nightly is also reasonable once intraday OI refresh cadence is
            // confirmed per the spec's §13 open question).
            if (cycle % 120 == 0 && cycle > 0) {            // ~120 * 30s = 1 hour
                scheduler.refit_regime_model();
            }
            ++cycle;
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    });

    w.run();       // blocks until window closes
    running = false;
    worker.join();
    return 0;
}
