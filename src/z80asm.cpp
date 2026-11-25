// Z80 assembler (see z80asm.hpp). "ZM p.N" is the printed page of Zilog's Z80 Assembly Language
// Programming Manual (1978); "UM p.N" the printed page of UM0080.
#include "z80asm.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>

#include "util.hpp"
#include "z80.hpp"

namespace unitron::z80 {

namespace {

// An error with ZM's number (appendix A); 0 = no number (a limit of unitron, not of the language).
struct Err {
    int code;
    std::string msg;
    bool layout = false;   // found while sizing (pass 1): the addresses after it can't be trusted
};

// ZM appendix A numbers
constexpr int E_NAME = 2, E_OPCODE = 3, E_NUMBER = 4, E_OPERATOR = 5, E_SYNTAX = 6, E_UNDEF = 8, E_OPERANDS = 9,
              E_RANGE = 10, E_MULTIPLE = 11, E_QUOTES = 13, E_COMMAND = 14, E_INCLUDE = 17;

bool ident_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool ident_char(char c) { return ident_start(c) || py_digit(c) || c == '?' || c == '_'; }

// ZM p.12: "All opcodes and keywords ... must be either all capitals or all lower case."
bool one_case(const std::string& s) { return s == upper(s) || s == lower(s); }

const std::set<std::string>& keywords() {
    // ZM p.4: register names, register pairs, AF' and the flag conditions
    static const std::set<std::string> k = {"A",  "B",  "C",  "D",  "E",  "H",  "L",  "I",  "R",  "AF", "BC", "DE",
                                            "HL", "SP", "IX", "IY", "AF'", "NZ", "Z", "NC", "PO", "PE", "P", "M"};
    return k;
}
// s as an operand keyword (upper case), or "" if it is none (wrong case counts as none: ZM p.12)
std::string keyword(const std::string& s) {
    if (!one_case(s)) return "";
    std::string u = upper(s);
    return keywords().count(u) ? u : "";
}

// ---------------------------------------------------------------- expressions (ZM p.7-9)
struct Node;
using NodeP = std::shared_ptr<const Node>;
struct Node {
    enum K { Num, Sym, Dollar, Un, Bin } k = Num;
    int64_t v = 0;
    std::string s;   // symbol name or operator
    NodeP a, b;
};

struct Lexer {
    std::string t;
    size_t i = 0;
    struct Tok {
        enum K { Num, Sym, Dollar, Op, LP, RP, End } k = End;
        int64_t v = 0;
        std::string s;
    };
    Tok cur;
    explicit Lexer(std::string text) : t(std::move(text)) { next(); }

    static int64_t number(const std::string& w) {
        // ZM p.12-13: a leading digit, then an optional base letter B, O/Q, D or H (any case)
        char last = char(std::toupper((unsigned char)w.back()));
        int base = 10;
        std::string digits = w;
        if (last == 'H') base = 16;
        else if (last == 'B') base = 2;
        else if (last == 'O' || last == 'Q') base = 8;
        else if (last == 'D') base = 10;
        if (!py_digit((unsigned char)last)) digits = w.substr(0, w.size() - 1);
        if (digits.empty()) throw Err{E_NUMBER, "invalid number '" + w + "'"};
        int64_t v = 0;
        for (char c : digits) {
            int dv = py_digit((unsigned char)c) ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 99;
            if (dv >= base) throw Err{E_NUMBER, "invalid number '" + w + "'"};
            v = v * base + dv;
            if (v > 0xFFFFFFFFLL) throw Err{E_RANGE, "number '" + w + "' is out of range"};
        }
        return v;
    }

    void next() {
        while (i < t.size() && py_space((unsigned char)t[i])) i++;
        cur = Tok();
        if (i >= t.size()) return;
        char c = t[i];
        if (py_digit((unsigned char)c)) {
            size_t j = i;
            while (j < t.size() && (std::isalnum((unsigned char)t[j]))) j++;
            cur.k = Tok::Num;
            cur.v = number(t.substr(i, j - i));
            i = j;
        } else if (ident_start(c)) {
            size_t j = i;
            while (j < t.size() && ident_char(t[j])) j++;
            cur.k = Tok::Sym;
            cur.s = t.substr(i, j - i);
            i = j;
        } else if (c == '$') {
            cur.k = Tok::Dollar;
            i++;
        } else if (c == '(') {
            cur.k = Tok::LP;
            i++;
        } else if (c == ')') {
            cur.k = Tok::RP;
            i++;
        } else if (c == '.') {
            size_t j = t.find('.', i + 1);
            if (j == std::string::npos) throw Err{E_OPERATOR, "invalid operator '" + t.substr(i) + "'"};
            std::string w = upper(t.substr(i, j + 1 - i));
            static const std::set<std::string> ops = {".NOT.", ".RES.", ".MOD.", ".SHR.", ".SHL.", ".AND.", ".OR.",
                                                      ".XOR.", ".EQ.",  ".GT.",  ".LT.",  ".UGT.", ".ULT."};
            if (!ops.count(w)) throw Err{E_OPERATOR, "invalid operator '" + t.substr(i, j + 1 - i) + "'"};
            cur.k = Tok::Op;
            cur.s = w;
            i = j + 1;
        } else if (c == '*' && i + 1 < t.size() && t[i + 1] == '*') {
            cur.k = Tok::Op;
            cur.s = "**";
            i += 2;
        } else if (std::string("+-*/&\\=><").find(c) != std::string::npos) {
            cur.k = Tok::Op;
            // the symbol spellings of ZM p.8 stand for the dotted operators
            static const std::map<char, std::string> alias = {{'&', ".AND."}, {'\\', ".NOT."}, {'=', ".EQ."}, {'>', ".GT."}, {'<', ".LT."}};
            auto a = alias.find(c);
            cur.s = a == alias.end() ? std::string(1, c) : a->second;
            i++;
        } else if (c == '\'') {
            throw Err{E_SYNTAX, "a string is not allowed in an expression"};
        } else {
            throw Err{E_SYNTAX, "unexpected character '" + std::string(1, c) + "'"};
        }
    }
};

// ZM p.8 priorities: 1 unary, 2 **, 3 * / .MOD. .SHR. .SHL., 4 + -, 5 .AND., 6 .OR. .XOR., 7 comparisons
int prio(const std::string& op) {
    if (op == "**") return 2;
    if (op == "*" || op == "/" || op == ".MOD." || op == ".SHR." || op == ".SHL.") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == ".AND.") return 5;
    if (op == ".OR." || op == ".XOR.") return 6;
    if (op == ".EQ." || op == ".GT." || op == ".LT." || op == ".UGT." || op == ".ULT.") return 7;
    return 0;
}

NodeP parse_level(Lexer& lx, int level);

NodeP parse_unary(Lexer& lx) {
    auto& t = lx.cur;
    if (t.k == Lexer::Tok::Op && (t.s == "+" || t.s == "-" || t.s == ".NOT." || t.s == ".RES.")) {
        auto n = std::make_shared<Node>();
        n->k = Node::Un;
        n->s = t.s;
        lx.next();
        n->a = parse_unary(lx);
        return n;
    }
    auto n = std::make_shared<Node>();
    if (t.k == Lexer::Tok::Num) {
        n->v = t.v;
    } else if (t.k == Lexer::Tok::Sym) {
        if (!keyword(t.s).empty()) throw Err{E_OPERANDS, "register or condition '" + t.s + "' in an expression"};
        n->k = Node::Sym;
        n->s = t.s;
    } else if (t.k == Lexer::Tok::Dollar) {
        n->k = Node::Dollar;
    } else if (t.k == Lexer::Tok::LP) {
        lx.next();
        NodeP in = parse_level(lx, 7);
        if (lx.cur.k != Lexer::Tok::RP) throw Err{E_SYNTAX, "unmatched parenthesis"};
        lx.next();
        return in;
    } else {
        throw Err{E_SYNTAX, t.k == Lexer::Tok::End ? "missing operand" : "syntax error in expression"};
    }
    lx.next();
    return n;
}

NodeP parse_level(Lexer& lx, int level) {
    if (level == 1) return parse_unary(lx);
    NodeP a = parse_level(lx, level - 1);
    while (lx.cur.k == Lexer::Tok::Op && prio(lx.cur.s) == level) {   // left to right (ZM p.7)
        auto n = std::make_shared<Node>();
        n->k = Node::Bin;
        n->s = lx.cur.s;
        lx.next();
        n->a = a;
        n->b = parse_level(lx, level - 1);
        a = n;
    }
    return a;
}

NodeP parse_expr(const std::string& text) {
    if (strip(text).empty()) throw Err{E_SYNTAX, "missing operand"};
    Lexer lx(text);
    NodeP n = parse_level(lx, 7);
    if (lx.cur.k != Lexer::Tok::End) throw Err{E_SYNTAX, "syntax error in expression '" + strip(text) + "'"};
    return n;
}

struct Undefined {
    std::string name;
};

using Lookup = std::function<int64_t(const std::string&)>;   // throws Undefined

int64_t eval(const NodeP& n, const Lookup& look, int64_t here) {
    switch (n->k) {
    case Node::Num: return n->v;
    case Node::Sym: return look(n->s);
    case Node::Dollar: return here;
    case Node::Un: {
        int64_t a = eval(n->a, look, here);
        if (n->s == "-") return -a;
        if (n->s == "+") return a;
        if (n->s == ".NOT.") return (~a) & 0xFFFF;   // 16-bit arithmetic (ZM p.8)
        return a & 0xFFFF;                           // .RES.: the 16-bit result, overflow ignored (ZM p.8-9)
    }
    case Node::Bin: {
        int64_t a = eval(n->a, look, here), b = eval(n->b, look, here);
        const std::string& o = n->s;
        auto big = [](int64_t v) { return v > 0xFFFFFFFFLL || v < -0xFFFFFFFFLL; };
        int64_t r = 0;
        if (o == "+") r = a + b;
        else if (o == "-") r = a - b;
        else if (o == "*") r = a * b;
        else if (o == "**") {
            if (b < 0) throw Err{E_RANGE, "negative exponent"};
            r = 1;
            for (int64_t k = 0; k < b; k++) {
                r *= a;
                if (big(r)) throw Err{E_RANGE, "expression out of range"};
            }
        } else if (o == "/" || o == ".MOD.") {
            if (b == 0) throw Err{E_RANGE, "division by zero"};
            r = o == "/" ? a / b : a - b * (a / b);   // ZM p.9: X.MOD.Y = X-Y*(X/Y)
        } else if (o == ".SHR.") r = b >= 16 || b < 0 ? 0 : (a & 0xFFFF) >> b;
        else if (o == ".SHL.") r = b >= 32 || b < 0 ? 0 : (a & 0xFFFF) << b;
        else if (o == ".AND.") r = (a & b) & 0xFFFF;
        else if (o == ".OR.") r = (a | b) & 0xFFFF;
        else if (o == ".XOR.") r = (a ^ b) & 0xFFFF;
        else {
            // ZM p.8: true is all ones (0FFFFH in 16 bits), false is 0; .GT./.LT. signed, .UGT./.ULT. unsigned
            auto s16 = [](int64_t v) { return int64_t(int16_t(uint16_t(v & 0xFFFF))); };
            bool t = false;
            if (o == ".EQ.") t = (a & 0xFFFF) == (b & 0xFFFF);
            else if (o == ".GT.") t = s16(a) > s16(b);
            else if (o == ".LT.") t = s16(a) < s16(b);
            else if (o == ".UGT.") t = (a & 0xFFFF) > (b & 0xFFFF);
            else if (o == ".ULT.") t = (a & 0xFFFF) < (b & 0xFFFF);
            r = t ? 0xFFFF : 0;
        }
        if (big(r)) throw Err{E_RANGE, "expression out of range"};
        return r;
    }
    }
    return 0;
}

// ---------------------------------------------------------------- source lines

// Index of the first `;` that is not inside a string, or npos. `AF'` is not a string (ZM p.4).
bool af_quote(const std::string& s, size_t q) {
    if (q < 2) return false;
    std::string w = s.substr(q - 2, 2);
    if (w != "AF" && w != "af") return false;
    return q < 3 || !ident_char(s[q - 3]);
}

size_t comment_at(const std::string& s) {
    bool in = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (in) {
            if (c == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'') i++;
                else in = false;
            }
        } else if (c == '\'') {
            if (!af_quote(s, i)) in = true;
        } else if (c == ';') {
            return i;
        }
    }
    return std::string::npos;
}

// Operands separated by commas outside parentheses and strings.
std::vector<std::string> split_operands(const std::string& f) {
    std::vector<std::string> out;
    if (strip(f).empty()) return out;
    std::string cur;
    int depth = 0;
    bool in = false;
    for (size_t i = 0; i < f.size(); i++) {
        char c = f[i];
        if (in) {
            cur += c;
            if (c == '\'') {
                if (i + 1 < f.size() && f[i + 1] == '\'') cur += f[++i];
                else in = false;
            }
            continue;
        }
        if (c == '\'' && !af_quote(f, i)) in = true;
        if (c == '(') depth++;
        if (c == ')') depth--;
        if (c == ',' && depth == 0) {
            out.push_back(strip(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (in) throw Err{E_QUOTES, "unbalanced quotes"};
    out.push_back(strip(cur));
    return out;
}

// 'text' -> bytes ('' inside is one quote; ZM appendix A no. 13)
std::optional<Bytes> string_operand(const std::string& s) {
    if (s.size() < 2 || s.front() != '\'' || s.back() != '\'') return std::nullopt;
    Bytes b;
    for (size_t i = 1; i + 1 < s.size(); i++) {
        if (s[i] == '\'') {
            if (i + 2 < s.size() && s[i + 1] == '\'') i++;
            else throw Err{E_QUOTES, "unbalanced quotes"};
        }
        b.push_back(uint8_t(s[i]));
    }
    return b;
}

// A classified operand.
struct Arg {
    enum K { Tok, Expr, Mem, Idx } k = Expr;
    std::string tok;   // Tok: keyword, "(HL)", "(C)", ...; Idx: "IX"/"IY"
    NodeP e;           // Expr, Mem, Idx (the displacement; null for (IX) itself)
    std::string text;
};

// whole s is wrapped in one pair of parentheses
bool wrapped(const std::string& s) {
    if (s.size() < 2 || s.front() != '(' || s.back() != ')') return false;
    int depth = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '(') depth++;
        if (s[i] == ')' && --depth == 0 && i + 1 != s.size()) return false;
    }
    return true;
}

Arg classify(const std::string& s) {
    Arg a;
    a.text = s;
    if (s.empty()) throw Err{E_SYNTAX, "missing operand"};
    std::string kw = keyword(s);
    if (!kw.empty()) {
        a.k = Arg::Tok;
        a.tok = kw;
        return a;
    }
    // ZM p.2/p.8: an operand wholly in parentheses is a memory location
    if (wrapped(s)) {
        std::string in = strip(s.substr(1, s.size() - 2));
        std::string ik = keyword(in);
        if (ik == "HL" || ik == "BC" || ik == "DE" || ik == "SP" || ik == "C" || ik == "IX" || ik == "IY") {
            a.k = Arg::Tok;
            a.tok = "(" + ik + ")";
            return a;
        }
        if (in.size() > 2) {
            std::string r = keyword(in.substr(0, 2));
            std::string rest = strip(in.substr(2));
            if ((r == "IX" || r == "IY") && !rest.empty() && (rest[0] == '+' || rest[0] == '-')) {
                a.k = Arg::Idx;
                a.tok = r;
                a.e = parse_expr(rest);   // the sign is part of the displacement: (IX-5)
                return a;
            }
        }
        a.k = Arg::Mem;
        a.e = parse_expr(in);
        return a;
    }
    a.k = Arg::Expr;
    a.e = parse_expr(s);
    return a;
}

bool shape(const Pat& p, const Arg& a) {
    switch (p.k) {
    case P::Lit: return a.k == Arg::Tok && a.tok == p.lit;
    case P::N:
    case P::NN:
    case P::Rel:
    case P::Const: return a.k == Arg::Expr;
    case P::Mem:
    case P::Port: return a.k == Arg::Mem;
    case P::IX: return (a.k == Arg::Idx && a.tok == "IX") || (a.k == Arg::Tok && a.tok == "(IX)");
    case P::IY: return (a.k == Arg::Idx && a.tok == "IY") || (a.k == Arg::Tok && a.tok == "(IY)");
    }
    return false;
}

const std::map<std::string, std::vector<int>>& by_mnem() {
    static const std::map<std::string, std::vector<int>> m = [] {
        std::map<std::string, std::vector<int>> r;
        auto& fs = forms();
        for (size_t i = 0; i < fs.size(); i++)
            if (!fs[i].alt) r[fs[i].mnem].push_back(int(i));
        return r;
    }();
    return m;
}

// The value check of a field (UM p.39 table 4: n 0-255, nn 0-65535, d -128..+127). Negative n
// and nn are accepted as two's complement (ZM p.8: "LD HL,-0EA9H").
void check(int64_t v, int64_t lo, int64_t hi, const std::string& what, const std::string& text) {
    if (v < lo || v > hi)
        throw Err{E_RANGE, what + " " + text + (strip(text) == std::to_string(v) ? "" : " (= " + std::to_string(v) + ")") + " is out of range"};
}

// An instruction: size in pass 1 (values may be unknown), bytes in pass 2.
struct Encoded {
    int size = 0;
    Bytes b;
};

Encoded encode_insn(const std::string& mnem, const std::string& field, int64_t pc, const Lookup& look, bool final) {
    if (!one_case(mnem)) throw Err{E_OPCODE, "invalid opcode '" + mnem + "' (mixed case)"};
    auto it = by_mnem().find(upper(mnem));
    if (it == by_mnem().end()) throw Err{E_OPCODE, "invalid opcode '" + mnem + "'"};
    std::vector<Arg> args;
    for (auto& s : split_operands(field)) args.push_back(classify(s));
    std::vector<const Form*> fit;
    for (int fi : it->second) {
        const Form& f = forms()[size_t(fi)];
        if (f.ops.size() != args.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < args.size() && ok; i++) ok = shape(f.ops[i], args[i]);
        if (ok) fit.push_back(&f);
    }
    if (fit.empty()) throw Err{E_OPERANDS, "invalid operand combination for " + upper(mnem) + (field.empty() ? "" : " " + strip(field))};
    auto value = [&](const NodeP& e) -> std::optional<int64_t> {
        try {
            return eval(e, look, pc);
        } catch (Undefined& u) {
            if (final) throw Err{E_UNDEF, "undefined symbol '" + u.name + "'"};
            return std::nullopt;
        }
    };
    // Const operands (BIT b, IM m, RST p) pick the form; all candidates have the same size
    const Form* f = fit.front();
    for (size_t i = 0; i < args.size(); i++)
        if (f->ops[i].k == P::Const) {
            auto v = value(args[i].e);
            if (!v) return {f->size(), {}};
            const Form* hit = nullptr;
            for (auto* g : fit)
                if (g->ops[i].v == *v) hit = g;
            if (!hit) {
                std::string m = upper(mnem);
                std::string want = m == "IM" ? "0, 1 or 2" : m == "RST" ? "00H, 08H, 10H, 18H, 20H, 28H, 30H or 38H" : "0 to 7";
                throw Err{E_RANGE, m + " " + args[i].text + (args[i].text == std::to_string(*v) ? "" : " (= " + std::to_string(*v) + ")") +
                                      " is out of range (" + want + ")"};
            }
            f = hit;
        }
    if (!final) return {f->size(), {}};
    int64_t n = 0, nn = 0, d = 0, target = 0;
    for (size_t i = 0; i < args.size(); i++) {
        const Pat& p = f->ops[i];
        const Arg& a = args[i];
        switch (p.k) {
        case P::N:
        case P::Port:
            n = *value(a.e);
            check(n, -128, 255, "byte", a.text);
            break;
        case P::NN:
        case P::Mem:
            nn = *value(a.e);
            check(nn, -32768, 65535, "word", a.text);
            break;
        case P::IX:
        case P::IY:
            d = a.e ? *value(a.e) : 0;
            check(d, -128, 127, "displacement", a.text);
            break;
        case P::Rel: {
            target = *value(a.e);
            check(target, -32768, 65535, "address", a.text);
            int64_t off = ((target - (pc + f->size())) & 0xFFFF);
            if (off >= 0x8000) off -= 0x10000;
            if (off < -128 || off > 127)   // ZM p.9: 128 bytes either way from the next instruction
                throw Err{E_RANGE, "relative jump to " + a.text + " is out of range (" + std::to_string(off) + " bytes)"};
            target = (pc + f->size() + off);
            break;
        }
        default: break;
        }
    }
    return {f->size(), encode(*f, n, nn, d, target, pc)};
}

}  // namespace

// ---------------------------------------------------------------- the assembler

struct Assembler::State {
    Assembler& as;
    int pass = 1;
    int64_t pc = 0;
    bool ended = false;
    bool cond_off = false, in_cond = false;
    Where cond_where;
    struct Sym {
        enum K { Label, Equ, Defl } k = Label;
        int64_t v = 0;
        NodeP e;
        int64_t here = 0;
        bool busy = false;
        Where where;
    };
    std::map<std::string, Sym> syms;
    std::vector<std::string> order;
    std::set<std::string> defined;   // in this pass
    std::vector<int> mem = std::vector<int>(size_t(RAM_SIZE), -1);
    int64_t top = 0;
    std::vector<std::string> layout_errors;
    std::map<std::string, std::string> file_cache;

    explicit State(Assembler& a) : as(a) {}

    void error(const Where& w, const Err& e) {
        std::string m = Diags::fmt(w, "error", e.code, e.msg);
        if (pass == 2) as.d.errors.push_back(m);
        else if (e.layout) layout_errors.push_back(m);   // other pass-1 errors come again in pass 2
    }

    int64_t lookup(const std::string& name) {
        auto it = syms.find(name);
        if (it == syms.end()) throw Undefined{name};
        Sym& s = it->second;
        if (s.k != Sym::Equ) return s.v;
        if (s.busy) throw Err{E_UNDEF, "'" + name + "' is defined in terms of itself"};
        s.busy = true;
        try {
            int64_t v = eval(s.e, [this](const std::string& n) { return lookup(n); }, s.here);
            s.busy = false;
            return v;
        } catch (...) {
            s.busy = false;
            throw;
        }
    }
    Lookup look() {
        return [this](const std::string& n) { return lookup(n); };
    }
    // a value needed now (ORG, DEFS, DEFL, COND): forward references are errors
    int64_t now(const std::string& text) {
        try {
            return eval(parse_expr(text), look(), pc);
        } catch (Undefined& u) {
            throw Err{E_UNDEF, "undefined symbol '" + u.name + "' (it must be defined before this line)", true};
        } catch (Err& e) {
            e.layout = true;
            throw;
        }
    }

    static void layout_check(int64_t v, int64_t hi, const std::string& what, const std::string& text) {
        try {
            check(v, 0, hi, what, text);
        } catch (Err& e) {
            e.layout = true;
            throw;
        }
    }

    void check_name(const std::string& n) {
        if (n.empty() || !ident_start(n[0])) throw Err{E_NAME, "name '" + n + "' contains invalid characters"};
        for (char c : n)
            if (!ident_char(c)) throw Err{E_NAME, "name '" + n + "' contains invalid characters"};
        if (!keyword(n).empty()) throw Err{E_NAME, "'" + n + "' is a register or condition name"};
    }

    void define(const std::string& name, Sym s, const Where& w) {
        check_name(name);
        auto it = syms.find(name);
        bool again = defined.count(name) > 0;
        if (again && !(s.k == Sym::Defl && it->second.k == Sym::Defl)) {
            if (pass == 2) throw Err{E_MULTIPLE, "multiple declaration of '" + name + "'"};
            return;
        }
        s.where = w;
        if (it == syms.end()) order.push_back(name);
        syms[name] = s;
        defined.insert(name);
    }

    void emit(const Bytes& b, const Where& w) {
        for (uint8_t x : b) {
            if (pc >= RAM_SIZE) throw Err{0, "address " + hexz(pc, 4) + " is beyond the Z80's 4 KB of RAM (0000H-0FFFH)"};
            if (pass == 2) {
                if (mem[size_t(pc)] >= 0) throw Err{0, "address " + hexz(pc, 4) + " is written twice (check the ORG lines)"};
                mem[size_t(pc)] = x;
                top = std::max(top, pc + 1);
            }
            pc++;
        }
        (void)w;
    }
    void advance(int64_t n) {
        if (pass == 1) {
            if (pc + n > RAM_SIZE) throw Err{0, "address " + hexz(pc + n - 1, 4) + " is beyond the Z80's 4 KB of RAM (0000H-0FFFH)", true};
            pc += n;
        }
    }

    std::string find_include(const std::string& name, const std::string& cur) {
        std::vector<std::string> cands = {path_join(path_parent(cur), name)};
        for (auto& d : as.include_dirs_) cands.push_back(path_join(d, name));
        if (path_absolute(name)) cands = {name};
        for (auto& c : cands)
            if (is_file(c)) return c;
        return "";
    }

    void run_file(const std::string& path, std::vector<Loc> chain) {
        std::string text;
        auto cached = file_cache.find(path);
        if (cached != file_cache.end()) text = cached->second;
        else {
            Bytes raw = read_bytes(path);
            text = universal_newlines(std::string(raw.begin(), raw.end()));
            file_cache[path] = text;
        }
        int ln = 0;
        for (auto& line : splitlines(text)) {
            ln++;
            if (ended) return;
            Where w;
            w.file = path;
            w.line = ln;
            w.chain = chain;
            try {
                statement(line, w, path, chain);
            } catch (Err& e) {
                error(w, e);
            }
        }
    }

    void command(const std::string& line, const Where& w, const std::string& path, const std::vector<Loc>& chain) {
        // ZM p.13-14: an assembler command is a line with * in column one; column two names it
        char c = line.size() > 1 ? char(std::toupper((unsigned char)line[1])) : ' ';
        if (c == 'I') {
            auto words = split_ws(line.substr(1));
            if (cond_off) return;
            if (words.size() != 2 || upper(words[0]) != "INCLUDE")   // commands are in any case (ZM p.12)
                throw Err{E_COMMAND, "expected *Include filename"};
            if (chain.size() + 1 >= 4) throw Err{E_INCLUDE, "include nested too deep (four levels at most)"};
            std::string f = find_include(words[1], path);
            if (f.empty()) throw Err{E_COMMAND, "include file '" + words[1] + "' not found"};
            auto ch = chain;
            ch.push_back(w.loc());
            run_file(f, ch);
            return;
        }
        // *Eject, *Heading, *List, *Maclist only shape the printed listing, which unitron doesn't write
        if (c == 'E' || c == 'H' || c == 'L' || c == 'M') return;
        throw Err{E_COMMAND, "unknown assembler command '" + rstrip(line) + "'"};
    }

    void statement(const std::string& raw, const Where& w, const std::string& path, const std::vector<Loc>& chain) {
        if (!raw.empty() && raw[0] == '*') return command(raw, w, path, chain);
        std::string line = raw;
        size_t sc = comment_at(line);
        if (sc != std::string::npos) line = line.substr(0, sc);
        line = rstrip(line);
        if (strip(line).empty()) return;
        // ZM p.6: a label ends with a colon, or starts in column one
        std::string label;
        size_t i = 0;
        if (!py_space((unsigned char)line[0])) {
            while (i < line.size() && !py_space((unsigned char)line[i]) && line[i] != ':') i++;
            label = line.substr(0, i);
            if (i < line.size() && line[i] == ':') i++;
        } else {
            size_t j = 0;
            while (j < line.size() && py_space((unsigned char)line[j])) j++;
            size_t k = j;
            while (k < line.size() && ident_char(line[k])) k++;
            if (k < line.size() && line[k] == ':' && k > j) {
                label = line.substr(j, k - j);
                i = k + 1;
            }
        }
        std::string rest = strip(line.substr(i));
        size_t e = 0;
        while (e < rest.size() && !py_space((unsigned char)rest[e])) e++;
        std::string op = rest.substr(0, e), field = strip(rest.substr(e));
        std::string uop = one_case(op) ? upper(op) : op;
        // conditional assembly (ZM p.11): COND nn ... ENDC, not nested
        if (uop == "COND") {
            if (in_cond) throw Err{E_SYNTAX, "COND cannot be nested"};
            in_cond = true;
            cond_where = w;
            if (!label.empty()) throw Err{E_SYNTAX, "COND takes no label"};
            cond_off = now(field) == 0;
            return;
        }
        if (uop == "ENDC") {
            if (!in_cond) throw Err{E_SYNTAX, "ENDC without COND"};
            in_cond = cond_off = false;
            return;
        }
        if (cond_off) return;
        if (uop == "EQU" || uop == "DEFL") {
            if (label.empty()) throw Err{E_SYNTAX, uop + " needs a label"};
            Sym s;
            if (uop == "EQU") {
                s.k = Sym::Equ;
                s.e = parse_expr(field);
                s.here = pc;
            } else {
                s.k = Sym::Defl;
                s.v = now(field);
            }
            define(label, s, w);
            return;
        }
        if (!label.empty()) {
            Sym s;
            s.v = pc;
            define(label, s, w);
        }
        if (op.empty()) return;
        if (uop == "ORG") {
            int64_t v = now(field);
            layout_check(v, 0xFFFF, "address", field);
            pc = v;
            return;
        }
        if (uop == "END") {
            if (!field.empty()) throw Err{E_OPERANDS, "END takes no operand"};
            ended = true;
            return;
        }
        if (uop == "DEFB" || uop == "DEFW") {
            auto ops = split_operands(field);
            if (ops.size() != 1) throw Err{E_OPERANDS, uop + " takes one operand (ZM p.10)"};
            int width = uop == "DEFB" ? 1 : 2;
            if (pass == 1) return advance(width);
            int64_t v;
            auto s = string_operand(ops[0]);
            if (s && width == 1) {
                if (s->size() != 1) throw Err{E_OPERANDS, "DEFB takes one character; use DEFM for a string"};
                v = (*s)[0];
            } else {
                try {
                    v = eval(parse_expr(ops[0]), look(), pc);
                } catch (Undefined& u) {
                    throw Err{E_UNDEF, "undefined symbol '" + u.name + "'"};
                }
                if (width == 1) check(v, -128, 255, "byte", ops[0]);
                else check(v, -32768, 65535, "word", ops[0]);
            }
            Bytes b = {uint8_t(v & 0xFF)};
            if (width == 2) b.push_back(uint8_t(v >> 8 & 0xFF));
            return emit(b, w);
        }
        if (uop == "DEFS") {
            int64_t n = now(field);
            layout_check(n, 0xFFFF, "size", field);
            if (pass == 1) return advance(n);
            return emit(Bytes(size_t(n), 0), w);   // unitron writes zeros (docs/z80.md)
        }
        if (uop == "DEFM") {
            auto s = string_operand(strip(field));
            if (!s) throw Err{E_SYNTAX, "DEFM needs a string in single quotes"};
            if (s->size() > 63) throw Err{E_SYNTAX, "DEFM string longer than 63 characters"};
            if (pass == 1) return advance(int64_t(s->size()));
            return emit(*s, w);
        }
        if (uop == "MACRO" || uop == "ENDM") throw Err{0, "macros are not supported yet (docs/z80.md)"};
        Encoded enc = encode_insn(op, field, pc, look(), pass == 2);
        if (pass == 1) return advance(enc.size);
        emit(enc.b, w);
    }

    void run(const std::string& path) {
        for (pass = 1; pass <= 2; pass++) {
            pc = 0;
            ended = cond_off = in_cond = false;
            defined.clear();
            try {
                run_file(path, {});
            } catch (std::runtime_error& e) {
                as.d.errors.push_back(Diags::fmt(Where::None(), "error", 0, e.what()));
                return;
            }
            if (in_cond) error(cond_where, Err{E_SYNTAX, "COND without ENDC"});
            if (pass == 1 && !layout_errors.empty()) {
                as.d.errors = layout_errors;
                return;
            }
        }
    }
};

bool Assembler::assemble(const std::string& path) {
    State st(*this);
    st.run(path);
    if (!d.errors.empty()) return false;
    image.assign(size_t(st.top), 0);
    for (int64_t a = 0; a < st.top; a++)
        if (st.mem[size_t(a)] >= 0) image[size_t(a)] = uint8_t(st.mem[size_t(a)]);
    symbols.clear();
    labels.clear();
    for (auto& n : st.order) {
        try {
            symbols.push_back({n, st.lookup(n)});
            if (st.syms[n].k == State::Sym::Label) labels.push_back(symbols.back());
        } catch (...) {
        }
    }
    return true;
}

Bytes assemble_line(const std::string& line, int64_t pc) {
    std::string t = strip(line);
    size_t e = 0;
    while (e < t.size() && !py_space((unsigned char)t[e])) e++;
    try {
        Encoded enc = encode_insn(t.substr(0, e), strip(t.substr(e)), pc, [](const std::string& n) -> int64_t { throw Undefined{n}; }, true);
        return enc.b;
    } catch (Err& x) {
        throw std::runtime_error((x.code ? "E" + std::to_string(x.code) + ": " : "") + x.msg);
    }
}

}  // namespace unitron::z80
