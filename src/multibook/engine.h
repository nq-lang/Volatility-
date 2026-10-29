#pragma once
// engine.h - scope-driven data layer. Fetches ONLY in-scope books through a scope-guarded source, computes each book
// once via BookCache, derives liquidity weights, aggregates, and builds the strike lens (T2-2 / T2-4).
#include <cmath>
#include <stdexcept>
#include "../scope/scope.h"
#include "../../include/vt/adapters.h"
#include "strike_lens.h"
#include "normalize.h"
#include "hedge_analytics.h"
#include "session_track.h"
#include "vol_context.h"
namespace vt::mb {
// Wraps any OptionsChainAdapter and refuses out-of-scope underlyings (scope-bypass guard).
class ScopedChainSource : public adapters::OptionsChainAdapter {
    adapters::OptionsChainAdapter& in_; const scope::Scope& sc_;
public:
    mutable int refused = 0, served = 0;
    ScopedChainSource(adapters::OptionsChainAdapter& in, const scope::Scope& sc) : in_(in), sc_(sc) {}
    ChainSnapshot fetch_snapshot(const std::string& u) override {
        if (!sc_.in_scope(u)) { ++refused; throw std::runtime_error("scope violation: " + u); }
        ++served; return in_.fetch_snapshot(u); }
};
struct AggregateResult {
    scope::Scope scope; std::map<std::string, BookAnalytics> books;     // included books only
    double raw_net_gex = 0;          // additive dollar gamma per 1% across included books
    double sign_agreement = 0;       // sum(w * sign(net_gex)) in [-1, 1]; |x| near 1 = books agree
    double flip_confluence = 0;      // share of weight whose translated flip is within tol of the weighted median
    std::optional<SessionView> session;   // dealer-hedging Greeks tracked open -> now (needs EngineOptions::track)
    std::optional<AggLadder> hedge;  // aggregate dealer hedging flows in anchor-equivalent contracts (index-convertible books only)
    std::string epoch;               // id of this model state (see epoch_of)
    std::optional<Lens> lens;        // Family / Multi-Book only
    VolCtx volctx;                   // terminal-wide VIX/VXN reading for the books in scope (Hedge Flow, sidebar, Pricing)
    std::vector<std::string> notes;
};
struct EngineOptions { std::map<std::string, std::pair<double, double>> realized;   // book id -> (annualised RV, last-day return) when the caller has bars
    double confluence_tol = 0.003; exposure::DealerConvention conv{}; bool vol_proxy = false;
    std::map<std::string, VolIn> vols; SessionTracker* track = nullptr; };                  // book id -> live vol-index regime (VIX/VXN) for the Strike Lens cards
inline AggregateResult run_scope(const reg::Registry& r, scope::Scope sc, adapters::OptionsChainAdapter& raw, BookCache& cache, const EngineOptions& opt = {}) {
    AggregateResult res;
    // Scope must authorise every configured family book BEFORE fetching (weights are only known after the fetch).
    if (sc.mode != scope::Mode::Single) { sc.books.clear(); for (auto& b : r.family(sc.family)->books) { scope::BookWeight w; w.id = b.id; w.priority = b.priority; w.included = true; sc.books.push_back(w); } }
    ScopedChainSource src(raw, sc);
    std::vector<std::string> ids; const reg::Family* fam = nullptr;
    if (sc.mode == scope::Mode::Single) ids = {sc.ticker};
    else { fam = r.family(sc.family); for (auto& b : fam->books) ids.push_back(b.id); }
    std::map<std::string, scope::BookInput> in;
    for (auto& id : ids) {
        scope::BookInput bi; bi.id = id;
        try { auto chain = src.fetch_snapshot(id); auto k = ts_key(chain);
            const auto& a = cache.get_or_compute(id, k, [&] { return compute_book(id, chain, opt.conv, opt.vol_proxy); });
            res.books[id] = a; { auto rv = opt.realized.find(id); if (rv != opt.realized.end()) apply_realized(res.books[id].vol, rv->second.first, rv->second.second); } bi.available = true; bi.liquidity = a.oi_notional; }
        catch (const std::exception& e) { bi.available = false; bi.why_missing = e.what(); }
        in[id] = bi;
    }
    double mx = 0; for (auto& kv : in) if (kv.second.available) mx = std::max(mx, kv.second.liquidity);
    for (auto& kv : in) if (kv.second.available) kv.second.liquidity = mx > 0 ? std::max(0.05, kv.second.liquidity / mx) : 1.0;
    if (sc.mode == scope::Mode::Single) { sc.books.clear(); res.scope = sc; if (res.books.count(sc.ticker)) res.raw_net_gex = res.books[sc.ticker].net_gex; return res; }
    scope::compute_weights(sc, *fam, in, r);
    for (auto& w : sc.books) if (!w.included) res.notes.push_back(w.id + " excluded: " + w.excluded_reason);
    for (auto& w : sc.books) if (w.included) { auto& a = res.books[w.id]; res.raw_net_gex += a.net_gex; res.sign_agreement += w.weight * (a.net_gex > 0 ? 1 : a.net_gex < 0 ? -1 : 0); }
    // anchor futures price (never throws out of scope: anchor is in scope)
    double F = 0; try { F = src.fetch_snapshot(fam->anchor).spot; } catch (...) { res.notes.push_back("anchor " + fam->anchor + " price unavailable: strike conversion disabled"); }
    res.lens = build_lens(r, *fam, F, res.books, &opt.vols);
    std::vector<Level> flips; for (auto& lb : res.lens->books) for (auto& w : sc.books) if (w.included && w.id == lb.id && lb.convertible)
        for (auto& l : lb.levels) if (l.label == "Gamma Flip" && l.fut) flips.push_back({*l.fut, w.weight});
    res.flip_confluence = confluence(flips, opt.confluence_tol);
    { std::vector<const HedgeAnalytics*> hs; for (auto& lb : res.lens->books) if (lb.convertible && res.books.count(lb.id) && res.books[lb.id].hedge.ok) { bool inc = false; for (auto& w : sc.books) if (w.id == lb.id && w.included) inc = true; if (inc) hs.push_back(&res.books[lb.id].hedge); }
      auto* as = r.sym(fam->anchor); if (as && F > 0 && as->multiplier > 0) res.hedge = aggregate_hedge(hs, F, as->multiplier); }
    { std::vector<std::string> inc; for (auto& w : sc.books) if (w.included) inc.push_back(w.id); res.volctx = build_volctx(opt.vols, inc, res.hedge ? &*res.hedge : nullptr, res.books); }
    if (opt.track) {
        std::vector<std::string> ids2; for (auto& lb : res.lens->books) if (lb.convertible && res.books.count(lb.id)) { bool inc = false; for (auto& w : sc.books) if (w.id == lb.id && w.included) inc = true; if (inc) ids2.push_back(lb.id); }
        for (auto& id2 : ids2) opt.track->record(id2, res.books[id2]);
        auto* as = r.sym(fam->anchor); if (as && F > 0 && as->multiplier > 0 && !ids2.empty() && res.hedge) {
            double ref = 0; for (size_t i = 0; i < res.hedge->shock.size(); ++i) if (std::fabs(std::fabs(res.hedge->shock[i]) - 1.0) < 1e-9) ref = std::max(ref, std::fabs(res.hedge->anchor_contracts[i]));
            VolIn vi; auto vv = opt.vols.find(fam->anchor); if (vv != opt.vols.end()) vi = vv->second;
            res.session = build_session_view(*opt.track, ids2, F * as->multiplier, ref, vi, res.books[ids2[0]].insight.ses, fam->anchor); } }
    res.epoch = epoch_of(sc.key()); if (res.lens) res.lens->epoch = res.epoch;
    res.scope = sc; return res;
}
}
