#pragma once
// registry.h - ticker registry + symbol master loaded from config/registry.json.
#include <fstream>
#include <sstream>
#include <algorithm>
#include "mini_json.h"
namespace vt::reg {
struct BookDef { std::string id; int priority = 0; bool resolved = true; std::string note; };
struct SymbolInfo { std::string level; std::string id, kind, underlying, currency, venue, vendor_symbol; double multiplier = 0; bool verified = false; };
struct Family { std::string id; bool enabled = true; std::string disabled_reason; std::string anchor; std::string cash_ref; double cash_scale = 1.0, tick = 0.0; std::vector<BookDef> books; };
enum class Weighting { PriorityOnly, PriorityXLiquidity };
struct Registry {
    std::vector<Family> families; std::map<std::string, SymbolInfo> symbols;
    std::map<std::string, std::string> aliases; std::vector<std::string> futures, standard, volatility; std::map<std::string, std::string> vol_anchor; std::map<std::string, std::vector<std::string>> vol_books; // volatility section (VIX, VXN, ...) + linked futures anchor
    std::map<std::string, std::vector<double>> leverage;
    Weighting weighting = Weighting::PriorityXLiquidity; double prior_decay = 0.75;
    std::string canon(std::string t) const { for (auto& c : t) c = (char)toupper((unsigned char)c); auto it = aliases.find(t); return it == aliases.end() ? t : it->second; }
    const Family* family(const std::string& id) const { for (auto& f : families) if (f.id == id) return &f; return nullptr; }
    bool is_anchor(const std::string& t) const { auto c = canon(t); const Family* f = family(c); return f && f->enabled && std::find(futures.begin(), futures.end(), c) != futures.end(); }
    // Enabled families in which a standard ticker is a book (priority-ordered by family order).
    std::vector<const Family*> families_of(const std::string& t) const {
        std::vector<const Family*> r; auto c = canon(t);
        for (auto& f : families) if (f.enabled) for (auto& b : f.books) if (b.id == c) { r.push_back(&f); break; }
        return r; }
    const SymbolInfo* sym(const std::string& id) const { auto it = symbols.find(id); return it == symbols.end() ? nullptr : &it->second; }
    // volatility index linked to a book/anchor (VXN for QQQ/NDX/NQ, VIX for SPX/SPY/XSP/ES); empty when none
    std::string vol_index_for(const std::string& id) const { auto c = canon(id); for (auto& kv : vol_books) if (std::find(kv.second.begin(), kv.second.end(), c) != kv.second.end() || (vol_anchor.count(kv.first) && vol_anchor.at(kv.first) == c)) return kv.first; return ""; }
    bool is_vol(const std::string& t) const { auto c = canon(t); return std::find(volatility.begin(), volatility.end(), c) != volatility.end(); }
    bool selectable(const std::string& t) const { auto c = canon(t); return is_vol(c) || is_anchor(c) || !families_of(c).empty() || (sym(c) && std::find(standard.begin(), standard.end(), c) != standard.end()); }
};
inline Registry from_json(const mj::Val& d) {
    Registry r;
    if (auto w = d.get("weighting")) { r.weighting = w->at("mode").str() == "priority_only" ? Weighting::PriorityOnly : Weighting::PriorityXLiquidity; r.prior_decay = w->at("prior_decay").num(0.75); }
    for (auto& kv : d.at("aliases").o) r.aliases[kv.first] = kv.second.str();
    for (auto& x : d.at("futures_tickers").a) r.futures.push_back(x.str());
    for (auto& x : d.at("standard_tickers").a) r.standard.push_back(x.str());
    if (auto v = d.get("volatility_tickers")) for (auto& x : v->a) r.volatility.push_back(x.str());
    if (auto v = d.get("vol_links")) for (auto& kv : v->o) { r.vol_anchor[kv.first] = kv.second.at("anchor").str(); for (auto& b : kv.second.at("books").a) r.vol_books[kv.first].push_back(b.str()); }
    for (auto& kv : d.at("symbol_master").o) { SymbolInfo s; s.id = kv.first; auto& v = kv.second; s.kind = v.at("kind").str(); s.level = v.has("level") ? v.at("level").str() : ""; s.underlying = v.at("underlying").str();
        s.multiplier = v.at("multiplier").num(0); s.currency = v.at("currency").str(); s.venue = v.at("venue").str(); s.vendor_symbol = v.at("vendor_symbol").str(); s.verified = v.at("verified").b; r.symbols[kv.first] = s; }
    if (auto l = d.get("leverage")) for (auto& kv : l->o) for (auto& x : kv.second.a) r.leverage[kv.first].push_back(x.num());
    for (auto& kv : d.at("families").o) { Family f; f.id = kv.first; auto& v = kv.second; f.enabled = v.at("enabled").b; f.disabled_reason = v.has("disabled_reason") ? v.at("disabled_reason").str() : "";
        f.anchor = v.at("anchor").at("id").str();
        f.cash_ref = v.at("cash_ref").str(); f.cash_scale = v.at("cash_scale").num(1); f.tick = v.at("tick").num(0);
        for (auto& b : v.at("books").a) { BookDef bd; bd.id = b.at("id").str(); bd.priority = (int)b.at("priority").num();
            bd.resolved = !b.has("needs_verification") || r.sym(bd.id) == nullptr ? true : true;
            if (b.has("needs_verification")) bd.note = b.at("needs_verification").str(); else if (b.has("assumed")) bd.note = b.at("assumed").str();
            auto* si = r.sym(bd.id); if (!si || si->multiplier <= 0) bd.resolved = false; // unresolved: no multiplier known (e.g. OZA)
            f.books.push_back(bd); }
        std::stable_sort(f.books.begin(), f.books.end(), [](auto& a, auto& b) { return a.priority < b.priority; });
        r.families.push_back(f); }
    return r;
}
inline Registry load(const std::string& path) { std::ifstream in(path); if (!in) throw std::runtime_error("cannot open " + path); std::stringstream ss; ss << in.rdbuf(); return from_json(mj::parse(ss.str())); }
}
