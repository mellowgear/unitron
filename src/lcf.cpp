// Link command files: parser and section placement. "TR x.y" = TLCS-900 Assembler Reference section.
#include "lcf.hpp"

#include <algorithm>
#include <stdexcept>

#include "util.hpp"

namespace unitron {

namespace {

const uint64_t M32 = 0xFFFFFFFFull;
const int64_t SPACE_END = int64_t(1) << 32;  // TR 8.1.3: values are unsigned 32-bit

enum Attr { A_R = 1, A_W = 2, A_X = 4, A_I = 8 };

struct Expr;
using ExprP = std::shared_ptr<const Expr>;
struct Expr {
    enum Op { Num, Dot, Sym, Fn, Un, Bin } op = Num;
    uint32_t v = 0;
    std::string s;  // symbol / section name, or operator text
    std::string fn; // function name (lowercase)
    ExprP a, b;
};

struct Mem {
    std::string name;
    int attrs = A_R | A_W | A_X | A_I;  // TR 8.2.2: omitted = all of RWXI
    int64_t org = 0, len = 0;
    int line = 0;
    std::string pre_kind;  // predefined memory (TR 8.2.3): code|data|romdata, else ""
    int pre_width = 0;     // 8 (.s) / 16 (.m) / 24 (.l)
};

struct InSpec {
    std::string file;                // "*" = any module
    std::vector<std::string> names;  // empty = every section of the file (TR 8.3.6)
};

struct OutSec {
    std::string name;
    int line = 0;
    ExprP org, addr, align, len;
    std::string attr;
    bool has_pad = false;
    ExprP pad;
    std::string mem;
    int mem_line = 0;
    std::vector<InSpec> specs;
};

struct Item {
    bool is_out = false;
    size_t out = 0;     // index into outs
    std::string sym;    // "." = location counter
    bool plus = false;  // +=
    ExprP e;
    int line = 0;
};

[[noreturn]] void fail(const std::string& path, int line, const std::string& msg) {
    throw std::runtime_error(path + ":" + std::to_string(line) + ": " + msg);
}

bool is_pow2(uint64_t v) { return v && !(v & (v - 1)); }

int64_t align_up(int64_t x, int64_t a) {
    if (a <= 1) return x;
    int64_t r = x % a;
    return r ? x + (a - r) : x;
}

std::string attr_text(int a) {
    std::string s;
    if (a & A_R) s += 'R';
    if (a & A_W) s += 'W';
    if (a & A_X) s += 'X';
    if (a & A_I) s += 'I';
    return s.empty() ? "none" : s;
}

// TR 8.2.2 Table 8-2: memory attributes a section type needs.
int required_attrs(const std::string& kind) {
    if (kind == "code") return A_R | A_X;
    if (kind == "data") return A_R | A_W;
    if (kind.empty()) return 0;
    return A_R;  // romdata
}

int64_t width_limit(int width) {
    if (width == 8) return 0x100;
    if (width == 16) return 0x10000;
    if (width == 24) return 0x1000000;
    return SPACE_END;
}

std::string width_name(int width) {
    return width == 8 ? "small" : width == 16 ? "medium" : width == 24 ? "large" : std::to_string(width) + "-bit";
}

bool reserved(const std::string& w) {
    static const char* words[] = {"addr", "align", "copy", "dsect", "len", "length", "memory", "next",
                                  "noload", "org", "origin", "overlay", "sections", "sizeof"};  // TR 8.1.1
    std::string l = lower(w);
    for (const char* k : words)
        if (l == k) return true;
    return false;
}

// ---------------------------------------------------------------- lexer

struct Tok {
    enum K { End, Ident, Num, Punct } k = End;
    std::string s;
    uint32_t v = 0;
    int line = 1;
    size_t pos = 0;
};

class Lexer {
public:
    Lexer(const std::string& text, const std::string& path) : t_(text), path_(path) {}
    const std::string& path() const { return path_; }

    const Tok& peek() {
        if (!has_) {
            la_ = scan();
            has_ = true;
        }
        return la_;
    }
    Tok next() {
        peek();
        has_ = false;
        return la_;
    }
    bool is(const char* punct) { return peek().k == Tok::Punct && peek().s == punct; }
    bool accept(const char* punct) {
        if (!is(punct)) return false;
        next();
        return true;
    }
    void expect(const char* punct, const std::string& what) {
        if (!accept(punct)) fail(path_, peek().line, std::string("expected '") + punct + "' " + what + ", found " + desc(peek()));
    }
    static std::string desc(const Tok& k) {
        if (k.k == Tok::End) return "end of file";
        return "'" + k.s + "'";
    }

    // Raw scanning for the input section specification field (file names may hold any character).
    char raw_peek() {
        unpeek();
        skip();
        return i_ < t_.size() ? t_[i_] : 0;
    }
    void raw_take() { i_++; }
    int raw_line() const { return line_; }
    std::string raw_word() {
        unpeek();
        skip();
        size_t s = i_;
        while (i_ < t_.size()) {
            char c = t_[i_];
            if (py_space((unsigned char)c) || c == '(' || c == ')' || c == '{' || c == '}' || c == ',') break;
            if (c == '/' && i_ + 1 < t_.size() && (t_[i_ + 1] == '*' || t_[i_ + 1] == '/')) break;
            i_++;
        }
        return t_.substr(s, i_ - s);
    }

private:
    void unpeek() {
        if (has_) {
            i_ = la_.pos;
            line_ = la_.line;
            has_ = false;
        }
    }
    // TR 8.1.8: /* */ comments (not nested); // line comments are an extension.
    void skip() {
        while (i_ < t_.size()) {
            char c = t_[i_];
            if (c == '\n') {
                line_++;
                i_++;
            } else if (py_space((unsigned char)c)) {
                i_++;
            } else if (c == '/' && i_ + 1 < t_.size() && t_[i_ + 1] == '*') {
                int start = line_;
                size_t e = t_.find("*/", i_ + 2);
                if (e == std::string::npos) fail(path_, start, "unterminated comment");
                for (size_t k = i_; k < e; k++)
                    if (t_[k] == '\n') line_++;
                i_ = e + 2;
            } else if (c == '/' && i_ + 1 < t_.size() && t_[i_ + 1] == '/') {
                while (i_ < t_.size() && t_[i_] != '\n') i_++;
            } else {
                break;
            }
        }
    }
    static bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.'; }
    static bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }

    Tok scan() {
        skip();
        Tok k;
        k.line = line_;
        k.pos = i_;
        if (i_ >= t_.size()) return k;
        char c = t_[i_];
        if (ident_start(c)) {  // TR 8.1.2
            size_t s = i_;
            while (i_ < t_.size() && ident_char(t_[i_])) i_++;
            k.k = Tok::Ident;
            k.s = t_.substr(s, i_ - s);
            return k;
        }
        if (c >= '0' && c <= '9') {  // TR 8.1.3
            size_t s = i_;
            while (i_ < t_.size() && ident_char(t_[i_]) && t_[i_] != '.') i_++;
            k.k = Tok::Num;
            k.s = t_.substr(s, i_ - s);
            k.v = number(k.s, k.line);
            return k;
        }
        static const char* two[] = {"+=", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||"};
        for (const char* p : two)
            if (t_.compare(i_, 2, p) == 0) {
                k.k = Tok::Punct;
                k.s = p;
                i_ += 2;
                return k;
            }
        if (std::string("{}():,;=+-*/!~<>&|").find(c) != std::string::npos) {
            k.k = Tok::Punct;
            k.s = std::string(1, c);
            i_++;
            return k;
        }
        fail(path_, line_, "unexpected character '" + std::string(1, c) + "'");
    }

    // TR 8.1.3: 0y binary, 0x hexadecimal, leading 0 octal, else decimal; unsigned 32-bit,
    // the part exceeding 32 bits is ignored.
    uint32_t number(const std::string& w0, int line) {
        std::string w = lower(w0);
        unsigned base = 10;
        std::string digits = w;
        if (w.size() >= 2 && w[0] == '0' && (w[1] == 'x' || w[1] == 'y')) {
            base = w[1] == 'x' ? 16 : 2;
            digits = w.substr(2);
        } else if (w.size() >= 2 && w[0] == '0') {
            base = 8;
            digits = w.substr(1);
        }
        if (digits.empty()) fail(path_, line, "invalid number '" + w0 + "'");
        uint64_t v = 0;
        for (char c : digits) {
            unsigned d = c >= '0' && c <= '9' ? unsigned(c - '0') : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) : 99;
            if (d >= base) fail(path_, line, "invalid number '" + w0 + "'");
            v = (v * base + d) & M32;
        }
        return uint32_t(v);
    }

    const std::string& t_;
    std::string path_;
    size_t i_ = 0;
    int line_ = 1;
    bool has_ = false;
    Tok la_;
};

}  // namespace

struct LcfData {
    std::string path;
    std::vector<Mem> mems;
    std::vector<OutSec> outs;
    std::vector<Item> items;
    bool has_sections = false;
    size_t auto_pos = 0;  // automatic output sections are allocated before items[auto_pos]
};

namespace {

// ---------------------------------------------------------------- parser

class Parser {
public:
    Parser(const std::string& text, const std::string& path) : lx_(text, path) { d_.path = path; }

    LcfData run() {
        while (lx_.peek().k != Tok::End) {
            Tok k = lx_.peek();
            if (lx_.accept(";")) continue;
            if (k.k != Tok::Ident) fail(lx_.path(), k.line, "syntax error at " + Lexer::desc(k));
            lx_.next();
            std::string kw = lower(k.s);
            if (kw == "memory" && lx_.is("{")) {
                lx_.next();
                memory_block();
            } else if (kw == "sections" && lx_.is("{")) {
                lx_.next();
                sections_block();
            } else if (lx_.is("=") || lx_.is("+=")) {
                if (k.s == ".")
                    fail(lx_.path(), k.line, "the location counter '.' can only be set in the section definition part (TR 8.1.4)");
                assign(k);
            } else {
                fail(lx_.path(), k.line, "syntax error at '" + k.s + "' (expected memory, sections or an assign statement)");
            }
        }
        finish();
        return std::move(d_);
    }

private:
    // TR 8.2.2: NAME [(ATTR)] : org=EXPR [,] len=EXPR
    void memory_block() {
        while (!lx_.accept("}")) {
            Tok k = lx_.next();
            if (k.k == Tok::Punct && (k.s == "," || k.s == ";")) continue;
            if (k.k != Tok::Ident) fail(lx_.path(), k.line, "expected a memory name, found " + Lexer::desc(k));
            Mem m;
            m.name = k.s;
            m.line = k.line;
            std::string l = lower(k.s);
            static const struct { const char* n; const char* kind; int w; } pre[] = {
                {"code.m", "code", 16},       {"code.l", "code", 24},       {"data.s", "data", 8},
                {"data.m", "data", 16},       {"data.l", "data", 24},       {"romdata.m", "romdata", 16},
                {"romdata.l", "romdata", 24}};  // TR 8.2.3 Table 8-3
            for (auto& p : pre)
                if (l == p.n) {
                    m.pre_kind = p.kind;
                    m.pre_width = p.w;
                    m.name = p.n;  // predefined names are case-insensitive (TR 8.1.2)
                }
            if (lx_.accept("(")) {
                std::string a = lx_.raw_word();
                int line = lx_.raw_line();
                m.attrs = 0;
                for (char c : a) {  // TR 8.2.2: R W X I, case-sensitive, each once (error 170)
                    int bit = c == 'R' ? A_R : c == 'W' ? A_W : c == 'X' ? A_X : c == 'I' ? A_I : 0;
                    if (!bit || (m.attrs & bit))
                        fail(lx_.path(), line, "can't set attributes \"" + a + "\" of memory " + m.name + " (only R, W, X, I, once each)");
                    m.attrs |= bit;
                }
                lx_.expect(")", "after the memory attributes");
            }
            lx_.expect(":", "after memory name " + m.name);
            bool has_org = false, has_len = false;
            for (int n = 0; n < 2; n++) {
                Tok f = lx_.next();
                std::string fk = f.k == Tok::Ident ? lower(f.s) : "";
                bool is_org = fk == "org" || fk == "origin", is_len = fk == "len" || fk == "length";
                if (!is_org && !is_len)
                    fail(lx_.path(), f.line, "expected org= or len= in memory " + m.name + ", found " + Lexer::desc(f));
                if ((is_org && has_org) || (is_len && has_len))
                    fail(lx_.path(), f.line, "memory " + m.name + ": " + fk + " given twice");
                lx_.expect("=", "after " + f.s);
                int64_t v = const_value(*expr(), f.line);
                if (is_org) {
                    m.org = v;
                    has_org = true;
                } else {
                    m.len = v;
                    has_len = true;
                }
                if (n == 0) lx_.accept(",");
            }
            if (m.org + m.len > SPACE_END) fail(lx_.path(), m.line, "memory " + m.name + " ends beyond 0xFFFFFFFF");
            for (auto& o : d_.mems)
                if (o.name == m.name) fail(lx_.path(), m.line, "memory " + m.name + " defined twice");
            d_.mems.push_back(m);
        }
    }

    void sections_block() {
        d_.has_sections = true;
        while (!lx_.accept("}")) {
            if (lx_.accept(";")) continue;
            Tok k = lx_.next();
            if (k.k != Tok::Ident)
                fail(lx_.path(), k.line, "expected an output section or an assign statement, found " + Lexer::desc(k));
            if (lx_.is("=") || lx_.is("+="))
                assign(k);
            else
                out_section(k);
        }
        d_.auto_pos = d_.items.size();
    }

    // TR 8.1.7: SYM = EXPR;  SYM += EXPR;
    void assign(const Tok& k) {
        if (k.s != "." && reserved(k.s)) fail(lx_.path(), k.line, "'" + k.s + "' is a reserved word (TR 8.1.1)");
        Item it;
        it.sym = k.s;
        it.line = k.line;
        it.plus = lx_.next().s == "+=";
        it.e = expr();
        lx_.expect(";", "after the assign statement (TR 8.1.7)");
        d_.items.push_back(it);
    }

    // TR 8.3.2: NAME [output spec] [(ATTR)] : { input specs } [=PAD] [> MEMORY]
    void out_section(const Tok& k) {
        OutSec o;
        o.name = k.s;
        o.line = k.line;
        for (auto& x : d_.outs)
            if (x.name == o.name) fail(lx_.path(), k.line, "multiply defined output section \"" + o.name + "\"");
        while (true) {
            Tok p = lx_.peek();
            if (lx_.accept(":")) break;
            if (lx_.accept("(")) {  // TR 8.3.5
                Tok a = lx_.next();
                std::string u = upper(a.s);
                if (a.k != Tok::Ident || (u != "DSECT" && u != "COPY" && u != "NOLOAD" && u != "OVERLAY"))
                    fail(lx_.path(), a.line, "unknown section attribute " + Lexer::desc(a) + " (DSECT, COPY, NOLOAD or OVERLAY)");
                if (!o.attr.empty()) fail(lx_.path(), a.line, "output section " + o.name + " has two attributes");
                o.attr = u;
                lx_.expect(")", "after the section attribute");
                continue;
            }
            std::string kw = p.k == Tok::Ident ? lower(p.s) : "";
            ExprP* slot = kw == "org" || kw == "origin" ? &o.org
                          : kw == "addr"                 ? &o.addr
                          : kw == "align"                ? &o.align
                          : kw == "len" || kw == "length" ? &o.len
                                                          : nullptr;
            if (!slot)
                fail(lx_.path(), p.line, "expected org=, addr=, align=, len=, (ATTRIBUTE) or ':' in output section " + o.name +
                                             ", found " + Lexer::desc(p));
            lx_.next();
            if (*slot) fail(lx_.path(), p.line, "output section " + o.name + ": " + kw + " given twice");
            if (kw == "align" && lx_.accept("(")) {  // TR 8.1.6 align(EXPR) as an alignment specification
                *slot = expr();
                lx_.expect(")", "after align(");
            } else {
                lx_.expect("=", "after " + p.s);
                *slot = expr();
            }
        }
        lx_.expect("{", "to open the input section specification of " + o.name);
        // TR 8.3.6: FILE(NAME ...) or *(NAME ...); FILE alone = all its sections
        while (true) {
            char c = lx_.raw_peek();
            if (c == '}') {
                lx_.raw_take();
                break;
            }
            if (c == 0) fail(lx_.path(), lx_.raw_line(), "unterminated input section specification of " + o.name);
            if (c == ',') {
                lx_.raw_take();
                continue;
            }
            int line = lx_.raw_line();
            InSpec sp;
            sp.file = lx_.raw_word();
            if (sp.file.empty()) fail(lx_.path(), line, std::string("unexpected '") + c + "' in input section specification");
            if (lx_.raw_peek() == '(') {
                lx_.raw_take();
                while (true) {
                    char d = lx_.raw_peek();
                    if (d == ')') {
                        lx_.raw_take();
                        break;
                    }
                    if (d == ',') {
                        lx_.raw_take();
                        continue;
                    }
                    if (d == 0) fail(lx_.path(), line, "unterminated input section list");
                    std::string n = lx_.raw_word();
                    if (n.empty()) fail(lx_.path(), lx_.raw_line(), std::string("unexpected '") + d + "' in input section list");
                    sp.names.push_back(n);
                }
                if (sp.names.empty()) fail(lx_.path(), line, "empty input section list " + sp.file + "()");
            }
            o.specs.push_back(sp);
        }
        if (lx_.accept("=")) {  // TR 8.3.7 padding (a number; a full expression would swallow "> memory")
            o.has_pad = true;
            o.pad = unary();
        }
        if (lx_.accept(">")) {  // TR 8.3.8
            Tok m = lx_.next();
            if (m.k != Tok::Ident) fail(lx_.path(), m.line, "expected a memory name after '>'");
            o.mem = m.s;
            o.mem_line = m.line;
        }
        Item it;
        it.is_out = true;
        it.out = d_.outs.size();
        it.line = o.line;
        d_.outs.push_back(o);
        d_.items.push_back(it);
    }

    void finish() {
        for (auto& o : d_.outs) {
            if (o.mem.empty()) continue;
            bool found = false;
            for (auto& m : d_.mems)
                if (m.name == o.mem || (!m.pre_kind.empty() && m.name == lower(o.mem))) {
                    o.mem = m.name;
                    found = true;
                }
            if (!found) fail(d_.path, o.mem_line, "output memory " + o.mem + " is not defined in the memory definition part");
        }
        // TR 8.2.3 / error 168: memory areas must not overlap, except predefined areas of one type
        for (size_t i = 0; i < d_.mems.size(); i++)
            for (size_t j = 0; j < i; j++) {
                const Mem &a = d_.mems[i], &b = d_.mems[j];
                if (a.org < b.org + b.len && b.org < a.org + a.len && !(a.pre_kind == b.pre_kind && !a.pre_kind.empty()))
                    fail(d_.path, a.line, "MEMORY segment overlap \"" + b.name + "\" and \"" + a.name + "\"");
            }
    }

    int64_t const_value(const Expr& e, int line) {
        switch (e.op) {
        case Expr::Num: return e.v;
        case Expr::Un: {
            uint64_t a = uint64_t(const_value(*e.a, line));
            return e.s == "-" ? int64_t((0 - a) & M32) : e.s == "~" ? int64_t(~a & M32) : int64_t(!a);
        }
        case Expr::Bin: {
            uint64_t a = uint64_t(const_value(*e.a, line)), b = uint64_t(const_value(*e.b, line));
            if (e.s == "+") return int64_t((a + b) & M32);
            if (e.s == "-") return int64_t((a - b) & M32);
            if (e.s == "*") return int64_t((a * b) & M32);
            if (e.s == "/") {
                if (!b) fail(lx_.path(), line, "division by zero");
                return int64_t(a / b);
            }
            if (e.s == "<<") return b >= 32 ? 0 : int64_t((a << b) & M32);
            if (e.s == ">>") return b >= 32 ? 0 : int64_t(a >> b);
            if (e.s == "&") return int64_t(a & b);
            if (e.s == "|") return int64_t(a | b);
            break;
        }
        default: break;
        }
        fail(lx_.path(), line, "memory addresses and sizes must be constant expressions");
    }

    // ---- expressions, TR 8.1.5 Table 8-1 (1 = tightest)
    ExprP expr() { return binary(11); }

    ExprP binary(int level) {
        static const std::vector<std::vector<std::string>> ops = {
            {}, {}, {}, {"*", "/"}, {"+", "-"}, {"<<", ">>"}, {"<", ">", "<=", ">="}, {"==", "!="}, {"&"}, {"|"}, {"&&"}, {"||"}};
        if (level == 2) return unary();
        ExprP a = binary(level - 1);
        while (lx_.peek().k == Tok::Punct &&
               std::find(ops[level].begin(), ops[level].end(), lx_.peek().s) != ops[level].end()) {
            auto e = std::make_shared<Expr>();
            e->op = Expr::Bin;
            e->s = lx_.next().s;
            e->a = a;
            e->b = binary(level - 1);
            a = e;
        }
        return a;
    }

    ExprP unary() {
        if (lx_.is("!") || lx_.is("~") || lx_.is("-")) {
            auto e = std::make_shared<Expr>();
            e->op = Expr::Un;
            e->s = lx_.next().s;
            e->a = unary();
            return e;
        }
        return primary();
    }

    ExprP primary() {
        Tok k = lx_.next();
        auto e = std::make_shared<Expr>();
        if (k.k == Tok::Num) {
            e->v = k.v;
            return e;
        }
        if (k.k == Tok::Punct && k.s == "(") {
            ExprP x = expr();
            lx_.expect(")", "to close '('");
            return x;
        }
        if (k.k != Tok::Ident) fail(lx_.path(), k.line, "expected an expression, found " + Lexer::desc(k));
        if (k.s == ".") {
            e->op = Expr::Dot;
            return e;
        }
        std::string l = lower(k.s);
        if (lx_.is("(")) {  // TR 8.1.6 functional operators
            if (l != "addr" && l != "org" && l != "sizeof" && l != "align" && l != "next")
                fail(lx_.path(), k.line, "unknown function '" + k.s + "'");
            lx_.next();
            e->op = Expr::Fn;
            e->fn = l;
            if (l == "addr" || l == "org" || l == "sizeof") {
                Tok n = lx_.next();
                if (n.k != Tok::Ident) fail(lx_.path(), n.line, l + "() needs a section name");
                e->s = n.s;
            } else {
                e->a = expr();
            }
            lx_.expect(")", "after the argument of " + l + "()");
            return e;
        }
        if (reserved(k.s)) fail(lx_.path(), k.line, "unexpected reserved word '" + k.s + "' in expression");
        e->op = Expr::Sym;
        e->s = k.s;
        return e;
    }

    Lexer lx_;
    LcfData d_;
};

// ---------------------------------------------------------------- placement

struct EvalError {
    std::string msg;
};

struct RtSec {
    std::string name;
    const OutSec* def = nullptr;  // null: automatic output section (TR 8.3.6)
    int line = 0;
    std::vector<size_t> members;  // indices into inputs
    std::string kind;
    int64_t maxalign = 1, limit = SPACE_END;
    bool placed = false, failed = false, busy = false;
    int64_t addr = 0, load = 0, size = 0;
    bool estimated = false;
    int64_t estimate = 0;
};

struct Occ {
    int64_t lo, hi;
    std::string who;
};

class Placer {
public:
    Placer(const LcfData& d, const std::vector<LcfInput>& in, const std::vector<std::pair<int64_t, int64_t>>& used)
        : d_(d), in_(in) {
        for (auto& u : used)
            if (u.second > u.first) occ_.push_back({u.first, u.second, "an absolute section"});
    }

    LcfResult run() {
        collect();
        for (size_t i = 0; i <= d_.items.size(); i++) {
            if (i == d_.auto_pos)
                for (size_t k = n_explicit_; k < secs_.size(); k++) allocate(k);
            if (i == d_.items.size()) break;
            const Item& it = d_.items[i];
            if (it.is_out)
                allocate(it.out);
            else
                assign(it);
        }
        return std::move(r_);
    }

private:
    void err(int line, const std::string& m) {
        r_.errors.push_back(d_.path + (line ? ":" + std::to_string(line) : std::string()) + ": " + m);
    }
    void warn(int line, const std::string& m) {
        r_.warnings.push_back(d_.path + (line ? ":" + std::to_string(line) : std::string()) + ": " + m);
    }

    // FILE(...) matches a module by path, by file name, or by file name without extension
    // (so "main.rel" in the lcf selects the module assembled from main.asm).
    static bool file_match(const std::string& spec, const std::string& file) {
        if (spec == "*" || spec == file) return true;
        if (path_name(spec) == path_name(file)) return true;
        return path_stem(spec) == path_stem(file);
    }

    void collect() {
        std::vector<bool> taken(in_.size(), false);
        for (auto& o : d_.outs) {
            RtSec s;
            s.name = o.name;
            s.def = &o;
            s.line = o.line;
            for (auto& sp : o.specs) {
                if (sp.names.empty()) {
                    for (size_t i = 0; i < in_.size(); i++)
                        if (!taken[i] && file_match(sp.file, in_[i].module_file)) {
                            taken[i] = true;
                            s.members.push_back(i);
                        }
                } else {
                    // all inputs of the first name (in input order), then the next name
                    for (auto& n : sp.names)
                        for (size_t i = 0; i < in_.size(); i++)
                            if (!taken[i] && in_[i].name == n && file_match(sp.file, in_[i].module_file)) {
                                taken[i] = true;
                                s.members.push_back(i);
                            }
                }
            }
            secs_.push_back(s);
        }
        n_explicit_ = secs_.size();
        // TR 8.3.6: unnamed input sections become output sections of the same name, joined in input order
        for (size_t i = 0; i < in_.size(); i++) {
            if (taken[i]) continue;
            size_t k = secs_.size();
            for (size_t j = n_explicit_; j < secs_.size(); j++)
                if (secs_[j].name == in_[i].name) k = j;
            if (k == secs_.size()) {
                RtSec s;
                s.name = in_[i].name;
                secs_.push_back(s);
            }
            secs_[k].members.push_back(i);
        }
        for (size_t k = 0; k < secs_.size(); k++) {
            RtSec& s = secs_[k];
            bool first = true;
            for (size_t i : s.members) {
                const LcfInput& x = in_[i];
                if (first) s.kind = x.kind;
                else if (x.kind != s.kind)
                    err(s.line, "can't link \"" + x.name + "\" (" + x.kind + ") into output section " + s.name + " (" + s.kind +
                                    ") with different attribute (TR 8.3.6)");
                first = false;
                s.maxalign = std::max(s.maxalign, std::max<int64_t>(1, x.align));
                s.limit = std::min(s.limit, width_limit(x.width));
            }
            by_name_[s.name] = k;
        }
        for (size_t k = n_explicit_; k < secs_.size(); k++)
            for (size_t j = 0; j < n_explicit_; j++)
                if (secs_[j].name == secs_[k].name) {
                    err(secs_[j].line, "multiply defined output section \"" + secs_[k].name +
                                           "\": input sections of that name are not selected by any output section");
                    secs_[k].failed = true;
                    by_name_[secs_[k].name] = j;
                }
    }

    // ---- expression evaluation (unsigned 32-bit, TR 8.1.3)
    uint32_t eval(const Expr& e) {
        switch (e.op) {
        case Expr::Num: return e.v;
        case Expr::Dot: return uint32_t(lc_);
        case Expr::Sym: {
            auto it = syms_.find(e.s);
            if (it == syms_.end()) throw EvalError{"undefined symbol '" + e.s + "'"};
            return uint32_t(it->second);
        }
        case Expr::Un: {
            uint64_t a = eval(*e.a);
            return uint32_t(e.s == "-" ? (0 - a) & M32 : e.s == "~" ? ~a & M32 : uint64_t(!a));
        }
        case Expr::Bin: {
            if (e.s == "&&") return eval(*e.a) && eval(*e.b);
            if (e.s == "||") return eval(*e.a) || eval(*e.b);
            uint64_t a = eval(*e.a), b = eval(*e.b);
            const std::string& o = e.s;
            if (o == "+") return uint32_t((a + b) & M32);
            if (o == "-") return uint32_t((a - b) & M32);
            if (o == "*") return uint32_t((a * b) & M32);
            if (o == "/") {
                if (!b) throw EvalError{"division by zero"};
                return uint32_t(a / b);
            }
            if (o == "<<") return b >= 32 ? 0 : uint32_t((a << b) & M32);
            if (o == ">>") return b >= 32 ? 0 : uint32_t(a >> b);
            if (o == "<") return a < b;
            if (o == ">") return a > b;
            if (o == "<=") return a <= b;
            if (o == ">=") return a >= b;
            if (o == "==") return a == b;
            if (o == "!=") return a != b;
            if (o == "&") return uint32_t(a & b);
            if (o == "|") return uint32_t(a | b);
            throw EvalError{"bad operator " + o};
        }
        case Expr::Fn: return function(e);
        }
        return 0;
    }

    uint32_t function(const Expr& e) {
        if (e.fn == "align" || e.fn == "next") {
            uint32_t a = eval(*e.a);
            if (!a) throw EvalError{e.fn + "(0)"};
            if (!is_pow2(a)) warn(0, "\"" + std::to_string(a) + "\" is not a power of 2");
            int64_t pos = align_up(lc_, a);
            if (e.fn == "next") {  // first multiple at or after '.' that no section occupies
                std::vector<Occ> o = occ_;
                std::sort(o.begin(), o.end(), [](const Occ& x, const Occ& y) { return x.lo < y.lo; });
                for (auto& x : o)
                    if (x.lo <= pos && pos < x.hi) pos = align_up(x.hi, a);
            }
            if (pos >= SPACE_END) throw EvalError{"overflow in location counter"};
            return uint32_t(pos);
        }
        auto it = by_name_.find(e.s);
        if (it == by_name_.end()) throw EvalError{e.fn + "(" + e.s + "): no output section named \"" + e.s + "\""};
        RtSec& s = secs_[it->second];
        if (e.fn == "sizeof") {
            if (s.placed) return uint32_t(s.size);
            if (s.failed) throw EvalError{"\"" + e.s + "\" was not allocated"};
            // not allocated yet: its size as if it starts aligned (checked when it is allocated)
            int64_t len = len_value(s);
            int64_t sz = layout(s, 0, nullptr, len);
            s.estimated = true;
            s.estimate = sz;
            return uint32_t(sz);
        }
        if (!s.placed) throw EvalError{"\"" + e.s + "\" not yet allocated (" + e.fn + "() needs a section defined earlier)"};
        // TR 8.1.6 wants an explicit org= for org(); when there is none the allocation address equals
        // the start address (TR 8.3.4), which is what org() returns here.
        return uint32_t(e.fn == "addr" ? s.addr : s.load);
    }

    int64_t len_value(RtSec& s) {
        if (!s.def || !s.def->len) return 0;
        if (s.busy) throw EvalError{"len of " + s.name + " depends on itself"};
        s.busy = true;
        int64_t v;
        try {
            v = eval(*s.def->len);
        } catch (...) {
            s.busy = false;
            throw;
        }
        s.busy = false;
        return v;
    }

    // Join the members at run address start, each aligned to its own alignment; returns the size
    // (at least len, TR 8.3.4 len=).
    int64_t layout(const RtSec& s, int64_t start, std::vector<int64_t>* offs, int64_t len) const {
        int64_t pos = start;
        for (size_t i : s.members) {
            pos = align_up(pos, in_[i].align);
            if (offs) offs->push_back(pos - start);
            pos += in_[i].size;
        }
        return std::max(pos - start, len);
    }

    const Mem* area_of(int64_t lo, int64_t size) const {
        for (auto& m : d_.mems)
            if (lo >= m.org && lo + size <= m.org + m.len) return &m;
        return nullptr;
    }
    const Mem* area_at(int64_t a) const {
        for (auto& m : d_.mems)
            if (a >= m.org && a < m.org + m.len) return &m;
        return nullptr;
    }

    bool fit(int64_t base, int64_t hi, int64_t size, int64_t align, int64_t& out) const {
        std::vector<Occ> o = occ_;
        std::sort(o.begin(), o.end(), [](const Occ& x, const Occ& y) { return x.lo < y.lo; });
        int64_t pos = align_up(base, align);
        for (auto& x : o) {
            if (x.hi <= pos) continue;
            if (x.lo >= pos + size) break;
            pos = align_up(x.hi, align);
        }
        if (pos + size > hi) return false;
        out = pos;
        return true;
    }

    // Automatic allocation (no org=/addr=; TR is silent on the exact rule, see docs/lcf.md). Candidate
    // areas: "> MEMORY", else the predefined memory of the section's type (TR 8.2.3), else every
    // user area with the attributes the type needs (TR 8.2.2), the one holding '.' first, then in
    // definition order. In each, the section goes to the first free aligned space at or after the
    // location counter if '.' lies in that area, else from the start of the area.
    bool find_space(const RtSec& s, int64_t size, int64_t align, int64_t& out) {
        int need = required_attrs(s.kind);
        std::string what = "section " + s.name + " (" + std::to_string(size) + " bytes";
        what += s.kind.empty() ? ")" : ", " + s.kind + ")";
        if (d_.mems.empty()) {  // TR 8.2.1: no memory part = everything is usable
            if (fit(lc_, s.limit, size, align, out)) return true;
            err(s.line, "no space for " + what + " after 0x" + hexs(lc_));
            return false;
        }
        std::vector<const Mem*> cands;
        std::string mem = s.def ? s.def->mem : "";
        if (!mem.empty()) {
            for (auto& m : d_.mems)
                if (m.name == mem) cands.push_back(&m);
            if ((cands[0]->attrs & need) != need) {
                err(s.line, "can't allocate \"" + s.name + "\" to \"" + mem + "\": " + s.kind + " needs " + attr_text(need) +
                                ", the memory is " + attr_text(cands[0]->attrs) + " (TR 8.3.8)");
                return false;
            }
        } else {
            // TR 8.2.3: predefined memory of the section's type, own displacement first
            int w = 99;
            for (size_t i : s.members) w = std::min(w, in_[i].width);
            for (auto& m : d_.mems)
                if (!s.kind.empty() && m.pre_kind == s.kind && m.pre_width == w) cands.push_back(&m);
            for (auto& m : d_.mems)
                if (!s.kind.empty() && m.pre_kind == s.kind && m.pre_width != w) cands.push_back(&m);
            if (cands.empty()) {
                for (auto& m : d_.mems)
                    if (m.pre_kind.empty() && (m.attrs & need) == need) cands.push_back(&m);
                // the area holding the location counter first: consecutive sections stay together
                std::stable_partition(cands.begin(), cands.end(),
                                      [&](const Mem* m) { return lc_ >= m->org && lc_ < m->org + m->len; });
            }
            if (cands.empty()) {
                err(s.line, "no memory area with attributes " + attr_text(need) + " for " + what + " (TR 8.2.2)");
                return false;
            }
        }
        std::string names;
        for (const Mem* m : cands) {
            int64_t lo = m->org, hi = std::min(m->org + m->len, s.limit);
            int64_t base = lc_ >= lo && lc_ < m->org + m->len ? lc_ : lo;
            if (base < hi && fit(base, hi, size, align, out)) return true;
            names += (names.empty() ? "" : ", ") + m->name;
        }
        err(s.line, "no space for " + what + " in " + names);
        return false;
    }

    void allocate(size_t k) {
        RtSec& s = secs_[k];
        if (s.failed) return;
        const OutSec* o = s.def;
        bool has_org = o && o->org, has_addr = o && o->addr, has_align = o && o->align;
        int64_t org = 0, addr = 0, alignv = 1, len = 0, pad = 0;
        try {
            if (has_org) org = eval(*o->org);
            if (has_addr) addr = eval(*o->addr);
            if (has_align) alignv = eval(*o->align);
            len = len_value(s);
            if (o && o->has_pad) pad = eval(*o->pad);
        } catch (EvalError& e) {
            err(s.line, "output section " + s.name + ": " + e.msg);
            s.failed = true;
            return;
        }
        std::string attr = o ? o->attr : "";
        if (has_align && !is_pow2(uint64_t(alignv))) {
            warn(s.line, "\"" + std::to_string(alignv) + "\" is not a power of 2 (align of " + s.name + ")");
            if (alignv < 1) alignv = 1;
        }
        if (has_org && has_align) {
            err(s.line, "output section " + s.name + ": the allocation address (org) and the alignment can't both be given (TR 8.3.4)");
            has_align = false;
        }
        if (attr == "OVERLAY" && !has_org && !has_addr) {
            err(s.line, "OVERLAY section " + s.name + " must be given an address (TR 8.3.5)");
            s.failed = true;
            return;
        }
        int64_t run, load;
        if (has_org) {  // TR 8.3.4: org = allocation (load) address; addr = start (run) address
            load = org;
            run = has_addr ? addr : org;
        } else if (has_addr) {  // only addr: the allocation address is the same
            run = load = addr;
            if (has_align && addr % alignv)
                err(s.line, "addr 0x" + hexs(addr) + " of " + s.name + " is not a multiple of its align " + std::to_string(alignv));
        } else {
            int64_t al = std::max(s.maxalign, alignv);
            if (!fit_size(s, len, al, run)) {
                s.failed = true;
                return;
            }
            load = run;
        }
        if (o && !o->mem.empty() && (has_org || has_addr))
            warn(s.line, "output memory " + o->mem + " of " + s.name + " is ignored: the section has an address (TR 8.3.8)");
        std::vector<int64_t> offs;
        int64_t size = layout(s, run, &offs, len);
        int64_t content = layout(s, run, nullptr, 0);
        if (o && o->len && content > len)
            warn(s.line, "Section \"" + s.name + "\" size " + std::to_string(content) + " larger than definition len " + std::to_string(len));

        if (run + size > SPACE_END || load + size > SPACE_END)
            err(s.line, "section " + s.name + " (" + std::to_string(size) + " bytes) runs past 0xFFFFFFFF");
        bool dummy = attr == "DSECT" || attr == "COPY";  // TR 8.3.5: no memory needed, may overlap
        std::string memname;
        if (!d_.mems.empty() && !dummy && size > 0) {
            auto check = [&](int64_t a, bool is_run) {
                const Mem* m = area_of(a, size);
                std::string where = is_run ? "" : " (load address)";
                if (!m) {
                    const Mem* p = area_at(a);
                    if (p)
                        err(s.line, "\"" + s.name + "\" at 0x" + hexs(a) + where + " (" + std::to_string(size) +
                                        " bytes) won't fit into memory " + p->name);
                    else
                        err(s.line, "\"" + s.name + "\" enters unconfigured memory at 0x" + hexs(a) + where);
                    return;
                }
                if (is_run) {
                    memname = m->name;
                    int need = required_attrs(s.kind);
                    if ((m->attrs & need) != need)
                        err(s.line, "attributes mismatch between section and memory: " + s.name + " (" + s.kind + ") needs " +
                                        attr_text(need) + ", memory " + m->name + " is " + attr_text(m->attrs) + " (TR 8.2.2)");
                }
            };
            check(run, true);
            if (load != run) check(load, false);  // TR 8.3.4: the load area's type need not match
        }
        if (!dummy && size > 0) {
            std::vector<std::pair<int64_t, int64_t>> mine = {{load, load + size}};
            if (run != load) mine.push_back({run, run + size});
            if (attr != "OVERLAY")  // TR 8.3.5: OVERLAY may overlap others
                for (auto& iv : mine)
                    for (auto& x : occ_)
                        if (iv.first < x.hi && x.lo < iv.second)
                            err(s.line, "section \"" + s.name + "\" overlap: 0x" + hexs(iv.first) + "-0x" + hexs(iv.second - 1) +
                                            " overlaps " + x.who + " at 0x" + hexs(x.lo) + "-0x" + hexs(x.hi - 1));
            for (auto& iv : mine) occ_.push_back({iv.first, iv.second, "section " + s.name});
        }
        LcfOutput out;
        out.name = s.name;
        out.addr = run;
        out.load = load;
        out.size = size;
        out.attr = attr;
        out.has_pad = o && o->has_pad;
        out.pad = pad;
        out.memory = memname;
        out.automatic = !o;
        for (size_t j = 0; j < s.members.size(); j++) {
            const LcfInput& x = in_[s.members[j]];
            int64_t a = run + offs[j];
            if (a + x.size > width_limit(x.width))
                err(s.line, "section " + x.name + " at 0x" + hexs(a) + " does not fit its " + width_name(x.width) + " displacement");
            r_.placed[x.id] = LcfPlaced{a, load + offs[j]};
            out.inputs.push_back(x.id);
        }
        if (s.estimated && s.estimate != size)
            err(s.line, "sizeof(" + s.name + ") was used before the section was allocated, as " + std::to_string(s.estimate) +
                            ", but its size is " + std::to_string(size) + " (alignment padding depends on its address)");
        s.placed = true;
        s.addr = run;
        s.load = load;
        s.size = size;
        lc_ = load + size;  // TR 8.1.4: updated each time a section is allocated
        if (!o && d_.has_sections)
            warn(0, "section " + s.name + " is not named in the section definition part: linked as its own output section at 0x" +
                        hexs(run) + " (TR 8.3.6)");
        r_.outputs.push_back(out);
    }

    bool fit_size(const RtSec& s, int64_t len, int64_t align, int64_t& out) {
        int64_t size = layout(s, 0, nullptr, len);  // start is aligned to every member's alignment
        return find_space(s, size, align, out);
    }

    void assign(const Item& it) {
        int64_t v;
        try {
            v = eval(*it.e);
        } catch (EvalError& e) {
            err(it.line, it.sym + ": " + e.msg);
            return;
        }
        if (it.sym == ".") {
            int64_t nv = it.plus ? lc_ + v : v;
            if (nv >= SPACE_END) {
                err(it.line, "overflow in location counter");
                return;
            }
            if (nv < lc_) {  // TR 8.1.4 / 8.1.7
                err(it.line, "location counter can't point lower address: 0x" + hexs(nv) + " < 0x" + hexs(lc_));
                return;
            }
            lc_ = nv;
            return;
        }
        if (it.plus) {
            auto f = syms_.find(it.sym);
            if (f == syms_.end()) {
                err(it.line, it.sym + " += ...: undefined symbol '" + it.sym + "'");
                return;
            }
            v = int64_t((uint64_t(f->second) + uint64_t(v)) & M32);
        }
        syms_[it.sym] = v;
        r_.symbols.push_back({it.sym, v});
    }

    const LcfData& d_;
    const std::vector<LcfInput>& in_;
    LcfResult r_;
    std::vector<RtSec> secs_;
    size_t n_explicit_ = 0;
    std::map<std::string, size_t> by_name_;
    std::map<std::string, int64_t> syms_;
    std::vector<Occ> occ_;
    int64_t lc_ = 0;
};

}  // namespace

Lcf Lcf::parse_string(const std::string& text, const std::string& path) {
    Lcf l;
    l.d_ = std::make_shared<LcfData>(Parser(text, path).run());
    return l;
}

Lcf Lcf::parse_file(const std::string& path) {
    Bytes b = read_bytes(path);
    return parse_string(universal_newlines(std::string(b.begin(), b.end())), path);
}

LcfResult Lcf::place(const std::vector<LcfInput>& inputs, const std::vector<std::pair<int64_t, int64_t>>& used) const {
    return Placer(*d_, inputs, used).run();
}

}  // namespace unitron
