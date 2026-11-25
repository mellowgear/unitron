#include "operands.hpp"

#include "regs.hpp"

namespace unitron {

RegOp make_reg(const std::string& u) {
    RegOp r;
    auto& info = REG().at(u);
    r.name = u;
    r.size = info.size;
    r.code = info.code;
    r.shrt = short_code(info.size, u);
    return r;
}

bool reg_of(const std::string& text, RegOp& out) {
    std::string u = upper(text);
    if (!REG().count(u)) return false;
    out = make_reg(u);
    return true;
}

static int parse_step(const std::string& s0) {
    std::string s = strip(s0);
    if (s.empty()) return 0;
    if (s != "1" && s != "2" && s != "4") throw OperandError("step must be 1, 2 or 4, not " + py_repr(s));
    return s[0] - '0';
}

static Operand mem(Operand::Mode mode, bool bare) {
    Operand o;
    o.kind = Operand::Mem;
    o.mode = mode;
    o.bare = bare;
    return o;
}

static ExprP parse_ex(const std::string& t) {
    try {
        return parse_expr(t);
    } catch (ExprError&) {
        throw;
    }
}

static Operand mem_body(const std::string& body, bool bare) {
    std::string b = strip(body);
    if (b.empty()) throw OperandError("empty memory operand");
    if (b[0] == '-') {
        std::string rest = strip(b.substr(1));
        size_t k = rest.find(':');
        std::string name = k == std::string::npos ? rest : rest.substr(0, k);
        std::string step = k == std::string::npos ? "" : rest.substr(k + 1);
        RegOp r;
        if (reg_of(strip(name), r)) {
            Operand o = mem(Operand::Pre, bare);
            o.reg = r;
            o.has_reg = true;
            o.step = parse_step(step);
            return o;
        }
    }
    size_t i = 0;
    while (i < b.size() && (py_alnum(b[i]) || b[i] == '_' || b[i] == '\'')) i++;
    std::string head = b.substr(0, i), tail = strip(b.substr(i));
    RegOp r;
    if (head.empty() || !reg_of(head, r)) {
        Operand o = mem(Operand::Abs, bare);
        o.addr = parse_ex(b);
        return o;
    }
    Operand o = mem(Operand::Ind, bare);
    o.reg = r;
    o.has_reg = true;
    if (tail.empty()) return o;
    if (tail == "-") {
        o.mode = Operand::Dec;
        return o;
    }
    std::string t1 = strip(tail.substr(1));
    if (tail[0] == '+' && (t1.empty() || t1[0] == ':')) {
        o.mode = Operand::Post;
        o.step = tail.find(':') != std::string::npos ? parse_step(t1.substr(1)) : 0;
        return o;
    }
    if (tail[0] == '+' || tail[0] == '-') {
        RegOp x;
        if (reg_of(t1, x)) {
            if (tail[0] != '+' || x.size == 4) throw OperandError("bad index register in " + py_repr(body));
            o.mode = Operand::Idx;
            o.idx = x;
            return o;
        }
        o.mode = Operand::Disp;
        o.disp = parse_ex(tail);
        return o;
    }
    throw OperandError("bad memory operand " + py_repr(body));
}

static bool balanced(const std::string& s) {
    int d = 0;
    for (char c : s) {
        d += c == '(';
        d -= c == ')';
        if (d < 0) return false;
    }
    return d == 0;
}

Operand parse_operand(const std::string& text, bool bare_addr, bool cond) {
    std::string t = strip(text);
    if (t.empty()) throw OperandError("missing operand");
    std::string u = upper(t);
    if (cond) {
        int c = cc_code(u);
        if (c >= 0) {
            Operand o;
            o.kind = Operand::Cond;
            o.name = u;
            o.code = c;
            return o;
        }
        throw OperandError(py_repr(t) + " is not a condition");
    }
    if (REG().count(u) && !bare_addr) {
        Operand o;
        o.kind = Operand::Reg;
        o.reg = make_reg(u);
        return o;
    }
    if (u == "F" || u == "F'" || u == "SR") {
        Operand o;
        o.kind = Operand::Special;
        o.name = u;
        return o;
    }
    auto it = CR_CODE().find(u);
    if (it != CR_CODE().end()) {
        Operand o;
        o.kind = Operand::Ctl;
        o.name = u;
        o.code = it->second.first;
        o.size = it->second.second;
        return o;
    }
    bool paren = t.size() >= 2 && t.front() == '(' && t.back() == ')' && balanced(t.substr(1, t.size() - 2));
    if (t.size() == 1) paren = false;
    if (bare_addr) {
        if (paren)
            throw OperandError("write " + strip(t.substr(1, t.size() - 2)) +
                               " without parentheses (asm900 JP/CALL/LDA syntax)");
        return mem_body(t, true);
    }
    if (paren) return mem_body(t.substr(1, t.size() - 2), false);
    Operand o;
    o.kind = Operand::Imm;
    o.expr = parse_ex(t);
    return o;
}

std::vector<std::string> split_operands(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    char q = 0;
    bool esc = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (q) {
            cur += c;
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == q) q = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            char prev = i ? text[i - 1] : 0;
            if (!(c == '\'' && prev && (py_alnum(prev) || prev == '_' || prev == '.'))) q = c;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        } else if (c == ',' && depth == 0) {
            out.push_back(strip(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    std::string last = strip(cur);
    if (!last.empty() || !out.empty()) out.push_back(last);
    return out;
}

}  // namespace unitron
