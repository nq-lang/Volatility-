#pragma once
// mini_json.h - minimal JSON reader (objects, arrays, strings, numbers, bool, null). Config use only.
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace vt::mj {
struct Val {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<Val> a; std::vector<std::pair<std::string, Val>> o;
    const Val* get(const std::string& k) const { for (auto& kv : o) if (kv.first == k) return &kv.second; return nullptr; }
    bool has(const std::string& k) const { return get(k) != nullptr; }
    const Val& at(const std::string& k) const { auto p = get(k); if (!p) throw std::runtime_error("missing key: " + k); return *p; }
    std::string str(const std::string& d = "") const { return t == Str ? s : d; }
    double num(double d = 0) const { return t == Num ? n : d; }
    bool is_null() const { return t == Null; }
};
class Parser {
    const std::string& s; size_t i = 0;
    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\n'||s[i]=='\t'||s[i]=='\r')) ++i; }
    [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m + " at " + std::to_string(i)); }
    std::string str() {
        if (s[i] != '"') { fail("expected string"); }
        ++i; std::string r;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\') { ++i; char c = s[i];
                switch (c) { case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': r += '\r'; break;
                    case 'u': { r += '?'; i += 4; break; } default: r += c; } ++i; }
            else r += s[i++];
        }
        if (i >= s.size()) { fail("unterminated string"); }
        ++i; return r;
    }
public:
    explicit Parser(const std::string& src) : s(src) {}
    Val parse() { Val v = value(); ws(); if (i != s.size()) fail("trailing data"); return v; }
    Val value() {
        ws(); if (i >= s.size()) fail("eof"); Val v; char c = s[i];
        if (c == '{') { v.t = Val::Obj; ++i; ws(); if (s[i] == '}') { ++i; return v; }
            for (;;) { ws(); std::string k = str(); ws(); if (s[i] != ':') fail("expected :"); ++i; v.o.emplace_back(k, value()); ws();
                if (s[i] == ',') { ++i; continue; } if (s[i] == '}') { ++i; break; } fail("expected , or }"); } }
        else if (c == '[') { v.t = Val::Arr; ++i; ws(); if (s[i] == ']') { ++i; return v; }
            for (;;) { v.a.push_back(value()); ws(); if (s[i] == ',') { ++i; continue; } if (s[i] == ']') { ++i; break; } fail("expected , or ]"); } }
        else if (c == '"') { v.t = Val::Str; v.s = str(); }
        else if (s.compare(i, 4, "true") == 0) { v.t = Val::Bool; v.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.t = Val::Bool; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else { size_t e = i; while (e < s.size() && (isdigit((unsigned char)s[e])||s[e]=='-'||s[e]=='+'||s[e]=='.'||s[e]=='e'||s[e]=='E')) ++e;
            if (e == i) { fail("bad token"); }
            v.t = Val::Num; v.n = std::stod(s.substr(i, e - i)); i = e; }
        return v;
    }
};
inline Val parse(const std::string& src) { return Parser(src).parse(); }
}
