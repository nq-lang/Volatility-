// MOCK fixture for the multibook_v1 model card, produced by the REAL walk-forward model + service on a synthetic regime-switching history.
#include <fstream>
#include <random>
#include "../src/multibook/cross_block.h"
using namespace vt; using namespace vt::mb;
int main(int, char** argv) {
    std::mt19937 g(23); std::normal_distribution<double> N(0, 1); const size_t n = 300; const double scale[5] = {0.004, 0.014, 0.011, 0.007, 0.009}; int r = 0;
    Mat am(5, Vec(17, 0.0)), cm(5, Vec(7, 0.0)); for (int k = 0; k < 5; ++k) { for (int j = 0; j < 17; ++j) am[k][j] = 0.4 * N(g); for (int j = 0; j < 7; ++j) cm[k][j] = 1.6 * N(g); }
    am[0][regime::NetGEX_z] = 2; am[0][regime::FlipDistancePct] = 1; am[0][regime::Speed_z] = -1; am[1][regime::ImpliedZ_ATM] = 2; am[1][regime::RvIvZscoreTod] = 2; am[2][regime::NetGEX_z] = -2; am[2][regime::FlipDistancePct] = -1; am[2][regime::Speed_z] = 1.5; am[3][regime::NetGEX_z] = 0.5; am[4][regime::Speed_z] = 0.5;
    mbm::Options o; o.step = 20; o.k_grid = {4, 5}; MultibookModelService svc(o); Vec out;
    for (size_t t = 0; t < n; ++t) { if (std::uniform_real_distribution<double>(0, 1)(g) > 0.88) r = int(g() % 5); Vec a(17), x(7); for (int j = 0; j < 17; ++j) a[j] = am[r][j] + 2.2 * N(g); for (int j = 0; j < 7; ++j) x[j] = cm[r][j] + 0.5 * N(g); svc.append_day("NQ", "QQQ", a, x); out.push_back(scale[r] * std::fabs(N(g)) + 0.001); }
    std::ofstream(argv[1]) << svc.status_json("NQ", "QQQ", "MOCK 2026-10-19", out, true); return 0; }
