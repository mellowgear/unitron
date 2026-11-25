#include "mpp.hpp"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "expr.hpp"
#include "operands.hpp"
#include "util.hpp"

namespace unitron {

std::string thome_include() {
    const char* th = std::getenv("THOME900");
    if (!th || !*th) return "";
    return path_join(th, "include");
}

namespace {

bool word(unsigned char c) { return py_alnum(c) || c == '_'; }
bool id0(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool idc(unsigned char c) { return id0(c) || (c >= '0' && c <= '9'); }
bool after_ident_quote(const std::string& t, size_t i) {
    return i && (py_alnum(t[i - 1]) || t[i - 1] == '_' || t[i - 1] == '.');
}

// Drop // and /* */ comments; keep ; comments. block carries /* across lines.
std::string strip_comments(const std::string& text, bool& block) {
    std::string out;
    size_t i = 0, n = text.size();
    char q = 0;
    while (i < n) {
        char c = text[i];
        if (block) {
            size_t j = text.find("*/", i);
            if (j == std::string::npos) return out;
            block = false;
            i = j + 2;
            continue;
        }
        if (q) {
            out += c;
            if (c == '\\' && i + 1 < n) {
                out += text[i + 1];
                i += 2;
                continue;
            }
            if (c == q) q = 0;
            i++;
            continue;
        }
        if (c == ';') {
            out += text.substr(i);
            break;
        }
        if (c == '"' || (c == '\'' && !after_ident_quote(text, i))) {
            q = c;
        } else if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            break;
        } else if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            block = true;
            i += 2;
            continue;
        }
        out += c;
        i++;
    }
    return out;
}

struct Def {
    bool fn = false;
    std::vector<std::string> params;
    std::string body;
};

struct Macro {
    std::vector<std::string> params, labels, body;
    Where where;
};

// Arguments of a function-like macro call starting at '(' -> (list, index after ')').
std::vector<std::string> call_args(const std::string& line, size_t k, const Where& where, size_t& end) {
    int depth = 0;
    std::vector<std::string> args;
    std::string cur;
    char q = 0;
    for (size_t j = k; j < line.size(); j++) {
        char c = line[j];
        if (q) {
            cur += c;
            if (c == q) q = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            q = c;
            cur += c;
            continue;
        }
        if (c == '(') {
            depth++;
            if (depth == 1) continue;
        } else if (c == ')') {
            depth--;
            if (depth == 0) {
                args.push_back(strip(cur));
                end = j + 1;
                return args;
            }
        } else if (c == ',' && depth == 1) {
            args.push_back(strip(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    throw MppError("unterminated macro call", where);
}

std::string expand_fn(const Def& d, const std::vector<std::string>& args, const std::vector<std::string>& raw) {
    std::map<std::string, size_t> idx;
    for (size_t i = 0; i < d.params.size(); i++) idx[d.params[i]] = i;
    const std::string& b0 = d.body;
    // #param -> "raw argument"
    std::string b1;
    for (size_t i = 0; i < b0.size();) {
        if (b0[i] == '#') {
            size_t j = i + 1;
            while (j < b0.size() && py_space(b0[j])) j++;
            if (j < b0.size() && id0(b0[j])) {
                size_t e = j + 1;
                while (e < b0.size() && word(b0[e])) e++;
                std::string name = b0.substr(j, e - j);
                auto it = idx.find(name);
                b1 += it != idx.end() ? "\"" + raw[it->second] + "\"" : b0.substr(i, e - i);
                i = e;
                continue;
            }
        }
        b1 += b0[i++];
    }
    // \bparam\b -> argument
    std::string b2;
    for (size_t i = 0; i < b1.size();) {
        if (id0(b1[i]) && (i == 0 || !word(b1[i - 1]))) {
            size_t e = i + 1;
            while (e < b1.size() && word(b1[e])) e++;
            std::string name = b1.substr(i, e - i);
            auto it = idx.find(name);
            b2 += it != idx.end() ? args[it->second] : name;
            i = e;
            continue;
        }
        b2 += b1[i++];
    }
    // \s*##\s* -> ""
    std::string b3;
    for (size_t i = 0; i < b2.size();) {
        size_t k = i;
        while (k < b2.size() && py_space(b2[k])) k++;
        if (b2.compare(k, 2, "##") == 0) {
            k += 2;
            while (k < b2.size() && py_space(b2[k])) k++;
            i = k;
            continue;
        }
        if (k > i) {
            b3 += b2.substr(i, k - i);
            i = k;
            continue;
        }
        b3 += b2[i++];
    }
    return b3;
}

int64_t floordiv_mod(int64_t v, int64_t w) {
    int64_t r = v % w;
    if (r != 0 && ((r < 0) != (w < 0))) r += w;
    return r;
}

// Evaluate a #if expression (C operators, TR p.60 precedence).
int64_t cexpr(const std::string& t, const Where& where) {
    std::vector<std::string> toks;
    bool bad = false;
    static const char* two[] = {"&&", "||", "==", "!=", ">=", "<=", "<<", ">>"};
    for (size_t i = 0; i < t.size();) {
        char c = t[i];
        if (c == '0' && i + 2 < t.size() + 0 && i + 1 < t.size() && (t[i + 1] == 'x' || t[i + 1] == 'X') && i + 2 < t.size() &&
            std::isxdigit((unsigned char)t[i + 2])) {
            size_t j = i + 2;
            while (j < t.size() && std::isxdigit((unsigned char)t[j])) j++;
            toks.push_back(t.substr(i, j - i));
            i = j;
            continue;
        }
        if (c == '0' && i + 2 < t.size() && std::string("bByY").find(t[i + 1]) != std::string::npos &&
            (t[i + 2] == '0' || t[i + 2] == '1')) {
            size_t j = i + 2;
            while (j < t.size() && (t[j] == '0' || t[j] == '1')) j++;
            toks.push_back(t.substr(i, j - i));
            i = j;
            continue;
        }
        if (c >= '0' && c <= '9') {
            size_t j = i;
            while (j < t.size() && t[j] >= '0' && t[j] <= '9') j++;
            toks.push_back(t.substr(i, j - i));
            i = j;
            continue;
        }
        bool got = false;
        for (auto tw : two)
            if (t.compare(i, 2, tw) == 0) {
                toks.push_back(tw);
                i += 2;
                got = true;
                break;
            }
        if (got) continue;
        if (std::string("-+*/%&|^~!()<>").find(c) != std::string::npos) {
            toks.push_back(std::string(1, c));
            i++;
            continue;
        }
        if (!py_space(c)) bad = true;
        i++;
    }
    if (bad) throw MppError("bad #if expression " + py_repr(t), where);
    size_t pos = 0;
    static const std::vector<std::vector<std::string>> prec = {
        {"||"}, {"&&"}, {"^"}, {"|"}, {"&"}, {"==", "!="}, {">", "<", ">=", "<="}, {">>", "<<"}, {"+", "-"}, {"*", "/", "%"}};
    auto peek = [&]() -> const std::string* { return pos < toks.size() ? &toks[pos] : nullptr; };
    std::function<int64_t(size_t)> level;
    std::function<int64_t()> unary = [&]() -> int64_t {
        const std::string* tk = peek();
        pos++;
        if (tk && (*tk == "-" || *tk == "+" || *tk == "~" || *tk == "!")) {
            std::string op = *tk;
            int64_t v = unary();
            if (op == "-") return -v;
            if (op == "+") return v;
            if (op == "~") return ~v;
            return v ? 0 : 1;
        }
        if (tk && *tk == "(") {
            int64_t v = level(0);
            const std::string* c = peek();
            if (!c || *c != ")") throw MppError("unbalanced parentheses in #if", where);
            pos++;
            return v;
        }
        if (!tk) throw MppError("#if expression ends early", where);
        try {
            return parse_number(*tk);
        } catch (ExprError& e) {
            throw MppError(e.what(), where);
        }
    };
    level = [&](size_t i) -> int64_t {
        if (i == prec.size()) return unary();
        int64_t v = level(i + 1);
        while (true) {
            const std::string* p = peek();
            bool in = false;
            if (p)
                for (auto& o : prec[i]) in |= *p == o;
            if (!in) break;
            std::string op = *p;
            pos++;
            int64_t w = level(i + 1);
            if (op == "||") v = (v || w) ? 1 : 0;
            else if (op == "&&") v = (v && w) ? 1 : 0;
            else if (op == "^") v = v ^ w;
            else if (op == "|") v = v | w;
            else if (op == "&") v = v & w;
            else if (op == "==") v = v == w;
            else if (op == "!=") v = v != w;
            else if (op == ">") v = v > w;
            else if (op == "<") v = v < w;
            else if (op == ">=") v = v >= w;
            else if (op == "<=") v = v <= w;
            else if (op == ">>") {
                if (w < 0) throw MppError("negative shift count", where);
                v = w >= 64 ? (v < 0 ? -1 : 0) : v >> w;
            } else if (op == "<<") {
                if (w < 0) throw MppError("negative shift count", where);
                v = w >= 64 ? 0 : int64_t(uint64_t(v) << w);
            } else if (op == "+") v = int64_t(uint64_t(v) + uint64_t(w));
            else if (op == "-") v = int64_t(uint64_t(v) - uint64_t(w));
            else if (op == "*") v = int64_t(uint64_t(v) * uint64_t(w));
            else if (op == "/") v = w ? v / w : 0;
            else if (op == "%") v = w ? floordiv_mod(v, w) : 0;
        }
        return v;
    };
    int64_t v = level(0);
    if (pos != toks.size()) throw MppError("bad #if expression " + py_repr(t), where);
    return v;
}

struct Pre {
    std::vector<std::string> include_dirs;
    std::map<std::string, Def> defs;
    std::map<std::string, Macro> macros;
    std::map<std::string, int> counters;

    std::string find(const std::string& name, const std::string& cur, bool angle) {
        if (path_absolute(name)) return is_file(name) ? path_norm(name) : "";
        std::vector<std::string> dirs;
        if (!angle) dirs.push_back(path_parent(cur));
        for (auto& d : include_dirs) dirs.push_back(d);
        for (auto& d : dirs) {
            std::string p = path_join(d, name);
            if (is_file(p)) return p;
        }
        return "";
    }

    void file(const std::string& path, const std::vector<Loc>& chain, int depth, std::vector<SrcLine>& out) {
        Where cw = chain.empty() ? Where::None() : Where{chain.back().file, chain.back().line, {}, false};
        if (depth > 255) throw MppError("#include nested deeper than 255 levels", cw);
        if (!chain.empty()) {
            // the Where of the #include line carries its own chain
            cw.chain.assign(chain.begin(), chain.end() - 1);
        }
        Bytes data;
        if (!read_bytes_opt(path, data)) throw MppError("can't open " + py_repr(path), cw);
        std::string text = universal_newlines(std::string(data.begin(), data.end()));
        std::vector<std::string> raw = split_on(text, "\n");
        if (!raw.empty() && raw.back().empty()) raw.pop_back();
        bool block = false;
        struct C {
            bool active, taken, parent;
        };
        std::vector<C> cond;
        size_t i = 0;
        while (i < raw.size()) {
            int ln = int(i + 1);
            std::string line = rstrip_chars(raw[i], "\r");
            while (ends_with(line, "\\") && i + 1 < raw.size()) {
                i++;
                line = line.substr(0, line.size() - 1) + rstrip_chars(raw[i], "\r");
            }
            i++;
            Where where{path, ln, chain, false};
            line = strip_comments(line, block);
            std::string s = strip(line);
            bool active = true;
            for (auto& c : cond) active = active && c.active;
            if (!s.empty() && s[0] == '#') {
                std::string d = strip(s.substr(1));
                size_t k = 0;
                while (k < d.size() && ((d[k] >= 'a' && d[k] <= 'z') || (d[k] >= 'A' && d[k] <= 'Z'))) k++;
                if (k == 0) {
                    if (active && !d.empty()) throw MppError("bad directive #" + d, where);
                    continue;
                }
                std::string kw = lower(d.substr(0, k));
                size_t r = k;
                while (r < d.size() && py_space(d[r])) r++;
                std::string rest = d.substr(r);
                if (kw == "if" || kw == "ifdef" || kw == "ifndef") {
                    if (!active) {
                        cond.push_back({false, true, false});
                        continue;
                    }
                    bool v;
                    if (kw == "if") {
                        v = cond_value(rest, where);
                    } else {
                        auto w = split_ws(rest);
                        v = !w.empty() && defs.count(w[0]);
                        if (kw == "ifndef") v = !v;
                    }
                    cond.push_back({v, v, true});
                    if (cond.size() > 63) throw MppError("#if nesting deeper than 63", where);
                    continue;
                }
                if (kw == "elif" || kw == "else" || kw == "endif") {
                    if (cond.empty()) throw MppError("#" + kw + " without #if", where);
                    C& top = cond.back();
                    if (kw == "endif") {
                        cond.pop_back();
                    } else if (kw == "else") {
                        top.active = top.parent && !top.taken;
                        top.taken = true;
                    } else {
                        bool v = top.parent && !top.taken && cond_value(rest, where);
                        top.active = v;
                        top.taken = top.taken || v;
                    }
                    continue;
                }
                if (!active) continue;
                if (kw == "include") {
                    std::string rs = strip(rest);
                    std::string name;
                    bool angle = false;
                    bool ok = false;
                    if (!rs.empty() && rs[0] == '"') {
                        size_t e = rs.find('"', 1);
                        if (e != std::string::npos && e > 1) {
                            name = rs.substr(1, e - 1);
                            ok = true;
                        }
                    } else if (!rs.empty() && rs[0] == '<') {
                        size_t e = rs.find('>', 1);
                        if (e != std::string::npos && e > 1) {
                            name = rs.substr(1, e - 1);
                            angle = ok = true;
                        }
                    }
                    if (!ok) throw MppError("#include needs \"file\" or <file>", where);
                    std::string f = find(name, path, angle);
                    if (f.empty()) throw MppError("can't find include file " + py_repr(name), where);
                    std::vector<Loc> ch = chain;
                    ch.push_back(where.loc());
                    file(f, ch, depth + 1, out);
                } else if (kw == "define") {
                    define(rest, where);
                } else if (kw == "undef") {
                    defs.erase(strip(rest));
                } else if (kw == "error") {
                    throw MppError("#error " + rest, where);
                } else if (kw == "line") {
                } else {
                    throw MppError("unknown directive #" + kw, where);
                }
                continue;
            }
            if (!active) continue;
            out.push_back({subst(line, where, 0, {}), where});
        }
        if (!cond.empty()) throw MppError("#if without #endif", Where{path, int(raw.size()), chain, false});
    }

    void define(const std::string& rest, const Where& where) {
        size_t i = 0;
        if (rest.empty() || !id0(rest[0])) throw MppError("bad #define", where);
        while (i < rest.size() && idc(rest[i])) i++;
        std::string name = rest.substr(0, i);
        Def d;
        if (i < rest.size() && rest[i] == '(') {
            size_t e = rest.find(')', i);
            if (e != std::string::npos) {
                std::string ps = rest.substr(i + 1, e - i - 1);
                d.fn = true;
                if (!strip(ps).empty())
                    for (auto& p : split_on(ps, ",")) d.params.push_back(strip(p));
                if (d.params.size() > 127) throw MppError("more than 127 #define parameters", where);
                i = e + 1;
            }
        }
        while (i < rest.size() && py_space(rest[i])) i++;
        d.body = strip(rest.substr(i));
        defs[name] = d;
    }

    bool cond_value(const std::string& text, const Where& where) {
        // defined(X) / defined X -> 1/0
        std::string t;
        for (size_t i = 0; i < text.size();) {
            if (text.compare(i, 7, "defined") == 0 && (i == 0 || !word(text[i - 1]))) {
                size_t j = i + 7;
                bool done = false;
                size_t k = j;
                while (k < text.size() && py_space(text[k])) k++;
                if (k < text.size() && text[k] == '(') {
                    size_t a = k + 1;
                    while (a < text.size() && py_space(text[a])) a++;
                    if (a < text.size() && id0(text[a])) {
                        size_t b = a + 1;
                        while (b < text.size() && word(text[b])) b++;
                        size_t c = b;
                        while (c < text.size() && py_space(text[c])) c++;
                        if (c < text.size() && text[c] == ')') {
                            t += defs.count(text.substr(a, b - a)) ? "1" : "0";
                            i = c + 1;
                            done = true;
                        }
                    }
                }
                if (!done && k > j && k < text.size() && id0(text[k])) {
                    size_t b = k + 1;
                    while (b < text.size() && word(text[b])) b++;
                    t += defs.count(text.substr(k, b - k)) ? "1" : "0";
                    i = b;
                    done = true;
                }
                if (done) continue;
            }
            t += text[i++];
        }
        t = subst(t, where, 0, {});
        // undefined identifiers are 0
        std::string u;
        for (size_t i = 0; i < t.size();) {
            if (id0(t[i]) && (i == 0 || !word(t[i - 1]))) {
                size_t e = i + 1;
                while (e < t.size() && word(t[e])) e++;
                u += "0";
                i = e;
                continue;
            }
            u += t[i++];
        }
        return cexpr(u, where) != 0;
    }

    std::string subst(const std::string& line, const Where& where, int depth, const std::set<std::string>& active) {
        if (defs.empty() || depth > 64) return line;
        std::string out;
        size_t i = 0, n = line.size();
        char q = 0;
        while (i < n) {
            char c = line[i];
            if (q) {
                out += c;
                if (c == '\\' && i + 1 < n) {
                    out += line[i + 1];
                    i += 2;
                    continue;
                }
                if (c == q) q = 0;
                i++;
                continue;
            }
            if (c == ';') {
                out += line.substr(i);
                break;
            }
            if (c == '"' || (c == '\'' && !after_ident_quote(line, i))) {
                q = c;
                out += c;
                i++;
                continue;
            }
            if (c == '.' && i + 1 < n && id0(line[i + 1]) && !(i && (idc(line[i - 1]) || line[i - 1] == '.'))) {
                // `.name` (a local-label tail): one asm900 identifier, never a #define name
                size_t j = i + 1;
                while (j < n && (idc(line[j]) || line[j] == '.')) j++;
                out += line.substr(i, j - i);
                i = j;
                continue;
            }
            if (id0(c)) {
                size_t j = i + 1;
                while (j < n && idc(line[j])) j++;
                if (j < n && line[j] == '.' && j + 1 < n && id0(line[j + 1])) {
                    // `Routine.loop` is one asm900 identifier (STATIC 3.4); tumpp names have no '.',
                    // so a dotted name is never a #define name and is copied whole.
                    while (j < n && (idc(line[j]) || line[j] == '.')) j++;
                    out += line.substr(i, j - i);
                    i = j;
                    continue;
                }
                std::string name = line.substr(i, j - i);
                bool prev_digit = i && py_digit(line[i - 1]);
                auto it = defs.find(name);
                if (it == defs.end() || active.count(name) || prev_digit) {
                    out += name;
                    i = j;
                    continue;
                }
                Def d = it->second;
                std::set<std::string> act2 = active;
                act2.insert(name);
                if (!d.fn) {
                    out += subst(d.body, where, depth + 1, act2);
                    i = j;
                    continue;
                }
                size_t k = j;
                while (k < n && (line[k] == ' ' || line[k] == '\t')) k++;
                if (k >= n || line[k] != '(') {
                    out += name;
                    i = j;
                    continue;
                }
                size_t end = 0;
                auto args = call_args(line, k, where, end);
                if (args.size() != d.params.size() && !(d.params.empty() && args.size() == 1 && args[0].empty()))
                    throw MppError(name + " takes " + std::to_string(d.params.size()) + " arguments", where);
                std::vector<std::string> sa;
                for (auto& a : args) sa.push_back(subst(a, where, depth + 1, active));
                std::string body = expand_fn(d, sa, args);
                out += subst(body, where, depth + 1, act2);
                i = end;
                continue;
            }
            out += c;
            i++;
        }
        return out;
    }

    // ---- ?macro
    std::vector<SrcLine> run_macros(const std::vector<SrcLine>& lines) {
        std::vector<SrcLine> out;
        size_t i = 0;
        while (i < lines.size()) {
            const std::string& text = lines[i].first;
            const Where& where = lines[i].second;
            std::string s = strip(text);
            if (starts_with(lower(s), "?macro")) {
                i = def_macro(lines, i);
                continue;
            }
            if (!s.empty() && s[0] == '?') {
                size_t e = 1;
                while (e < s.size() && word(s[e])) e++;
                std::string w = lower(s.substr(1, e - 1));
                if (w == "endm") throw MppError("?endm without ?macro", where);
                // Outside a macro body a `?` word is a macro function (TR ch. 15): only ?macro/?endm
                // are implemented, so every other one is rejected here rather than reaching the
                // assembler as an unknown mnemonic.
                throw MppError("?" + w + " is not supported (only ?macro/?endm)", where);
            }
            expand_line(text, where, 0, out);
            i++;
        }
        return out;
    }

    static bool kw_at(const std::string& t, const char* kw, size_t& after) {
        // \s*<kw> (case-insensitive); after = index after kw
        size_t i = 0;
        while (i < t.size() && py_space(t[i])) i++;
        size_t n = std::char_traits<char>::length(kw);
        if (lower(t.substr(i, n)) != kw) return false;
        after = i + n;
        return true;
    }

    size_t def_macro(const std::vector<SrcLine>& lines, size_t i) {
        const std::string& text = lines[i].first;
        const Where& where = lines[i].second;
        size_t a = 0;
        bool ok = kw_at(text, "?macro", a);
        size_t b = a;
        while (b < text.size() && py_space(text[b])) b++;
        if (!ok || b == a || b >= text.size() || !id0(text[b])) throw MppError("bad ?macro line", where);
        size_t e = b + 1;
        while (e < text.size() && word(text[e])) e++;
        std::string name = text.substr(b, e - b);
        while (e < text.size() && py_space(text[e])) e++;
        std::string rest = text.substr(e);
        Macro m;
        for (auto& p : split_on(split_on(rest, ";")[0], ","))
            if (!strip(p).empty()) m.params.push_back(strip(p));
        if (macros.count(name)) throw MppError("macro " + name + " defined twice", where);
        if (m.params.size() > 127) throw MppError("more than 127 macro parameters", where);
        size_t j = i + 1;
        if (j < lines.size()) {
            size_t la = 0;
            const std::string& t = lines[j].first;
            if (kw_at(t, "labels", la) && la < t.size() && py_space(t[la])) {
                size_t r = la;
                while (r < t.size() && py_space(t[r])) r++;
                for (auto& x : split_on(split_on(t.substr(r), ";")[0], ","))
                    if (!strip(x).empty()) m.labels.push_back(strip(x));
                if (m.labels.size() > 64) throw MppError("more than 64 macro labels", lines[j].second);
                j++;
            }
        }
        while (j < lines.size()) {
            const std::string& t = lines[j].first;
            size_t ea = 0;
            if (kw_at(t, "?endm", ea) && (ea >= t.size() || !word(t[ea]))) {
                m.where = where;
                macros[name] = m;
                return j + 1;
            }
            if (kw_at(t, "?macro", ea) && (ea >= t.size() || !word(t[ea])))
                throw MppError("nested ?macro definition", lines[j].second);
            m.body.push_back(t);
            j++;
        }
        throw MppError("?macro " + name + " has no ?endm", where);
    }

    void expand_line(const std::string& text, const Where& where, int depth, std::vector<SrcLine>& out) {
        if (macros.empty()) {
            out.push_back({text, where});
            return;
        }
        std::string code = text.substr(0, text.find(';'));
        // ^(\s*)(?:([A-Za-z_.][\w.]*):)?(\s*)([A-Za-z_]\w*)\b(.*)$
        size_t g1 = 0;
        while (g1 < code.size() && py_space(code[g1])) g1++;
        std::string label;
        bool has_label = false;
        size_t p4 = std::string::npos;
        if (g1 < code.size() && (id0(code[g1]) || code[g1] == '.')) {
            size_t e = g1 + 1;
            while (e < code.size() && (word(code[e]) || code[e] == '.')) e++;
            if (e < code.size() && code[e] == ':') {
                size_t k = e + 1;
                while (k < code.size() && py_space(code[k])) k++;
                if (k < code.size() && id0(code[k])) {
                    has_label = true;
                    label = code.substr(g1, e - g1);
                    p4 = k;
                }
            }
        }
        if (!has_label) {
            if (g1 < code.size() && id0(code[g1])) p4 = g1;
        }
        if (p4 == std::string::npos) {
            out.push_back({text, where});
            return;
        }
        size_t e4 = p4 + 1;
        while (e4 < code.size() && word(code[e4])) e4++;
        std::string name = code.substr(p4, e4 - p4);
        std::string g5 = code.substr(e4);
        auto it = macros.find(name);
        if (it == macros.end()) {
            out.push_back({text, where});
            return;
        }
        if (g1 == 0 && !has_label) {
            out.push_back({text, where});
            return;
        }
        if (depth > 32) throw MppError("macro nesting deeper than 32", where);
        const Macro& m = it->second;
        std::vector<std::string> args;
        if (!strip(g5).empty()) args = split_operands(g5);
        if (args.size() > m.params.size())
            throw MppError(name + " takes " + std::to_string(m.params.size()) + " arguments, got " + std::to_string(args.size()), where);
        args.resize(m.params.size());
        std::map<std::string, std::string> sub;
        for (size_t i = 0; i < m.params.size(); i++)
            if (!sub.count(m.params[i]) || true) sub[m.params[i]] = args[i];
        for (auto& lab : m.labels) {
            int k = counters.count(lab) ? counters[lab] : 0;
            if (k > 999) throw MppError("more than 1000 expansions of macro label " + lab, where);
            counters[lab] = k + 1;
            std::string num = std::to_string(k);
            while (num.size() < 3) num = "0" + num;
            sub[lab] = lab + num;
        }
        if (has_label && !label.empty()) out.push_back({label + ":", where});
        for (auto& b : m.body) {
            std::string t;
            for (size_t i = 0; i < b.size();) {
                if (b[i] == '?' && i + 1 < b.size() && id0(b[i + 1])) {
                    size_t e = i + 2;
                    while (e < b.size() && word(b[e])) e++;
                    std::string nm = b.substr(i + 1, e - i - 1);
                    auto s = sub.find(nm);
                    t += s != sub.end() ? s->second : b.substr(i, e - i);
                    i = e;
                    continue;
                }
                t += b[i++];
            }
            expand_line(t, where, depth + 1, out);
        }
    }
};

}  // namespace

std::vector<SrcLine> preprocess(const std::string& path0, const std::vector<std::string>& include_dirs, const Defines& defines) {
    Pre p;
    for (auto& d : include_dirs) p.include_dirs.push_back(path_norm(d));
    std::string th = thome_include();
    if (!th.empty()) p.include_dirs.push_back(th);
    p.defs["__900__"] = {false, {}, "1"};
    p.defs["__TOSHIBA__"] = {false, {}, "1"};
    p.defs["__TRUE__"] = {false, {}, "1"};
    p.defs["__FALSE__"] = {false, {}, "0"};
    for (auto& kv : defines) p.defs[kv.first] = {false, {}, kv.second};
    std::vector<SrcLine> lines;
    p.file(path_norm(path0), {}, 0, lines);
    return p.run_macros(lines);
}

}  // namespace unitron
