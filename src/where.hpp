// Source positions and diagnostics.
#pragma once
#include <string>
#include <vector>

namespace unitron {

struct Loc {
    std::string file;
    int line = 0;
    std::string str() const { return file + ":" + std::to_string(line); }
};

struct Where {
    std::string file;
    int line = 0;
    std::vector<Loc> chain;   // include chain, outermost first
    bool none = false;        // Python's None
    std::string str() const { return file + ":" + std::to_string(line); }
    Loc loc() const { return {file, line}; }
    static Where None() {
        Where w;
        w.none = true;
        return w;
    }
};

struct Diags {
    std::vector<std::string> errors, warnings;
    void error(const Where& w, int code, const std::string& msg) { errors.push_back(fmt(w, "error", code, msg)); }
    void warn(const Where& w, int code, const std::string& msg) { warnings.push_back(fmt(w, "warning", code, msg)); }
    static std::string fmt(const Where& w, const char* kind, int code, const std::string& msg) {
        std::string head = w.none ? "" : w.str() + ": ";
        std::string k = kind;
        std::string num = code && k == "error" ? " E" + std::to_string(code) : code ? " W" + std::to_string(code) : "";
        std::string s = head + k + num + ": " + msg;
        if (!w.none)
            for (auto& c : w.chain) s += "\n    included from " + c.str();
        return s;
    }
};

}  // namespace unitron
