#pragma once
// scope_io.h - parses the scope the page sends via window.vtSetScope(JSON). webview hands bind() a JSON array of
// the call arguments, so the request looks like ["{\"mode\":\"family\",...}"] (a JSON string holding JSON).
#include "scope.h"
namespace vt::scope {
inline std::optional<Scope> from_ui_request(const reg::Registry& r, const std::string& req) {
    try {
        auto outer = mj::parse(req); const mj::Val* v = &outer;
        if (v->t == mj::Val::Arr && !v->a.empty()) v = &v->a[0];
        mj::Val inner; if (v->t == mj::Val::Str) { inner = mj::parse(v->s); v = &inner; }
        if (v->t != mj::Val::Obj) return std::nullopt;
        std::string m = v->at("mode").str(), t = v->at("ticker").str(), f = v->has("family") ? v->at("family").str() : "";
        Mode want = m == "single" ? Mode::Single : Mode::MultiBook;
        if (!r.selectable(t)) return std::nullopt;                       // unknown or disabled ticker: reject
        if (want != Mode::Single && !f.empty() && (!r.family(f) || !r.family(f)->enabled)) return std::nullopt;
        // Single-book picked from a futures family: the page sends ticker=book, mode=single.
        return build(r, t, want, f, {}, "");
    } catch (...) { return std::nullopt; }
}
}
