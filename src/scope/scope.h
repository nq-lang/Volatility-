#pragma once
// scope.h - Scope drives every panel/model/LLM call. mode: single | multi_book | family.
#include <cmath>
#include "../registry/registry.h"
namespace vt::scope {
enum class Mode { Single, MultiBook, Family };
inline const char* mode_str(Mode m) { return m == Mode::Single ? "single" : m == Mode::MultiBook ? "multi_book" : "family"; }
struct BookWeight { std::string id; int priority = 0; double prior = 0, liquidity = 1, relevance = 1, weight = 0; bool included = false; std::string excluded_reason; };
struct Scope {
    Mode mode = Mode::Single; std::string ticker, family, anchor;
    std::vector<BookWeight> books;
    reg::Weighting weighting = reg::Weighting::PriorityXLiquidity;
    double confidence = 1.0; std::string snapshot_ts;
    std::string model_variant() const { return mode == Mode::Single ? "single_v1" : "multibook_v1"; }
    std::vector<std::string> active_ids() const { std::vector<std::string> r; for (auto& b : books) if (b.included) r.push_back(b.id); return r; }
    bool in_scope(const std::string& id) const {
        if (mode == Mode::Single) return id == ticker;
        if (id == anchor) return true;
        for (auto& b : books) if (b.included && b.id == id) return true;
        return false; }
    std::string key() const { std::string k = std::string(mode_str(mode)) + "|" + ticker + "|" + family + "|" + snapshot_ts; for (auto& b : books) if (b.included) k += "|" + b.id; return k; }
};
struct BookInput { std::string id; bool available = true; double liquidity = 1.0; double relevance = 1.0; std::string why_missing; };
inline double prior_of(int priority, double decay) { return std::pow(decay, std::max(0, priority - 1)); }
// Renormalize weights across available, resolved books only. Excluded books keep a reason (never silently dropped).
inline void compute_weights(Scope& s, const reg::Family& f, const std::map<std::string, BookInput>& in, const reg::Registry& r) {
    s.books.clear(); double tot = 0;
    for (auto& b : f.books) {
        BookWeight w; w.id = b.id; w.priority = b.priority; w.prior = prior_of(b.priority, r.prior_decay);
        auto it = in.find(b.id);
        if (!b.resolved) w.excluded_reason = "unresolved symbol (" + b.note + ")";
        else if (it == in.end() || !it->second.available) w.excluded_reason = it == in.end() ? "no data" : (it->second.why_missing.empty() ? "no data" : it->second.why_missing);
        else { w.included = true; w.liquidity = it->second.liquidity; w.relevance = it->second.relevance;
            w.weight = w.prior * (r.weighting == reg::Weighting::PriorityXLiquidity ? w.liquidity : 1.0) * w.relevance; tot += w.weight; }
        s.books.push_back(w); }
    for (auto& w : s.books) w.weight = (w.included && tot > 0) ? w.weight / tot : 0;
    double all = 0, got = 0; for (auto& w : s.books) { all += w.prior; if (w.included) got += w.prior; }
    s.confidence = all > 0 ? got / all : 0; s.weighting = r.weighting;
}
// Futures anchor => Family (no popup). Standard ticker => Single unless MultiBook requested.
inline Scope build(const reg::Registry& r, const std::string& ticker, Mode want, const std::string& family_pick,
                   const std::map<std::string, BookInput>& in, const std::string& ts) {
    auto t = r.canon(ticker); Scope s; s.ticker = t; s.snapshot_ts = ts;
    if (r.is_anchor(t)) { s.mode = Mode::Family; s.family = t; s.anchor = t; compute_weights(s, *r.family(t), in, r); return s; }
    if (want == Mode::Single) { s.mode = Mode::Single; return s; }
    auto fams = r.families_of(t); if (fams.empty()) { s.mode = Mode::Single; return s; }
    const reg::Family* f = fams[0]; for (auto* x : fams) if (x->id == family_pick) f = x;
    s.mode = Mode::MultiBook; s.family = f->id; s.anchor = f->anchor; compute_weights(s, *f, in, r); return s;
}
// Scope-guarded chain source: refuses out-of-scope books.
template <class Inner> struct Guarded {
    const Scope& sc; Inner& in; mutable int refused = 0;
    template <class... A> auto fetch(const std::string& id, A&&... a) const {
        if (!sc.in_scope(id)) { ++refused; throw std::runtime_error("scope violation: " + id); }
        return in.fetch(id, std::forward<A>(a)...); }
};
}
