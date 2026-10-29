// validate_main.cpp -- runs the §10 suite on real exports.
//   vt_validate days.csv bars.csv [soft_percentile]
//   days.csv : one row per RTH session: 17 raw features (regime::FeatureIndex order), fwd_range, event_flag(0/1)
//   bars.csv : one row per intraday evaluation: speed_value, flip_distance_pct
// Exit code 0 only if all five gates pass; non-zero means the regime output is NOT cleared for live-decision use.
#include <fstream>
#include <iostream>
#include <sstream>
#include "regime_validation.h"
using namespace vt;
static std::vector<std::vector<double>> read_csv(const char* path) {
    std::ifstream f(path); std::vector<std::vector<double>> out; std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line); std::string c; std::vector<double> row;
        while (std::getline(ss, c, ',')) { try { row.push_back(std::stod(c)); } catch (...) { row.clear(); break; } } // skips a header row
        if (!row.empty()) out.push_back(std::move(row));
    }
    return out;
}
int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: vt_validate days.csv bars.csv [soft_percentile]\n"; return 2; }
    validation::SuiteInput in;
    if (argc > 3) in.soft_percentile = std::stod(argv[3]);
    for (auto& r : read_csv(argv[1])) {
        if (r.size() != regime::kNumFeatures + 2) { std::cerr << "days.csv: expected " << regime::kNumFeatures + 2 << " columns, got " << r.size() << "\n"; return 2; }
        validation::DayRecord d; d.features.assign(r.begin(), r.begin() + regime::kNumFeatures);
        d.fwd_range = r[regime::kNumFeatures]; d.event_day = r[regime::kNumFeatures + 1] > 0.5;
        in.days.push_back(std::move(d));
    }
    for (auto& r : read_csv(argv[2])) { if (r.size() != 2) { std::cerr << "bars.csv: expected 2 columns\n"; return 2; } in.bars.push_back({r[0], r[1]}); }
    return validation::report(validation::run_suite(in)) ? 0 : 1;
}
