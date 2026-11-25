#include "expr.hpp"

#include <vector>

namespace unitron {

bool INTEL_SUFFIX = false;

ExprP num_expr(int64_t v) {
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Num;
    e->v = v;
    return e;
}

int64_t wrap32(int64_t v) {
    uint32_t u = uint32_t(uint64_t(v));
    return u & 0x80000000u ? int64_t(u) - (int64_t(1) << 32) : int64_t(u);
}

// ---- Python int(s, base) for the bases used here, as a bignum (base 2^32 limbs)
static int digit_of(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

static bool py_int_base(const std::string& s0, int base, Number& out) {
    std::string s = s0;
    std::string l = lower(s);
    const char* pre = base == 16 ? "0x" : base == 8 ? "0o" : base == 2 ? "0b" : nullptr;
    if (pre && starts_with(l, pre)) {
        s = s.substr(2);
        if (!s.empty() && s[0] == '_') s = s.substr(1);
    }
    if (s.empty() || s.front() == '_' || s.back() == '_') return false;
    std::vector<uint32_t> limbs{0};
    char prev = 0;
    for (char c : s) {
        if (c == '_') {
            if (prev == '_') return false;
            prev = c;
            continue;
        }
        prev = c;
        int d = digit_of(c);
        if (d >= base) return false;
        uint64_t carry = uint64_t(d);
        for (auto& x : limbs) {
            uint64_t t = uint64_t(x) * uint64_t(base) + carry;
            x = uint32_t(t);
            carry = t >> 32;
        }
        if (carry) limbs.push_back(uint32_t(carry));
    }
    uint64_t lo = limbs[0] | (limbs.size() > 1 ? uint64_t(limbs[1]) << 32 : 0);
    out.v = int64_t(lo);
    out.big = limbs.size() > 2 || (lo >> 63);
    if (out.big) {
        std::vector<uint32_t> t = limbs;
        std::string d;
        while (!(t.size() == 1 && t[0] == 0)) {
            uint64_t r = 0;
            for (size_t i = t.size(); i-- > 0;) {
                uint64_t cur = (r << 32) | t[i];
                t[i] = uint32_t(cur / 10);
                r = cur % 10;
            }
            d.insert(d.begin(), char('0' + r));
            while (t.size() > 1 && t.back() == 0) t.pop_back();
        }
        out.decimal = d;
    }
    return true;
}

Number parse_number_full(const std::string& t) {
    std::string tl = lower(t);
    Number n;
    bool ok;
    if (starts_with(tl, "0x")) ok = py_int_base(tl.substr(2), 16, n);
    else if (starts_with(tl, "0y")) ok = py_int_base(tl.substr(2), 2, n);
    else if (INTEL_SUFFIX && tl.size() > 1 && std::string("hboqd").find(tl.back()) != std::string::npos) {
        char c = tl.back();
        int base = c == 'h' ? 16 : c == 'b' ? 2 : c == 'd' ? 10 : 8;
        ok = py_int_base(tl.substr(0, tl.size() - 1), base, n);
    } else if (tl.size() > 1 && tl[0] == '0') ok = py_int_base(tl.substr(1), 8, n);
    else ok = py_int_base(tl, 10, n);
    if (ok) return n;
    if (starts_with(tl, "0b"))
        throw ExprError("illegal numeric constant " + py_repr(t) +
                        ": asm900 writes binary as 0y (0b is rejected, E202)");
    if ((tl.empty() || std::string("hboqd").find(tl.back()) != std::string::npos) && !INTEL_SUFFIX)
        throw ExprError("illegal numeric constant " + py_repr(t) + ": suffixed numbers need -H (E202)");
    throw ExprError("illegal numeric constant " + py_repr(t) + " (E202)");
}

int64_t parse_number(const std::string& t) { return parse_number_full(t).v; }

static int64_t esc_value(uint32_t c) {
    switch (c) {
        case 'b': return 8;
        case 'n': return 10;
        case 't': return 9;
        case '"': return 0x22;
        case '?': return 0x3F;
        case 'a': return 7;
        case 'f': return 12;
        case 'r': return 13;
        case 'v': return 11;
        case '\'': return 0x27;
        case '\\': return 0x5C;
    }
    return -1;
}

static bool is_hex(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

Bytes unescape(const std::string& body8) {
    auto body = utf8_decode(body8);
    Bytes out;
    size_t i = 0, n = body.size();
    while (i < n) {
        uint32_t c = body[i];
        if (c != '\\') {
            if (c > 0xFF) throw ExprError("character " + py_repr(utf8_encode({c})) + " is not a byte");
            out.push_back(uint8_t(c));
            i++;
            continue;
        }
        i++;
        if (i >= n) throw ExprError("string ends in a backslash");
        c = body[i];
        if (c == 'x') {
            size_t k = i + 1;
            int v = 0, m = 0;
            while (k < n && m < 2 && is_hex(body[k])) {
                v = v * 16 + digit_of(char(body[k]));
                k++;
                m++;
            }
            if (!m) throw ExprError("\\x needs hex digits");
            out.push_back(uint8_t(v));
            i = k;
        } else if (c >= '0' && c <= '7') {
            size_t k = i;
            int v = 0, m = 0;
            std::string txt;
            while (k < n && m < 3 && body[k] >= '0' && body[k] <= '7') {
                v = v * 8 + int(body[k] - '0');
                txt += char(body[k]);
                k++;
                m++;
            }
            if (v > 0xFF) throw ExprError("octal escape \\" + txt + " above 377");
            out.push_back(uint8_t(v));
            i = k;
        } else if (esc_value(c) >= 0) {
            out.push_back(uint8_t(esc_value(c)));
            i++;
        } else {
            throw ExprError("unknown escape \\" + utf8_encode({c}));
        }
    }
    return out;
}

int64_t char_value(const std::string& body) {
    Bytes b = unescape(body);
    if (b.empty()) throw ExprError("empty character constant");
    int64_t v = 0;
    size_t s = b.size() > 4 ? b.size() - 4 : 0;
    for (size_t i = s; i < b.size(); i++) v = (v << 8) | b[i];
    return v;
}

int64_t apply_op(const std::string& op, int64_t a, int64_t b) {
    char c = op[0];
    if (op == "<<") {
        if (b < 0) throw ExprError("negative shift count");
        return b < 64 ? wrap32(int64_t(uint64_t(a) << b)) : 0;
    }
    if (op == ">>") {
        if (b < 0) throw ExprError("negative shift count");
        if (a >= 0) return b < 64 ? wrap32((a & 0xFFFFFFFFLL) >> b) : 0;
        return wrap32(b < 63 ? (a >> b) : -1);
    }
    switch (c) {
        case '+': return wrap32(int64_t(uint64_t(a) + uint64_t(b)));
        case '-': return wrap32(int64_t(uint64_t(a) - uint64_t(b)));
        case '*': return wrap32(int64_t(uint64_t(a) * uint64_t(b)));
        case '/':
        case '%': {
            if (b == 0) throw ExprError("division by zero");
            int64_t aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
            int64_t q = aa / bb;
            if ((a >= 0) != (b >= 0)) q = -q;
            return c == '/' ? wrap32(q) : wrap32(int64_t(uint64_t(a) - uint64_t(q) * uint64_t(b)));
        }
        case '&': return wrap32(a & b);
        case '^': return wrap32(a ^ b);
        case '|': return wrap32(a | b);
    }
    throw ExprError(op);
}

// ---- tokenizer and parser
namespace {

enum TK { T_NONE, T_NUM, T_CHR, T_ID, T_OP };
struct Tok {
    TK k;
    std::string v;
};

bool hexd(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool alnum_ascii(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// one token at pos (Python regex alternation order: num, chr, id, op); returns length or 0
size_t match_token(const std::string& s, size_t pos, Tok& t) {
    size_t n = s.size();
    char c = s[pos];
    if (c >= '0' && c <= '9') {
        if (c == '0' && pos + 2 < n + 0 && pos + 1 < n && (s[pos + 1] == 'x' || s[pos + 1] == 'X') && pos + 2 < n &&
            hexd(s[pos + 2])) {
            size_t j = pos + 2;
            while (j < n && hexd(s[j])) j++;
            t = {T_NUM, s.substr(pos, j - pos)};
            return j - pos;
        }
        if (c == '0' && pos + 2 < n &&
            (s[pos + 1] == 'b' || s[pos + 1] == 'B' || s[pos + 1] == 'y' || s[pos + 1] == 'Y') &&
            (s[pos + 2] == '0' || s[pos + 2] == '1')) {
            size_t j = pos + 2;
            while (j < n && (s[j] == '0' || s[j] == '1')) j++;
            t = {T_NUM, s.substr(pos, j - pos)};
            return j - pos;
        }
        size_t j = pos + 1;
        while (j < n && (alnum_ascii(s[j]) || s[j] == '_')) j++;
        t = {T_NUM, s.substr(pos, j - pos)};
        return j - pos;
    }
    if (c == '\'') {
        size_t j = pos + 1;
        while (j < n) {
            if (s[j] == '\'') {
                t = {T_CHR, s.substr(pos, j + 1 - pos)};
                return j + 1 - pos;
            }
            if (s[j] == '\\') {
                if (j + 1 >= n || s[j + 1] == '\n') break;
                j += 2;
                continue;
            }
            j++;
        }
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.') {
        size_t j = pos + 1;
        while (j < n && (alnum_ascii(s[j]) || s[j] == '_' || s[j] == '.' || s[j] == '\'')) j++;
        t = {T_ID, s.substr(pos, j - pos)};
        return j - pos;
    }
    if (pos + 1 < n && ((c == '<' && s[pos + 1] == '<') || (c == '>' && s[pos + 1] == '>'))) {
        t = {T_OP, s.substr(pos, 2)};
        return 2;
    }
    if (std::string("-+*/%&|^~()$:").find(c) != std::string::npos) {
        t = {T_OP, std::string(1, c)};
        return 1;
    }
    return 0;
}

int bin_level(const std::string& v) {
    if (v == "*" || v == "/" || v == "%") return 2;
    if (v == "+" || v == "-") return 3;
    if (v == ">>" || v == "<<") return 4;
    if (v == "&") return 5;
    if (v == "^") return 6;
    if (v == "|") return 7;
    return -1;
}

struct P {
    std::vector<Tok> toks;
    size_t i = 0;

    explicit P(const std::string& text) {
        size_t pos = 0, n = text.size();
        while (pos < n) {
            if (py_space(text[pos])) {
                pos++;
                continue;
            }
            Tok t;
            size_t m = match_token(text, pos, t);
            if (!m) throw ExprError("unexpected " + py_repr(text.substr(pos)));
            pos += m;
            toks.push_back(t);
        }
    }
    Tok peek() const { return i < toks.size() ? toks[i] : Tok{T_NONE, ""}; }
    Tok take() {
        Tok t = peek();
        i++;
        return t;
    }
    bool is_op(const Tok& t, const char* v) const { return t.k == T_OP && t.v == v; }

    ExprP mk_bin(const std::string& op, ExprP a, ExprP b) {
        if (a->is_const() && b->is_const()) return num_expr(apply_op(op, a->v, b->v));
        auto e = std::make_shared<Expr>();
        e->kind = Expr::Bin;
        e->op = op;
        e->a = a;
        e->b = b;
        return e;
    }

    ExprP expr(int level = 8) {
        if (level == 8) {
            ExprP a = expr(7);
            while (is_op(peek(), ":")) {
                take();
                ExprP b = expr(7);
                if (!b->is_const() || b->v < 1 || b->v > 32)
                    throw ExprError("the width after ':' must be a constant 1..32");
                auto e = std::make_shared<Expr>();
                e->kind = Expr::Width;
                e->a = a;
                e->v = b->v;
                a = e;
            }
            return a;
        }
        if (level == 1) return unary();
        ExprP a = expr(level - 1);
        while (true) {
            Tok t = peek();
            if (t.k == T_OP && bin_level(t.v) == level) {
                take();
                ExprP b = expr(level - 1);
                a = mk_bin(t.v, a, b);
            } else {
                return a;
            }
        }
    }

    ExprP unary() {
        Tok t = take();
        if (t.k == T_OP && (t.v == "-" || t.v == "~" || t.v == "+")) {
            ExprP a = unary();
            if (t.v == "+") return a;
            if (a->is_const()) return num_expr(t.v == "-" ? wrap32(-a->v) : wrap32(~a->v));
            auto e = std::make_shared<Expr>();
            e->kind = t.v == "-" ? Expr::Neg : Expr::Not;
            e->a = a;
            return e;
        }
        if (is_op(t, "(")) {
            ExprP a = expr();
            if (!is_op(take(), ")")) throw ExprError("unbalanced parentheses");
            return a;
        }
        if (is_op(t, "$")) {
            auto e = std::make_shared<Expr>();
            e->kind = Expr::Dollar;
            return e;
        }
        if (t.k == T_NUM) return num_expr(wrap32(parse_number(t.v)));
        if (t.k == T_CHR) return num_expr(wrap32(char_value(t.v.substr(1, t.v.size() - 2))));
        if (t.k == T_ID) {
            std::string lv = lower(t.v);
            if ((lv == "sizeof" || lv == "startof") && is_op(peek(), "(")) {
                take();
                Tok nm = take();
                if (nm.k != T_ID || !is_op(take(), ")")) throw ExprError(t.v + "() takes a section name");
                auto e = std::make_shared<Expr>();
                e->kind = Expr::Func;
                e->op = lv;
                e->name = nm.v;
                return e;
            }
            auto e = std::make_shared<Expr>();
            e->kind = Expr::Sym;
            e->name = t.v;
            return e;
        }
        if (t.k == T_NONE) throw ExprError("expression expected");
        throw ExprError("unexpected " + py_repr(t.v));
    }
};

}  // namespace

ExprP parse_expr(const std::string& text) {
    P p(text);
    if (p.toks.empty()) throw ExprError("expression expected");
    ExprP e = p.expr();
    if (p.i != p.toks.size()) throw ExprError("unexpected " + py_repr(p.toks[p.i].v));
    return e;
}

VW evaluate(const ExprP& e, const LookupFn& lookup, const HereFn& here, const FuncFn* func) {
    switch (e->kind) {
        case Expr::Num: return {e->v, 0};
        case Expr::Sym: return lookup(e->name);
        case Expr::Dollar: return here();
        case Expr::Func:
            if (!func) throw ExprError(e->op + "() is not allowed here");
            return (*func)(e->op, e->name);
        case Expr::Neg: {
            VW r = evaluate(e->a, lookup, here, func);
            return {wrap32(-r.first), r.second};
        }
        case Expr::Not: {
            VW r = evaluate(e->a, lookup, here, func);
            return {wrap32(~r.first), r.second};
        }
        case Expr::Width: {
            VW r = evaluate(e->a, lookup, here, func);
            int n = int(e->v);
            int64_t mask = n >= 63 ? -1 : (int64_t(1) << n) - 1;
            return {r.first & mask, n};
        }
        case Expr::Bin: {
            VW a = evaluate(e->a, lookup, here, func);
            VW b = evaluate(e->b, lookup, here, func);
            return {apply_op(e->op, a.first, b.first), std::max(a.second, b.second)};
        }
    }
    return {0, 0};
}

}  // namespace unitron
