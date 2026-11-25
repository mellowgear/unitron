#include "assembler.hpp"
#include "rom.hpp"

#include <algorithm>
#include <set>

#include "lcf.hpp"
#include "line.hpp"
#include "regs.hpp"
#include "util.hpp"

namespace unitron {

namespace {

int data_w(const std::string& u) {
    if (u == "DB") return 1;
    if (u == "DW") return 2;
    if (u == "DP") return 3;
    if (u == "DD" || u == "DL") return 4;
    return 0;
}
int fill_w(const std::string& u) {
    if (u == "DFB") return 1;
    if (u == "DFW") return 2;
    if (u == "DFP") return 3;
    if (u == "DFD" || u == "DFL") return 4;
    return 0;
}
int space_w(const std::string& u) {
    if (u == "DSB") return 1;
    if (u == "DSW") return 2;
    if (u == "DSP") return 3;
    if (u == "DSD" || u == "DSL") return 4;
    return 0;
}
bool naming(const std::string& u) { return u == "EQU" || u == "SECTION" || data_w(u) || fill_w(u) || space_w(u); }

const std::set<std::string>& DIRECTIVES() {
    static const std::set<std::string> s = {"EQU", "SECTION", "DB", "DW", "DP", "DD", "DL", "DFB", "DFW", "DFP", "DFD",
                                            "DFL", "DSB", "DSW", "DSP", "DSD", "DSL", "MODULE", "END", "ORG", "ALIGN",
                                            "PUBLIC", "EXTERN", "FDATAS", "FDATAD", "FDATAX", "FILE", "LINE",
                                            "SYMBOL_A", "SYMBOL_B", "SYMBOL_N", "SYMBOL_T"};
    return s;
}
const std::set<std::string> UNSUPPORTED = {"FDATAS", "FDATAD", "FDATAX", "FILE", "LINE", "SYMBOL_A", "SYMBOL_B", "SYMBOL_N", "SYMBOL_T"};
const std::set<std::string> LISTING = {"LIST", "NOLIST", "TITLE", "EJECT", "SAVE", "RESTORE", "LI", "NOLI", "TT", "EJ", "SA", "RS"};

int attr_w(const std::string& u) { return u == "SMALL" ? 8 : u == "MEDIUM" ? 16 : u == "LARGE" ? 24 : 0; }
const char* attr_name(int w) { return w == 8 ? "small" : w == 16 ? "medium" : "large"; }
int64_t attr_limit(int w) { return w == 8 ? 0x100 : w == 16 ? 0x10000 : 0x1000000; }

bool numtok(const std::string& p) {
    if (p.size() > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        for (size_t i = 2; i < p.size(); i++)
            if (!std::isxdigit((unsigned char)p[i])) return false;
        return true;
    }
    if (p == "0") return true;
    if (p.empty() || p[0] < '1' || p[0] > '9') return false;
    for (char c : p)
        if (c < '0' || c > '9') return false;
    return true;
}

bool ident(const std::string& s) {
    if (s.empty()) return false;
    auto first = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.'; };
    if (!first(s[0])) return false;
    for (char c : s)
        if (!first(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}

bool reserved_word(const std::string& w) {
    std::string u = upper(w), b;
    int s;
    return DIRECTIVES().count(u) || split_mnemonic(u, b, s);
}

int64_t pymod(int64_t a, int64_t b) {
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

struct Stmt {
    bool has_label = false, has_op = false;
    std::string label, op, field;
};

// (\S+)\s*(.*)$ on a non-empty string without leading space
void op_field(const std::string& s, std::string& op, std::string& field) {
    size_t i = 0;
    while (i < s.size() && !py_space(s[i])) i++;
    op = s.substr(0, i);
    while (i < s.size() && py_space(s[i])) i++;
    field = s.substr(i);
}

Stmt parse_statement(const std::string& code) {
    Stmt st;
    std::string s = rstrip(code);
    if (strip(s).empty()) return st;
    if (!py_space(s[0])) {
        size_t i = 0;
        while (i < s.size() && !py_space(s[i]) && s[i] != ':') i++;
        if (i == 0) throw AsmError(200, "syntax error: a line cannot start with ':'");
        std::string tok = s.substr(0, i);
        bool colon = i < s.size() && s[i] == ':';
        std::string rest = s.substr(i + (colon ? 1 : 0));
        if (colon) {
            st.has_label = true;
            st.label = tok;
            s = rest;
        } else if (!(tok[0] == '$' || reserved_word(tok))) {
            st.has_label = true;
            st.label = tok;
            s = rest;
        }
    }
    s = strip(s);
    if (s.empty()) return st;
    std::string op, field;
    op_field(s, op, field);
    if (!st.has_label && ends_with(op, ":") && op.size() > 1) {
        st.has_label = true;
        st.label = op.substr(0, op.size() - 1);
        s = field;
        if (s.empty()) return st;
        op_field(s, op, field);
    } else if (!st.has_label && !reserved_word(op) && op[0] != '$') {
        if (!field.empty()) {
            std::string op2, f2;
            op_field(field, op2, f2);
            if (naming(upper(op2))) {
                st.has_label = true;
                st.label = op;
                op = op2;
                field = f2;
            }
        }
    }
    st.has_op = true;
    st.op = op;
    st.field = field;
    return st;
}

std::vector<std::string> names_of(const std::string& field) {
    std::vector<std::string> out;
    for (auto& n0 : split_on(field, ",")) {
        std::string n = strip(n0);
        if (n.empty()) continue;
        if (!ident(n)) throw AsmError(208, "illegal symbol name " + py_repr(n));
        out.push_back(n);
    }
    return out;
}

std::vector<DVal> data_values(const std::string& field, int w) {
    std::vector<DVal> out;
    for (auto& p : split_operands(field)) {
        DVal v;
        if (p.empty()) throw AsmError(206, "empty value");
        if (numtok(p)) {
            Number n = parse_number_full(p);
            v.k = DVal::Int;
            v.v = n.v;
            v.big = n.big;
            v.decimal = n.decimal;
        } else if (p[0] == '"') {
            if (w != 1) throw AsmError(209, "strings are allowed in db/dfb only");
            if (p.size() < 2 || p.back() != '"') throw AsmError(209, "unterminated string");
            v.k = DVal::Str;
            try {
                v.s = unescape(p.substr(1, p.size() - 2));
            } catch (ExprError& e) {
                throw AsmError(209, e.what());   // TR error 209, as for an unterminated string
            }
            if (v.s.size() > 511) throw AsmError(209, "string longer than 511 bytes");
        } else {
            ExprP e = parse_expr(p);
            if (e->is_const()) {
                v.k = DVal::Int;
                v.v = e->v;
            } else {
                v.k = DVal::Ex;
                v.e = e;
            }
        }
        out.push_back(v);
    }
    return out;
}

std::string read_source(const std::string& path) {
    Bytes b;
    if (!read_bytes_opt(path, b)) throw AsmError(20, "can't open " + py_repr(path));
    return universal_newlines(std::string(b.begin(), b.end()));
}

}  // namespace

std::string split_comment(const std::string& line) {
    if (line.find(';') == std::string::npos) return line;
    char q = 0;
    bool esc = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (q) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == q) q = 0;
            continue;
        }
        if (c == ';') return line.substr(0, i);
        if (c == '"' || (c == '\'' && !(i && (py_alnum(line[i - 1]) || line[i - 1] == '_' || line[i - 1] == '.')))) q = c;
    }
    return line;
}

bool is_reserved(const std::string& name) {
    static const std::set<std::string> extra = {"F", "F'", "SR", "CODE", "DATA", "ROMDATA", "SIZEOF", "STARTOF", "INCLUDE",
                                                "MAX", "MIN", "LIST", "LI", "TITLE", "TT", "SA", "RS", "EJ", "NOR", "SYS",
                                                "BANK", "LOC0", "COM0", "SMALL", "MEDIUM", "LARGE", "ABS"};
    std::string u = upper(name), b;
    int s;
    if (REG().count(u) || cc_code(u) >= 0 || CR_CODE().count(u) || DIRECTIVES().count(u) || extra.count(u)) return true;
    return split_mnemonic(u, b, s);
}

int Section::width() const { return has_abs ? 0 : attr_w(upper(attr)); }

Assembler::Assembler(const std::vector<std::string>& include_dirs, bool strict, bool mpp, const Defines& defines, bool optimize)
    : strict_(strict), use_mpp_(mpp), optimize_(optimize), defines_(defines) {
    for (auto& d : include_dirs) include_dirs_.push_back(path_norm(d));
    std::string th = thome_include();
    if (!th.empty()) include_dirs_.push_back(th);
}

std::vector<SrcLine> Assembler::lines_of(const std::string& path) {
    std::vector<SrcLine> out;
    if (use_mpp_) {
        IncludeHook hook = [this](const std::string& name, const std::string& found) {
            if (!found.empty()) {
                add_dep(found);
                return true;
            }
            if (!missing_ok_) return true;   // the preprocessor reports it
            add_dep(name);
            missing = true;
            return false;
        };
        try {
            return preprocess(path, include_dirs_, defines_, track_ ? &hook : nullptr);
        } catch (MppError& e) {
            d.error(e.where, 0, e.what());
            return {};
        }
    }
    std::string text;
    try {
        text = read_source(path);
    } catch (AsmError& e) {
        d.error(Where::None(), 0, e.what());
        return {};
    }
    int n = 1;
    for (auto& t : splitlines(text)) out.push_back({t, Where{path, n++, {}, false}});
    return out;
}

std::string Assembler::find_include(const std::string& name, const std::string& cur) {
    if (path_absolute(name)) return is_file(name) ? path_norm(name) : "";
    std::vector<std::string> dirs{path_parent(cur)};
    for (auto& d : include_dirs_) dirs.push_back(d);
    for (auto& dd : dirs) {
        std::string p = path_join(dd, name);
        if (is_file(p)) return p;
    }
    return "";
}

void Assembler::add_module(const std::string& path0) {
    std::string path = path_norm(path0);
    add_dep(path);
    size_t errors = d.errors.size();
    auto lines = lines_of(path);
    if (d.errors.size() != errors) return;   // unreadable, or a preprocessor error: nothing to check
    modules_.push_back(std::make_unique<Module>());
    Module& mod = *modules_.back();
    mod.name = path_stem(path);
    mod.where = Where{path, 1, {}, false};
    cur_sec_ = nullptr;
    first_ = true;
    run(mod, lines, path, {});
    if (!mod.ended) d.warn(Where{path, int(lines.size()), {}, false}, 506, "no `end` directive");
    OMap<std::string> seen;
    for (auto& kv : mod.syms.items) {
        const std::string& n = kv.first;
        if (n.size() > 32) {
            std::string key = n.substr(0, 32);
            seen.setdefault(key, n);
            std::string other = *seen.get(key);
            if (other != n)
                d.warn(kv.second.where, 0, py_repr(n) + " and " + py_repr(other) +
                                               " agree in their first 32 characters (asm900 V2.2d keeps only 32)");
        }
    }
}

void Assembler::run(Module& mod, const std::vector<SrcLine>& lines, const std::string& path, const std::vector<Loc>& chain) {
    for (auto& tl : lines) {
        std::string text = tl.first;
        Where where = tl.second;
        if (text.find('\x1a') != std::string::npos) text = replace_all(text, "\x1a", "");
        if (mod.ended) {
            if (!strip(split_comment(text)).empty()) {
                d.warn(where, 507, "text after `end` is ignored");
                return;
            }
            continue;
        }
        if (!chain.empty()) where = Where{where.file, where.line, chain, false};
        try {
            statement(mod, text, where, path);
        } catch (AsmError& e) {
            d.error(where, e.code, e.what());
        } catch (ExprError& e) {
            d.error(where, 200, e.what());
        } catch (LineError& e) {
            d.error(where, 200, e.what());
        }
    }
}

void Assembler::statement(Module& mod, const std::string& text, const Where& where, const std::string& path) {
    (void)path;
    std::string code = split_comment(text);
    if (strip(code).empty()) return;
    seq_++;
    Stmt st = parse_statement(code);
    if (st.has_op && st.op[0] == '$') {
        if (st.has_label && !st.label.empty()) throw AsmError(200, "a control instruction cannot have a label");
        control(mod, upper(st.op.substr(1)), st.field, where);
        return;
    }
    if (scan_) {   // asm -M without -o: only the includes matter
        if (st.has_op && upper(st.op) == "END") mod.ended = true;
        return;
    }
    if (first_) {
        first_ = false;
        if (!mod.maximum) {
            d.error(where, 218, "`$maximum` must be the first statement (TR p.25)");
            mod.maximum = true;
        }
    }
    std::string u = st.has_op ? upper(st.op) : "";
    bool label = st.has_label && !st.label.empty();
    if (st.has_op && (u == "EQU" || u == "SECTION")) {
        if (!label) throw AsmError(200, "`" + st.op + "` needs a name");
        if (u == "EQU") {
            Sym s;
            s.kind = Sym::Equ;
            s.where = where;
            s.expr = parse_expr(st.field);
            define(mod, st.label, s);
        } else {
            section(mod, st.label, st.field, where);
        }
        return;
    }
    if (label) {
        if (!ident(st.label) || py_digit(st.label[0])) throw AsmError(208, "illegal label " + py_repr(st.label));
        Section& sec = need_section();
        auto it = std::make_unique<Item>();
        it->kind = Item::Label;
        it->where = where;
        Item* ip = it.get();
        sec.items.push_back(std::move(it));
        Sym s;
        s.kind = Sym::Label;
        s.where = where;
        s.sec = &sec;
        s.item = ip;
        define(mod, st.label, s);
    }
    if (!st.has_op) return;
    if (data_w(u) || fill_w(u) || space_w(u)) {
        data(u, st.field, where);
    } else if (u == "MODULE") {
        if (mod.name != path_stem(where.file) && mod.named) {
            d.warn(where, 509, "second `module` directive ignored");
        } else {
            mod.name = strip(st.field);
            mod.named = true;
        }
    } else if (u == "END") {
        mod.ended = true;
    } else if (u == "ORG") {
        Section& sec = need_section();
        auto it = std::make_unique<Item>();
        it->kind = Item::Org;
        it->where = where;
        it->ex = parse_expr(st.field);
        sec.items.push_back(std::move(it));
    } else if (u == "ALIGN") {
        ExprP e = parse_expr(st.field);
        int64_t a = e->is_const() ? e->v : abs_now(mod, e);
        Section& sec = need_section();
        if (a <= 0 || (a & (a - 1))) d.warn(where, 508, "align " + dec(a) + " is not a power of 2");
        auto it = std::make_unique<Item>();
        it->kind = Item::Align;
        it->where = where;
        it->align = a;
        sec.items.push_back(std::move(it));
        sec.align = std::max(sec.align, a);
    } else if (u == "PUBLIC") {
        for (auto& n : names_of(st.field)) mod.pub.setdefault(n, where);
    } else if (u == "EXTERN") {
        std::string field = st.field;
        auto parts = split_ws(field, 1);
        int w = 24;
        if (!parts.empty() && attr_w(upper(parts[0]))) {
            w = attr_w(upper(parts[0]));
            field = parts.size() > 1 ? parts[1] : "";
        }
        for (auto& n : names_of(field)) {
            Sym* old = mod.syms.get(n);
            if (old && old->kind != Sym::Extern)
                throw AsmError(203, "multi-defined symbol " + py_repr(n) + " (defined at " + old->where.str() + ")");
            Sym s;
            s.kind = Sym::Extern;
            s.where = where;
            s.width = w;
            mod.syms.set(n, s);
        }
    } else if (UNSUPPORTED.count(u)) {
        throw AsmError(200, "`" + st.op + "` is not supported (floating point / debug information)");
    } else {
        std::string b;
        int sf;
        if (!split_mnemonic(u, b, sf)) throw AsmError(200, "unknown mnemonic or directive " + py_repr(st.op));
        Section& sec = need_section();
        if (sec.kind != "code") d.warn(where, 504, "instruction in a " + sec.kind + " section");
        ParsedInsn p = parse_insn(st.op, st.field);
        auto it = std::make_unique<Item>();
        it->kind = Item::Ins;
        it->where = where;
        it->base = p.base;
        it->sfx = p.sfx;
        it->ops = std::move(p.ops);
        sec.items.push_back(std::move(it));
    }
}

void Assembler::control(Module& mod, const std::string& name, const std::string& field, const Where& where) {
    if (name == "MAXIMUM") {
        if (!first_) d.warn(where, 218, "`$maximum` should be the first statement");
        mod.maximum = true;
        first_ = false;
        return;
    }
    if (name == "MINIMUM") throw AsmError(218, "$minimum is not supported (the 900/H has only maximum mode)");
    if (name == "SYSTEM" || name == "NORMAL") {
        d.warn(where, 0, "$" + lower(name) + " only records a mode in asm900; ignored");
        return;
    }
    if (LISTING.count(name)) return;
    if (name == "INCLUDE") {
        std::string f = strip(field), fname;
        bool ok = false;
        if (f.size() >= 3 && f.front() == '"' && f.back() == '"' && f.find('"', 1) == f.size() - 1) {
            fname = f.substr(1, f.size() - 2);
            ok = true;
        } else if (f.size() >= 3 && f.front() == '<' && f.back() == '>' && f.find('>', 1) == f.size() - 1) {
            fname = f.substr(1, f.size() - 2);
            ok = true;
        }
        if (!ok) throw AsmError(218, "$include needs \"file\"");
        std::vector<Loc> chain = where.chain;
        chain.push_back(where.loc());
        // asm900's limits (TR p.32): 8 levels, 99 $include per translation unit. An error with
        // --strict; otherwise a warning (once per module), since unitron itself has no such limit.
        if (chain.size() > 64) throw AsmError(234, "include nesting too deep (recursive include?)");
        if (chain.size() > 8) {
            if (strict_) throw AsmError(234, "include nesting deeper than 8");
            if (!mod.warned_depth) d.warn(where, 0, "include nesting deeper than 8 (asm900's limit; --strict makes it an error)");
            mod.warned_depth = true;
        }
        if (++mod.includes > 99) {
            if (strict_) throw AsmError(234, "more than 99 $include in one translation unit");
            if (!mod.warned_count)
                d.warn(where, 0, "more than 99 $include in one translation unit (asm900's limit; --strict makes it an error)");
            mod.warned_count = true;
        }
        std::string p = find_include(fname, where.file);
        if (p.empty() && track_ && missing_ok_) {   // -MG: it will be generated
            add_dep(fname);
            missing = true;
            return;
        }
        if (p.empty()) throw AsmError(20, "can't open " + py_repr(fname));
        add_dep(p);
        std::string text = read_source(p);
        std::vector<SrcLine> lines;
        int n = 1;
        for (auto& t : splitlines(text)) lines.push_back({t, Where{p, n++, {}, false}});
        run(mod, lines, p, chain);
        return;
    }
    throw AsmError(218, "unknown control instruction $" + lower(name));
}

Section& Assembler::need_section() {
    if (!cur_sec_) throw AsmError(215, "no section definition before this statement");
    return *cur_sec_;
}

void Assembler::define(Module& mod, const std::string& name, const Sym& sym) {
    if (is_reserved(name)) throw AsmError(208, py_repr(name) + " is a reserved word");
    Sym* old = mod.syms.get(name);
    if (old) throw AsmError(203, "multi-defined symbol " + py_repr(name) + " (first defined at " + old->where.str() + ")");
    Sym s = sym;
    s.seq = seq_;
    mod.syms.set(name, s);
}

int64_t Assembler::abs_now(Module& mod, const ExprP& e) {
    VW r;
    try {
        r = evaluate(e, [&](const std::string& n) { return lookup(mod, n, true); }, []() { return VW{0, 0}; });
    } catch (Unknown& u) {
        throw AsmError(223, py_repr(u.name) + " must be defined before this point");
    }
    if (r.second) throw AsmError(222, "absolute expression expected");
    return r.first;
}

void Assembler::section(Module& mod, const std::string& name, const std::string& field, const Where& where) {
    auto toks = split_ws(field);
    std::string t0 = toks.empty() ? "" : upper(toks[0]);
    if (toks.empty() || (t0 != "CODE" && t0 != "DATA" && t0 != "ROMDATA"))
        throw AsmError(212, "section type must be code, data or romdata");
    std::string kind = lower(toks[0]);
    std::string attr;
    bool has_abs = false;
    int64_t abs_addr = 0, align = 1;
    std::vector<std::string> rt(toks.begin() + 1, toks.end());
    std::string rest = join(rt, " ");
    // re.findall(r"(abs\s*=\s*[^\s]+|align\s*=\s*[^\s]+|\w+)", rest, re.I)
    size_t i = 0;
    auto wordc = [](char c) { return py_alnum(c) || c == '_'; };
    while (i < rest.size()) {
        std::string part;
        bool matched = false;
        for (const char* kw : {"abs", "align"}) {
            size_t n = std::char_traits<char>::length(kw);
            if (lower(rest.substr(i, n)) == kw) {
                size_t j = i + n;
                while (j < rest.size() && py_space(rest[j])) j++;
                if (j < rest.size() && rest[j] == '=') {
                    j++;
                    while (j < rest.size() && py_space(rest[j])) j++;
                    size_t k = j;
                    while (k < rest.size() && !py_space(rest[k])) k++;
                    if (k > j) {
                        part = rest.substr(i, k - i);
                        i = k;
                        matched = true;
                        break;
                    }
                }
            }
        }
        if (!matched) {
            if (wordc(rest[i])) {
                size_t k = i;
                while (k < rest.size() && wordc(rest[k])) k++;
                part = rest.substr(i, k - i);
                i = k;
            } else {
                i++;
                continue;
            }
        }
        std::string p = replace_all(lower(part), " ", "");
        if (starts_with(p, "abs=")) {
            abs_addr = abs_now(mod, parse_expr(part.substr(part.find('=') + 1)));
            has_abs = true;
        } else if (starts_with(p, "align=")) {
            std::string a = split_on(part.substr(part.find('=') + 1), ",")[0];
            align = a.empty() ? 1 : abs_now(mod, parse_expr(a));
        } else if (attr_w(upper(p))) {
            attr = lower(p);
        } else {
            throw AsmError(216, "bad section attribute " + py_repr(part));
        }
    }
    if (attr == "small" && kind != "data") throw AsmError(216, "only data sections can be small (TR p.19)");
    if (!attr.empty() && has_abs) throw AsmError(216, "a section is either relocatable (small/medium/large) or abs=");
    if (!has_abs && attr.empty()) attr = "large";
    auto it = mod.secmap.find(name);
    if (it != mod.secmap.end()) {
        Section* old = it->second;
        if (old->kind != kind || (has_abs && (!old->has_abs || old->abs != abs_addr)) || (!attr.empty() && old->attr != attr))
            d.warn(where, 517, "section " + name + " already has a different attribute; continuing it");
        cur_sec_ = old;
        return;
    }
    auto sec = std::make_unique<Section>();
    sec->module = &mod;
    sec->name = name;
    sec->kind = kind;
    sec->attr = attr;
    sec->has_abs = has_abs;
    sec->abs = abs_addr;
    sec->align = align;
    sec->where = where;
    sec->base = has_abs ? abs_addr : 0;
    cur_sec_ = sec.get();
    mod.secmap[name] = sec.get();
    mod.sections.push_back(std::move(sec));
}

void Assembler::data(const std::string& u, const std::string& field, const Where& where) {
    Section& sec = need_section();
    if (sec.kind == "data" && !space_w(u) && !sec.has_init) {
        sec.has_init = true;   // reported in emit() unless an .lcf gives the section a ROM org=
        sec.init_where = where;
    }
    auto it = std::make_unique<Item>();
    it->where = where;
    it->seq = seq_;
    if (data_w(u)) {
        int w = data_w(u);
        auto vals = data_values(field, w);
        if (vals.empty()) throw AsmError(231, lower(u) + " needs values");
        it->kind = Item::Data;
        it->w = w;
        it->vals = vals;
        bool all = true;
        for (auto& v : vals) all = all && v.k != DVal::Ex;
        if (all) {
            it->data = pack(w, vals, where);
            it->kind = Item::Bin;
        }
    } else if (fill_w(u)) {
        int w = fill_w(u);
        auto parts = split_operands(field);
        if (parts.size() != 2) throw AsmError(231, lower(u) + " takes count,value");
        ExprP n = parse_expr(parts[0]);
        auto v = data_values(parts[1], w);   // parts[1] is one operand: one value
        it->kind = Item::Fill;
        it->w = w;
        it->ex = n;
        it->fillv = v[0];
    } else {
        it->kind = Item::Space;
        it->w = space_w(u);
        it->ex = parse_expr(field);
    }
    sec.items.push_back(std::move(it));
}

// ------------------------------------------------------------ symbols
VW Assembler::lookup(Module& mod, const std::string& name, bool strict) {
    Sym* s = mod.syms.get(name);
    if (!s) {
        if (strict || final_) throw Unknown(name);
        return {0, 0};
    }
    if (s->kind == Sym::Label) return {s->sec->base + s->item->off, s->sec->width()};
    if (s->kind == Sym::Equ) {
        if (s->expr->is_const()) return {s->expr->v, 0};
        if (s->busy) throw AsmError(200, py_repr(name) + " is defined in terms of itself");
        s->busy = true;
        VW r;
        try {
            FuncFn f = [&](const std::string& fn, const std::string& sn) { return func(mod, fn, sn); };
            r = evaluate(s->expr, [&](const std::string& n) { return lookup(mod, n, strict); }, []() { return VW{0, 0}; }, &f);
        } catch (...) {
            s->busy = false;
            throw;
        }
        s->busy = false;
        return {r.first, 0};
    }
    // extern
    auto it = publics_ready_ ? publics_.find(name) : publics_.end();
    if (it == publics_.end()) {
        auto ls = lcf_syms_.find(name);
        if (ls != lcf_syms_.end()) return {ls->second, s->width};
        if (final_ && link_) throw Unknown(name);
        return {0, s->width};
    }
    int w = s->width;
    VW r = lookup(*it->second.first, name, strict);
    return {r.first, w};
}

VW Assembler::func(Module& mod, const std::string& f, const std::string& name) {
    auto it = mod.secmap.find(name);
    if (it == mod.secmap.end()) throw AsmError(200, "section " + py_repr(name) + " does not exist (E232)");
    if (f == "sizeof") return {it->second->size, 0};
    return {it->second->base, it->second->width()};
}

// ------------------------------------------------------------ layout
bool Assembler::layout() {
    bool changed = false;
    for (auto& mp : modules_) {
        Module& mod = *mp;
        for (auto& sp : mod.sections) {
            Section& sec = *sp;
            int64_t lc = 0;
            for (auto& ip : sec.items) {
                Item& it = *ip;
                if (it.kind == Item::Org) {
                    int64_t v = value(mod, sec, it, it.ex).first;
                    int64_t target = sec.has_abs ? v - sec.base : v;
                    if (target < lc) {
                        if (final_)
                            d.error(it.where, 229, "org 0x" + hexs(v) + " is below the location counter 0x" + hexs(sec.base + lc));
                        target = lc;
                    }
                    lc = target;
                    it.off = lc;
                    continue;
                }
                if (it.kind == Item::Align) {
                    int64_t a = it.align > 0 ? it.align : 1;
                    int64_t pos = sec.has_abs ? sec.base + lc : lc;
                    lc += pymod(-pos, a);
                    it.off = lc;
                    continue;
                }
                if (it.off != lc) {
                    it.off = lc;
                    changed = true;
                }
                int64_t n = 0;
                if (it.kind == Item::Ins) n = ins_size(mod, sec, it);
                else if (it.kind == Item::Bin) n = int64_t(it.data.size());
                else if (it.kind == Item::Data) n = int64_t(it.w) * int64_t(it.vals.size());
                else if (it.kind == Item::Fill && it.fillv.k == DVal::Str)
                    n = int64_t(it.fillv.s.size()) * count(mod, sec, it);   // dfb n,"str": n copies
                else if (it.kind == Item::Fill || it.kind == Item::Space) n = it.w * count(mod, sec, it);
                if (n != it.size) {
                    it.size = n;
                    changed = true;
                }
                lc += n;
            }
            if (sec.size != lc) {
                sec.size = lc;
                changed = true;
            }
        }
    }
    return changed;
}

int64_t Assembler::count(Module& mod, Section& sec, Item& it) {
    (void)sec;
    VW r;
    try {
        // a symbol defined on a later line is a forward reference, error 223: TR p.21-22 (df* count,
        // ds*), and asm900 checks it for the ds* codes 0x149-0x14c (A 0x406bad-0x406bfb) and the
        // df* codes 0x143/0x145-0x147 (A 0x409420-0x40946e)
        r = evaluate(
            it.ex,
            [&](const std::string& n) {
                Sym* s = mod.syms.get(n);
                if (s && s->kind != Sym::Extern && s->seq > it.seq) throw Unknown(n);
                return lookup(mod, n, true);
            },
            []() { return VW{0, 0}; });
    } catch (Unknown& u) {
        if (final_) d.error(it.where, 223, "count uses " + py_repr(u.name) + ", not defined before this line");
        return 0;
    } catch (std::runtime_error& e) {
        if (final_) d.error(it.where, 206, e.what());
        return 0;
    }
    int64_t v = r.first;
    if (v <= 0 && it.kind == Item::Space) {
        if (final_) d.warn(it.where, 501, "ds count of 0 or less; 1 assumed");
        v = 1;
    }
    return std::max<int64_t>(v, 0);
}

VW Assembler::value(Module& mod, Section& sec, Item& it, const ExprP& e) {
    if (e->is_const()) return {e->v, 0};
    try {
        return ev_strict(mod, sec, it, e);
    } catch (Unknown& u) {
        if (final_) d.error(it.where, 221, "undefined symbol " + py_repr(u.name));
        return {0, 0};
    } catch (ExprError& e2) {
        if (final_) d.error(it.where, 206, e2.what());
        return {0, 0};
    } catch (AsmError& e2) {
        if (final_) d.error(it.where, 206, e2.what());
        return {0, 0};
    }
}

VW Assembler::ev_strict(Module& mod, Section& sec, Item& it, const ExprP& e) {
    if (e->is_const()) return {e->v, 0};
    FuncFn f = [&](const std::string& fn, const std::string& sn) { return func(mod, fn, sn); };
    return evaluate(e, [&](const std::string& n) { return lookup(mod, n); },
                    [&]() { return VW{sec.base + it.off, sec.width()}; }, &f);
}

int64_t Assembler::ins_size(Module& mod, Section& sec, Item& it) {
    try {
        return int64_t(encode_item(mod, sec, it, false).size());
    } catch (EncodeError&) {
    } catch (ExprError&) {
    } catch (AsmError&) {
    } catch (Unknown&) {
    }
    return it.size ? it.size : 1;
}

Bytes Assembler::encode_item(Module& mod, Section& sec, Item& it, bool final) {
    EvalFn ev = [&](const ExprP& e) { return ev_strict(mod, sec, it, e); };
    JumpOpts jo;
    jo.optimize = optimize_;
    // label, or label +/- constant, in this same section: the distance does not depend on placement
    jo.local = [&](const ExprP& e) {
        const Expr* x = e.get();
        while (x->kind == Expr::Bin && (x->op == "+" || x->op == "-") && x->b->is_const()) x = x->a.get();
        if (x->kind != Expr::Sym) return false;
        Sym* s = mod.syms.get(x->name);
        return s && s->kind == Sym::Label && s->sec == &sec;
    };
    return encode(it.base, it.sfx, it.ops, sec.base + it.off, ev, final, &jo);
}

// ------------------------------------------------------------ linking
void Assembler::resolve_publics() {
    publics_.clear();
    publics_ready_ = true;
    for (auto& mp : modules_) {
        Module& mod = *mp;
        for (auto& kv : mod.pub.items) {
            const std::string& n = kv.first;
            const Where& where = kv.second;
            Sym* s = mod.syms.get(n);
            if (!s || s->kind == Sym::Extern) {
                d.error(where, 210, "public " + py_repr(n) + " is not defined in module " + mod.name);
                continue;
            }
            auto it = publics_.find(n);
            if (it != publics_.end()) {
                d.error(where, 203, "public " + py_repr(n) + " also defined at " + it->second.second.str());
                continue;
            }
            publics_[n] = {&mod, where};
        }
    }
    if (!link_) return;
    for (auto& mp : modules_) {
        for (auto& kv : mp->syms.items) {
            const Sym& s = kv.second;
            if (s.kind != Sym::Extern) continue;
            auto it = publics_.find(kv.first);
            if (it == publics_.end()) continue;
            Sym* ds = it->second.first->syms.get(kv.first);
            if (ds->kind == Sym::Label && !ds->sec->has_abs && ds->sec->width() != s.width)
                d.error(s.where, 216, "extern " + py_repr(kv.first) + " is declared " + attr_name(s.width) +
                                          " but its section " + ds->sec->name + " is " + ds->sec->attr);
        }
    }
}

static bool first_fit(std::vector<std::pair<int64_t, int64_t>> used, int64_t base, int64_t size, int64_t align,
                      int64_t limit, int64_t& out) {
    std::sort(used.begin(), used.end());
    int64_t pos = base + pymod(-base, align);
    for (auto& ab : used) {
        if (ab.second <= pos) continue;
        if (ab.first >= pos + size) break;
        pos = ab.second + pymod(-ab.second, align);
    }
    if (pos + size > limit) return false;
    out = pos;
    return true;
}

void Assembler::place(const std::map<std::string, int64_t>& placements, int64_t data_base, int64_t code_base) {
    std::vector<std::pair<int64_t, int64_t>> used;
    for (auto& mp : modules_)
        for (auto& sp : mp->sections)
            if (sp->has_abs && sp->size) used.push_back({sp->base, sp->base + sp->size});
    std::map<std::string, std::vector<Section*>> groups;
    std::vector<std::string> order;
    for (auto& mp : modules_)
        for (auto& sp : mp->sections)
            if (!sp->has_abs) {
                if (!groups.count(sp->name)) order.push_back(sp->name);
                groups[sp->name].push_back(sp.get());
            }
    for (auto& name : order) {
        auto& secs = groups[name];
        int64_t total_align = 1;
        bool firsta = true;
        for (auto* s : secs) {
            total_align = firsta ? s->align : std::max(total_align, s->align);
            firsta = false;
        }
        int64_t size = 0;
        for (auto* s : secs) {
            size += pymod(-size, s->align);
            size += s->size;
        }
        int64_t limit = attr_limit(secs[0]->width());
        int64_t start = 0;
        auto pl = placements.find(name);
        if (pl != placements.end()) {
            start = pl->second;
        } else {
            bool found = false;
            int64_t bases[2] = {secs[0]->kind == "data" ? data_base : code_base, secs[0]->width() > 8 ? 0x100 : 0};
            for (int64_t b : bases)
                if (first_fit(used, b, size, total_align, limit, start)) {
                    found = true;
                    break;
                }
            if (!found) {
                d.error(secs[0]->where, 216, "no room for section " + name + " (" + dec(size) + " bytes, " + secs[0]->attr + ")");
                continue;
            }
        }
        if (start + size > limit)
            d.error(secs[0]->where, 216, "section " + name + " at 0x" + hexs(start) + " does not fit its " + secs[0]->attr +
                                             " displacement");
        int64_t pos = start;
        for (auto* s : secs) {
            pos += pymod(-pos, s->align);
            s->base = pos;
            pos += s->size;
        }
        if (size) used.push_back({start, start + size});
    }
}

void Assembler::place_lcf() {
    Lcf lcf;
    try {
        lcf = Lcf::parse_file(lcf_path_);
    } catch (std::exception& e) {
        d.error(Where::None(), 0, e.what());
        lcf_failed_ = true;
        return;
    }
    std::vector<std::pair<int64_t, int64_t>> used;
    std::vector<Section*> rel;
    std::vector<LcfInput> inputs;
    for (auto& mp : modules_)
        for (auto& sp : mp->sections) {
            if (sp->has_abs) {
                if (sp->size) used.push_back({sp->base, sp->base + sp->size});
                continue;
            }
            inputs.push_back({rel.size(), sp->name, mp->where.file, sp->kind, sp->size, sp->align, sp->width()});
            rel.push_back(sp.get());
        }
    LcfResult r = lcf.place(inputs, used);
    for (auto& w : r.warnings) d.warn(Where::None(), 0, w);
    for (auto& e : r.errors) d.error(Where::None(), 0, e);
    for (auto& kv : r.placed) {
        Section* s = rel[kv.first];
        s->base = kv.second.addr;
        s->load_delta = kv.second.load - kv.second.addr;
    }
    for (auto& o : r.outputs)
        if (o.attr == "NOLOAD" || o.attr == "DSECT")
            for (size_t id : o.inputs) rel[id]->noload = true;
    // TR 8.3.5, Table 8-4: OVERLAY and COPY sections output their bytes and may overlap others
    for (auto& o : r.outputs)
        if (o.attr == "OVERLAY" || o.attr == "COPY")
            for (size_t id : o.inputs) rel[id]->overlay = true;
    for (auto& s : r.symbols) lcf_syms_[s.first] = s.second;
}

// ------------------------------------------------------------ driver
std::vector<Chunk> Assembler::assemble(bool link, const std::map<std::string, int64_t>& placements, int64_t data_base,
                                       int64_t code_base, int max_passes) {
    link_ = link;
    final_ = false;
    resolve_publics();
    bool settled = false;
    for (int i = 0; i < max_passes; i++)
        if (!layout()) {
            settled = true;
            break;
        }
    if (!settled) d.error(Where::None(), 0, "instruction sizes did not settle");
    if (link && !lcf_path_.empty()) {
        place_lcf();
    } else if (link) {
        place(placements, data_base, code_base);
    } else {
        for (auto& mp : modules_)
            for (auto& sp : mp->sections)
                if (!sp->has_abs) sp->base = 0;
    }
    if (lcf_failed_) return {};
    final_ = true;
    layout();
    return emit();
}

std::vector<Chunk> Assembler::emit() {
    std::vector<Chunk> chunks;
    struct Span {
        int64_t a, b;
        Where where;
        Section* sec;
        size_t order;
    };
    std::vector<Span> spans;
    // data sections are RAM: their initial values have no place in the image, except where an
    // .lcf stores them in ROM (org= differs from addr=, TR 8.3.4) for the startup code to copy
    for (auto& mp : modules_)
        for (auto& sp : mp->sections)
            if (sp->has_init && !sp->load_delta)
                d.warn(sp->init_where, 0, "initialized data in data section " + sp->name +
                                              " is not stored in the image; use ds* to reserve RAM, or a romdata section "
                                              "for constants (or an .lcf org= in ROM)");
    for (auto& mp : modules_)
        for (auto& sp : mp->sections)
            for (auto& ip : sp->items) {
                Item& it = *ip;
                if (!it.size) continue;
                int64_t addr = sp->base + it.off + sp->load_delta;   // image address
                if (sp->noload) addr = sp->base + it.off;
                if ((link_ || sp->has_abs) && !sp->noload && !sp->overlay) spans.push_back({addr, addr + it.size, it.where, sp.get(), spans.size()});
                Bytes b;
                if (!item_bytes(*mp, *sp, it, b)) continue;
                if (sp->noload) continue;
                if (int64_t(b.size()) != it.size) {
                    d.error(it.where, 0, "size changed from " + dec(it.size) + " to " + dec(int64_t(b.size())) +
                                             " bytes between passes (internal)");
                    continue;
                }
                chunks.push_back({addr, b, sp.get()});
            }
    std::stable_sort(spans.begin(), spans.end(), [](const Span& x, const Span& y) {
        if (x.a != y.a) return x.a < y.a;
        return x.b < y.b;
    });
    const Span* last = nullptr;
    for (auto& s : spans) {
        if (last && s.a < last->b)
            d.error(s.where, 217, "sections overlap at 0x" + hexs(s.a) + ": " + s.where.str() + " (" + s.sec->name +
                                      ") runs into bytes from " + last->where.str() + " (" + last->sec->name + ", 0x" +
                                      hexs(last->a) + "-0x" + hexs(last->b - 1) + ")");
        if (!last || s.b > last->b) last = &s;
    }
    return chunks;
}

bool Assembler::item_bytes(Module& mod, Section& sec, Item& it, Bytes& out) {
    switch (it.kind) {
        case Item::Ins:
            try {
                out = encode_item(mod, sec, it, true);
                return true;
            } catch (EncodeError& e) {
                d.error(it.where, 230, e.what());
            } catch (Unknown& u) {
                d.error(it.where, 221, "undefined symbol " + py_repr(u.name));
            } catch (ExprError& e) {
                d.error(it.where, 206, e.what());
            } catch (AsmError& e) {
                d.error(it.where, 206, e.what());
            }
            return false;
        case Item::Bin:
            out = it.data;
            return true;
        case Item::Data: {
            std::vector<DVal> vals;
            for (auto& v : it.vals) {
                if (v.k != DVal::Ex) {
                    vals.push_back(v);
                } else {
                    DVal x;
                    x.v = value(mod, sec, it, v.e).first;
                    vals.push_back(x);
                }
            }
            out = pack(it.w, vals, it.where);
            return true;
        }
        case Item::Fill: {
            DVal v = it.fillv;
            if (v.k == DVal::Ex) {
                DVal x;
                x.v = value(mod, sec, it, v.e).first;
                v = x;
            }
            // dfb n,"str" writes the string n times: asm900's dfb handler (A 0x41bbf0) sizes it as
            // length * n and repeats the string's bytes (TR p.21 allows a string in dfb)
            Bytes one = pack(it.w, {v}, it.where);
            out.clear();
            if (!one.empty()) {
                int64_t times = it.size / int64_t(one.size());
                for (int64_t i = 0; i < times; i++) out.insert(out.end(), one.begin(), one.end());
            }
            return true;
        }
        default:
            return false;   // space: reserved, no bytes
    }
}

Bytes Assembler::pack(int w, const std::vector<DVal>& vals, const Where& where) {
    Bytes out;
    int64_t lo = -(int64_t(1) << (8 * w - 1)), hi = (int64_t(1) << (8 * w)) - 1;
    if (w == 4) {
        lo = -(int64_t(1) << 31);
        hi = (int64_t(1) << 32) - 1;
    }
    for (auto& v : vals) {
        if (v.k == DVal::Str) {
            out.insert(out.end(), v.s.begin(), v.s.end());
            continue;
        }
        if (v.big || !(lo <= v.v && v.v <= hi))
            d.error(where, 501, "value " + (v.big ? v.decimal : dec(v.v)) + " does not fit in " + dec(w) + " byte(s)");
        uint64_t u = uint64_t(v.v);
        for (int i = 0; i < w; i++) out.push_back(uint8_t(u >> (8 * i)));
    }
    return out;
}

std::vector<std::pair<std::string, int64_t>> Assembler::symbols() {
    OMap<int64_t> out;
    for (auto& mp : modules_)
        for (auto& kv : mp->syms.items) {
            if (kv.second.kind == Sym::Extern) continue;
            try {
                out.set(kv.first, lookup(*mp, kv.first).first);
            } catch (...) {
            }
        }
    return out.items;
}

Image build_image(const std::vector<Chunk>& chunks, const int64_t* origin, const int64_t* size, int fill, Diags* d) {
    std::vector<const Chunk*> rom;
    for (auto& c : chunks)
        if (c.sec->kind != "data" || c.sec->load_delta) rom.push_back(&c);
    // an OVERLAY/COPY section's bytes are written last: where it overlaps, its bytes are in the image
    std::stable_partition(rom.begin(), rom.end(), [](const Chunk* c) { return !c->sec->overlay; });
    Image im;
    if (origin) {
        im.origin = *origin;
    } else {
        bool any = false;
        for (auto* c : rom) {
            im.origin = any ? std::min(im.origin, c->addr) : c->addr;
            any = true;
        }
        if (!any) im.origin = 0;
        // a program in the cartridge's first ROM chip: the image is the cartridge, from 0x200000
        // (SNK SysPro, main CPU memory map), whatever the lowest byte written is
        if (any && CART_BASE <= im.origin && im.origin < CART_BASE + 0x200000) im.origin = CART_BASE;
    }
    if (size) {
        im.size = *size;
    } else {
        int64_t mx = im.origin;
        bool any = false;
        for (auto* c : rom) {
            int64_t e = c->addr + int64_t(c->b.size());
            mx = any ? std::max(mx, e) : e;
            any = true;
        }
        im.size = mx - im.origin;
    }
    im.data.assign(size_t(std::max<int64_t>(im.size, 0)), uint8_t(fill));
    int64_t outside = 0;
    int64_t isz = std::max<int64_t>(im.size, 0);
    for (auto* c : rom) {
        int64_t a = c->addr, n = int64_t(c->b.size());
        int64_t lo = std::max(a, im.origin), hi = std::min(a + n, im.origin + isz);
        if (lo < hi) std::copy(c->b.begin() + (lo - a), c->b.begin() + (hi - a), im.data.begin() + (lo - im.origin));
        outside += n - std::max<int64_t>(0, hi - lo);
    }
    if (outside && d)
        d->warn(Where::None(), 0, dec(outside) + " code/romdata bytes lie outside the image 0x" + hexs(im.origin) + "+0x" +
                                      hexs(im.size));
    return im;
}

}  // namespace unitron
