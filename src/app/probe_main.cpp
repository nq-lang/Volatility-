// probe_main.cpp -- run ONCE against the live data source to answer the two open questions
// from the PCA spec's §13: intraday OI refresh cadence, and available history depth.
//   vt_probe [SPX|SPY|NDX|QQQ] [polls=120] [poll_seconds=30]
// Integration point, like main.cpp: needs your concrete adapters (not buildable in the sandbox).
#include <iostream>
#include <thread>
#include "../tools/source_probe.h"
#include "your_api_adapters.h"   // YourChainAdapter, YourRegimeHistoryAdapter
int main(int argc, char** argv) {
    std::string u = argc > 1 ? argv[1] : "SPX";
    size_t polls = argc > 2 ? std::stoul(argv[2]) : 120; double every = argc > 3 ? std::stod(argv[3]) : 30.0;
    YourChainAdapter chain; YourRegimeHistoryAdapter hist;
    auto c = vt::tools::measure_oi_refresh_cadence(chain, u, polls, every, [](double s) { std::this_thread::sleep_for(std::chrono::duration<double>(s)); });
    auto d = vt::tools::probe_history_depth(hist, u);
    vt::tools::print(c, d);
    return 0;
}
