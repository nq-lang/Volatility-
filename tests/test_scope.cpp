#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/scope/scope_io.h"
using namespace vt;
static std::map<std::string, scope::BookInput> all(const reg::Family& f) { std::map<std::string, scope::BookInput> m; for (auto& b : f.books) m[b.id] = {b.id, true, 1.0, 1.0, ""}; return m; }
struct Fake { int calls = 0; int fetch(const std::string&) { return ++calls; } };
int main() {
    auto r = reg::load(std::string(VT_SRC_DIR) + "/config/registry.json");
    assert(r.families.size() == 6);
    assert(r.canon("sl") == "SI" && r.canon("b/lco") == "B_LCO");
    assert(r.family("GC")->enabled);
    assert(r.is_anchor("GC") && r.is_anchor("nq") && r.is_anchor("ES"));
    assert(r.families_of("NVDA").size() == 2);
    assert(r.families_of("MSFT").size() == 3);
    assert(r.families_of("OG").size() == 1 && r.families_of("GLD").size() == 1);
    assert(!r.family("YM")->books[2].resolved);
    const auto& nq = *r.family("NQ"); size_t n = nq.books.size();
    for (unsigned mask = 1; mask < (1u << n); ++mask) {
        auto in = all(nq); for (size_t i = 0; i < n; ++i) if (!(mask >> i & 1)) in[nq.books[i].id].available = false;
        scope::Scope s; scope::compute_weights(s, nq, in, r); double t = 0;
        for (auto& b : s.books) { t += b.weight; assert(b.included || b.weight == 0); }
        assert(std::fabs(t - 1.0) < 1e-12);
    }
    { auto& ym = *r.family("YM"); scope::Scope s; scope::compute_weights(s, ym, all(ym), r); double t = 0; for (auto& b : s.books) t += b.weight;
      assert(std::fabs(t - 1) < 1e-12); assert(!s.books[2].included && s.books[2].excluded_reason.find("unresolved") == 0); assert(s.confidence < 1); }
    { r.weighting = reg::Weighting::PriorityOnly; auto in = all(nq); in["QQQ"].liquidity = 100; scope::Scope s; scope::compute_weights(s, nq, in, r);
      r.weighting = reg::Weighting::PriorityXLiquidity; scope::Scope s2; scope::compute_weights(s2, nq, in, r); assert(s2.books[0].weight > s.books[0].weight); }
    assert(scope::build(r, "NQ", scope::Mode::Single, "", all(nq), "t").mode == scope::Mode::Family);
    assert(scope::build(r, "QQQ", scope::Mode::Single, "", {}, "t").mode == scope::Mode::Single);
    { auto s = scope::build(r, "MSFT", scope::Mode::MultiBook, "ES", all(*r.family("ES")), "t"); assert(s.family == "ES" && s.mode == scope::Mode::MultiBook); }
    { auto s = scope::build(r, "QQQ", scope::Mode::Single, "", {}, "t"); Fake f; scope::Guarded<Fake> g{s, f};
      assert(g.fetch("QQQ") == 1); bool threw = false; try { g.fetch("SPX"); } catch (...) { threw = true; } assert(threw && f.calls == 1 && g.refused == 1); }
    { auto q = scope::from_ui_request(r, "[\"{\\\"mode\\\":\\\"multi_book\\\",\\\"ticker\\\":\\\"QQQ\\\",\\\"family\\\":\\\"NQ\\\"}\"]");
      assert(q && q->mode == scope::Mode::MultiBook && q->family == "NQ" && q->ticker == "QQQ");
      auto fm = scope::from_ui_request(r, "[\"{\\\"mode\\\":\\\"family\\\",\\\"ticker\\\":\\\"GC\\\",\\\"family\\\":\\\"GC\\\"}\"]"); assert(fm && fm->mode == scope::Mode::Family && fm->anchor == "GC");
      auto sg = scope::from_ui_request(r, "[\"{\\\"mode\\\":\\\"single\\\",\\\"ticker\\\":\\\"NVDA\\\"}\"]"); assert(sg && sg->mode == scope::Mode::Single);
      assert(!scope::from_ui_request(r, "[\"{\\\"mode\\\":\\\"single\\\",\\\"ticker\\\":\\\"ZZZZ\\\"}\"]")); assert(!scope::from_ui_request(r, "garbage")); }
    std::puts("test_scope OK"); return 0;
}
