// Port of legacy/python/unitron/lift/lift.py. Byte-identical output with the Python reference, including the
// text of UNIMPLEMENTED(...) lines: the reference catches any exception raised while lifting one
// instruction and prints `type(e).__name__: e`, so the Python errors (KeyError, TypeError,
// ValueError, IndexError, NotImplementedError) are reproduced as PyErr with the same messages,
// raised at the same points (side effects made before the error are kept, as in the reference).
#include "lift.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "embedded.hpp"
#include "hwnames.hpp"
#include "rom.hpp"
#include "trace.hpp"
#include "util.hpp"

namespace unitron {
namespace {

using Lines = std::vector<std::string>;
using Fn = std::function<std::string(const std::string&)>;

// A Python exception raised while lifting one instruction.
struct PyErr {
    std::string type, msg;
};
[[noreturn]] void raise(const char* type, const std::string& msg) { throw PyErr{type, msg}; }

constexpr int NONE = -1;   // Python None for an operand size

void append(Lines& a, const Lines& b) { a.insert(a.end(), b.begin(), b.end()); }
Lines cat(Lines a, const Lines& b) {
    append(a, b);
    return a;
}

// ---------------------------------------------------------------- registers
struct RegE {
    std::string expr;
    int size;
};

std::map<std::string, RegE> reg_table() {
    std::map<std::string, RegE> t;
    auto part = [](const std::string& base, int size, int off) {
        if (size == 4) return base;
        return "R" + dec(size * 8) + "(" + base + "," + dec(off / size) + ")";
    };
    const char* pairs[4] = {"WA", "BC", "DE", "HL"};
    const char* los[4] = {"A", "C", "E", "L"};
    const char* his[4] = {"W", "B", "D", "H"};
    for (int i = 0; i < 4; i++) {
        std::string p = pairs[i], lo = los[i], hi = his[i];
        std::vector<std::pair<std::string, std::string>> banks = {
            {"", "cpu.b[cpu.rfp][" + dec(i) + "]"}, {"_P", "cpu.b[(cpu.rfp-1)&3][" + dec(i) + "]"}};
        for (int n = 0; n < 4; n++) banks.push_back({dec(n), "cpu.b[" + dec(n) + "][" + dec(i) + "]"});
        for (auto& [tag, base] : banks) {
            bool numeric = !tag.empty() && py_digit(tag[0]);
            std::string pre = numeric ? "R" : "";
            t["X" + p + tag] = {base, 4};
            t[(numeric ? pre + p : p) + tag] = {part(base, 2, 0), 2};
            t["Q" + p + tag] = {part(base, 2, 2), 2};
            t[pre + lo + tag] = {part(base, 1, 0), 1};
            t[pre + hi + tag] = {part(base, 1, 1), 1};
            t["Q" + lo + tag] = {part(base, 1, 2), 1};
            t["Q" + hi + tag] = {part(base, 1, 3), 1};
        }
    }
    const char* idx[4] = {"IX", "IY", "IZ", "SP"};
    for (int i = 0; i < 4; i++) {
        std::string r = idx[i], base = "cpu.x[" + dec(i) + "]";
        t["X" + r] = {base, 4};
        t[r] = {part(base, 2, 0), 2};
        t["Q" + r] = {part(base, 2, 2), 2};
        t[r + "L"] = {part(base, 1, 0), 1};
        t[r + "H"] = {part(base, 1, 1), 1};
        t["Q" + r + "L"] = {part(base, 1, 2), 1};
        t["Q" + r + "H"] = {part(base, 1, 3), 1};
    }
    return t;
}

const std::map<std::string, RegE>& REGS() {
    static const std::map<std::string, RegE> t = reg_table();
    return t;
}

const std::map<int64_t, std::string>& IO() {
    static const std::map<int64_t, std::string> m = [] {
        std::map<int64_t, std::string> r;
        for (auto& h : hw_names())
            if (h.kind != "vector") r[h.addr] = h.name;
        return r;
    }();
    return m;
}

std::string cname(const std::string& r) { return replace_all(r, "'", "_P"); }

const RegE* reg(const std::string& name) {
    auto it = REGS().find(cname(name));
    return it == REGS().end() ? nullptr : &it->second;
}
// reg(name)[1]
int regsz(const std::string& name) {
    const RegE* r = reg(name);
    if (!r) raise("TypeError", "'NoneType' object is not subscriptable");
    return r->size;
}

// ------------------------------------------------------------ Python-isms
std::string size_key(int size) { return size == NONE ? "None" : dec(size); }
std::string U(int size) {
    if (size == 1) return "uint8_t";
    if (size == 2) return "uint16_t";
    if (size == 4) return "uint32_t";
    raise("KeyError", size_key(size));
}
std::string S(int size) {
    if (size == 1) return "int8_t";
    if (size == 2) return "int16_t";
    if (size == 4) return "int32_t";
    raise("KeyError", size_key(size));
}
// f"{size * 8}"
std::string m8(int size) {
    if (size == NONE) raise("TypeError", "unsupported operand type(s) for *: 'NoneType' and 'int'");
    return dec(int64_t(size) * 8);
}
bool truthy(int size) { return size != NONE && size != 0; }

// ops[i] (negative i counts from the end)
const std::string& at(const std::vector<Op>& ops, int i) {
    int n = int(ops.size());
    int j = i < 0 ? n + i : i;
    if (j < 0 || j >= n) raise("IndexError", "list index out of range");
    return ops[size_t(j)].text;
}
// a, b = ops
std::pair<std::string, std::string> unpack2(const std::vector<Op>& ops) {
    if (ops.size() < 2)
        raise("ValueError", "not enough values to unpack (expected 2, got " + dec(int64_t(ops.size())) + ")");
    if (ops.size() > 2) raise("ValueError", "too many values to unpack (expected 2)");
    return {ops[0].text, ops[1].text};
}
std::pair<std::string, std::string> unpack2(const std::vector<std::string>& v) {
    if (v.size() < 2)
        raise("ValueError", "not enough values to unpack (expected 2, got " + dec(int64_t(v.size())) + ")");
    if (v.size() > 2) raise("ValueError", "too many values to unpack (expected 2)");
    return {v[0], v[1]};
}
bool ops_eq(const std::vector<Op>& ops, std::initializer_list<const char*> v) {
    if (ops.size() != v.size()) return false;
    size_t i = 0;
    for (const char* s : v)
        if (ops[i++].text != s) return false;
    return true;
}
// Python s[a:b] for small non-negative a and negative b
std::string pyslice(const std::string& s, int64_t a, int64_t b) {
    int64_t n = int64_t(s.size());
    if (a < 0) a = std::max<int64_t>(0, n + a);
    if (b < 0) b = std::max<int64_t>(0, n + b);
    a = std::min(a, n);
    b = std::min(b, n);
    return b > a ? s.substr(size_t(a), size_t(b - a)) : std::string();
}
bool in(const std::string& s, std::initializer_list<const char*> v) {
    for (const char* x : v)
        if (s == x) return true;
    return false;
}

// ------------------------------------------------------------ operand parse
// ^-?(0x[0-9A-Fa-f]+|\d+)$
bool is_num(const std::string& s0) {
    std::string s = s0;
    if (!s.empty() && s.back() == '\n') s.pop_back();   // `$` also matches before a final newline
    size_t i = 0;
    if (i < s.size() && s[i] == '-') i++;
    if (s.size() - i >= 3 && s[i] == '0' && s[i + 1] == 'x') {
        bool ok = true;
        for (size_t j = i + 2; j < s.size(); j++)
            if (!isxdigit((unsigned char)s[j])) ok = false;
        if (ok) return true;
    }
    if (i >= s.size()) return false;
    for (size_t j = i; j < s.size(); j++)
        if (!py_digit((unsigned char)s[j])) return false;
    return true;
}
int64_t num(const std::string& s) {
    try {
        return py_int0(s);
    } catch (const std::exception&) {
        raise("ValueError", "invalid literal for int() with base 0: " + py_repr(s));
    }
}
int64_t int10(const std::string& s) {
    try {
        return py_int10(s);
    } catch (const std::exception&) {
        raise("ValueError", "invalid literal for int() with base 10: " + py_repr(s));
    }
}

bool is_mem(const std::string& op) { return starts_with(op, "("); }

std::string sym_addr(int64_t a) {
    auto it = IO().find(a);
    if (it != IO().end()) return it->second;
    auto v = VECTORS().find(a);
    if (v != VECTORS().end()) return v->second;
    return "0x" + hexs(a);
}

bool is_regchar(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\''; }

// A parsed memory operand.
struct Mem {
    std::string text, base, addr;
    int size;
    int64_t pre = 0, post = 0;   // 0 = None
    Mem(const std::string& text_, int size_) : text(text_), size(size_) {
        std::string body = starts_with(text, "(") ? pyslice(text, 1, -1) : text;
        int64_t step = 0;
        if (body.find(':') != std::string::npos) {
            auto [b, st] = unpack2(split_on(body, ":"));
            body = b;
            step = int10(st);
        }
        int64_t dflt = step ? step : (truthy(size) ? size : 1);
        if (starts_with(body, "-") && reg(body.substr(1))) {
            base = body.substr(1);
            pre = dflt;
            addr = cname(base);
        } else if (ends_with(body, "+") && reg(body.substr(0, body.size() - 1))) {
            base = body.substr(0, body.size() - 1);
            post = dflt;
            addr = cname(base);
        } else if (is_num(body)) {
            addr = sym_addr(num(body));
        } else {
            // ^([A-Z0-9']+)([+-])(.+)$
            size_t k = 0;
            while (k < body.size() && is_regchar(body[k])) k++;
            bool m = k > 0 && k < body.size() && (body[k] == '+' || body[k] == '-') && k + 1 < body.size() &&
                     body.find('\n', k + 1) == std::string::npos;
            if (m && reg(body.substr(0, k))) {
                std::string b = body.substr(0, k), sign(1, body[k]), off = body.substr(k + 1);
                if (is_num(off)) {
                    addr = cname(b) + " " + sign + " " + off;
                } else {   // (r32+r8) / (r32+r16): signed index register
                    int isz = regsz(off);
                    addr = cname(b) + " + (int" + dec(isz * 8) + "_t)" + cname(off);
                }
            } else if (reg(body)) {
                addr = cname(body);
            } else {
                raise("ValueError", "cannot parse memory operand " + py_repr(text));
            }
        }
    }
    Lines setup() const { return pre ? Lines{cname(base) + " -= " + dec(pre) + ";"} : Lines{}; }
    Lines teardown() const { return post ? Lines{cname(base) + " += " + dec(post) + ";"} : Lines{}; }
};

// Operation size in bytes from register operands or mnemonic suffix.
int opsize(const Insn& ins) {
    for (auto& o : ins.ops)
        if (!is_mem(o.text) && reg(o.text)) return reg(o.text)->size;
    const std::string& m = ins.mnem;
    char c = m.back();
    if ((c == 'B' || c == 'W' || c == 'L') && !in(m, {"SUB", "SBC", "DJNZ", "ADC", "LDCF", "STCF"}))
        return c == 'B' ? 1 : c == 'W' ? 2 : 4;
    return NONE;
}

std::string base_mnem(const std::string& m) {
    if (m.size() > 2 && (m.back() == 'B' || m.back() == 'W' || m.back() == 'L') &&
        !in(m, {"SUB", "SBC", "DJNZ", "ADC", "LDCF", "STCF", "ANDCF", "ORCF", "XORCF", "RL", "SLL", "SRL", "CPL",
                "PUSHW", "POPW", "BS1B"})) {
        std::string b = m.substr(0, m.size() - 1);
        if (in(b, {"LD", "ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP", "INC", "DEC", "PUSH", "POP",
                   "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL", "LDIR", "LDDR", "LDI", "LDD", "CPIR",
                   "CPDR", "CPI", "CPD"}))
            return b;
    }
    if (m == "PUSHW" || m == "POPW") return m.substr(0, m.size() - 1);
    return m;
}

// ------------------------------------------------------------------- flags
bool is_alu(const std::string& m) { return in(m, {"ADD", "ADC", "SUB", "SBC", "AND", "OR", "XOR", "CP"}); }
bool is_shift(const std::string& m) { return in(m, {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"}); }
bool is_full_writer(const std::string& m) { return is_alu(m) || is_shift(m) || m == "NEG" || m == "DAA"; }
bool is_reads_c(const std::string& m) {
    return in(m, {"ADC", "SBC", "RL", "RR", "CCF", "STCF", "ANDCF", "ORCF", "XORCF", "DAA"});
}

bool reads_flags(const Insn& ins) {
    std::string m = base_mnem(ins.mnem);
    if (in(m, {"JR", "JRL", "JP", "CALL", "RET"}) && ins.conditional) return true;
    if (m == "SCC" || m == "ZCF" || is_reads_c(m)) return true;
    if (m == "PUSH" && !ins.ops.empty() && in(ins.ops[0].text, {"F", "SR"})) return true;
    if (m == "EX" && ops_eq(ins.ops, {"F", "F'"})) return true;
    return false;
}

bool writes_all_flags(const Insn& ins) {
    std::string m = base_mnem(ins.mnem);
    if (is_full_writer(m) && !in(m, {"ADC", "SBC", "RL", "RR", "DAA"})) return true;
    if (m == "POP" && !ins.ops.empty() && in(ins.ops[0].text, {"F", "SR"})) return true;
    return false;
}

struct Flow {
    const InsnMap& insns;
    std::vector<int64_t> succ(const Insn& ins) const {
        std::vector<int64_t> out;
        std::string m = base_mnem(ins.mnem);
        if (in(m, {"CALL", "CALR", "RET", "RETI", "RETD"}) && !ins.conditional)
            return out;   // assume flags are dead across calls/returns (cc900)
        if (ins.has_target && !ins.is_call && insns.has(ins.target)) out.push_back(ins.target);
        if (!ins.ends_block) {
            int64_t nxt = ins.addr + ins.size;
            if (insns.has(nxt)) out.push_back(nxt);
        }
        return out;
    }
    bool flags_live_after(const Insn& ins, int depth = 10) const {
        std::set<int64_t> seen;
        std::vector<std::pair<int64_t, int>> stack;
        for (int64_t s : succ(ins)) stack.push_back({s, depth});
        if (stack.empty() && !ins.ends_block && !in(base_mnem(ins.mnem), {"CALL", "CALR"}))
            return true;   // flows into untraced code: be conservative
        while (!stack.empty()) {
            auto [a, d] = stack.back();
            stack.pop_back();
            if (!seen.insert(a).second) continue;
            const Insn& i = insns.at(a);
            if (reads_flags(i)) return true;
            if (writes_all_flags(i)) continue;
            if (d == 0) return true;
            auto nx = succ(i);
            if (nx.empty() && !i.ends_block && !in(base_mnem(i.mnem), {"CALL", "CALR"})) return true;
            for (int64_t s : nx) stack.push_back({s, d - 1});
        }
        return false;
    }
};

// --------------------------------------------------------------- the lifter
std::string cc_expr(const std::string& cc) {
    static const std::map<std::string, std::string> t = {
        {"F", "0"},
        {"T", "1"},
        {"Z", "cpu.fz"},
        {"NZ", "!cpu.fz"},
        {"C", "cpu.fc"},
        {"NC", "!cpu.fc"},
        {"MI", "cpu.fs"},
        {"PL", "!cpu.fs"},
        {"OV", "cpu.fv"},
        {"NOV", "!cpu.fv"},
        {"LT", "(cpu.fs ^ cpu.fv)"},
        {"GE", "!(cpu.fs ^ cpu.fv)"},
        {"LE", "(cpu.fz | (cpu.fs ^ cpu.fv))"},
        {"GT", "!(cpu.fz | (cpu.fs ^ cpu.fv))"},
        {"ULE", "(cpu.fc | cpu.fz)"},
        {"UGT", "!(cpu.fc | cpu.fz)"}};
    auto it = t.find(cc);
    if (it == t.end()) raise("KeyError", py_repr(cc));
    return it->second;
}

// Condition of 'CP a,b ; Jcc' as a plain C comparison, or "" (None).
std::string fused_cmp(const std::string& cc, const std::string& a, const std::string& b, int size) {
    std::string u = U(size), s = S(size);
    std::string ua = "(" + u + ")(" + a + ")", ub = "(" + u + ")(" + b + ")";
    std::string sa = "(" + s + ")(" + a + ")", sb = "(" + s + ")(" + b + ")";
    if (cc == "Z") return ua + " == " + ub;
    if (cc == "NZ") return ua + " != " + ub;
    if (cc == "C") return ua + " < " + ub;
    if (cc == "NC") return ua + " >= " + ub;
    if (cc == "ULE") return ua + " <= " + ub;
    if (cc == "UGT") return ua + " > " + ub;
    if (cc == "LT") return sa + " < " + sb;
    if (cc == "GE") return sa + " >= " + sb;
    if (cc == "LE") return sa + " <= " + sb;
    if (cc == "GT") return sa + " > " + sb;
    if (cc == "MI") return "(" + s + ")(" + ua + " - " + ub + ") < 0";
    if (cc == "PL") return "(" + s + ")(" + ua + " - " + ub + ") >= 0";
    if (cc == "T") return "1";
    if (cc == "F") return "0";
    return "";
}

std::string label(int64_t a) { return "L_" + hexs(a, 6); }

// f"{s:<60}" (width in code points)
std::string ljust(const std::string& s, size_t w) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) n++;
    return n >= w ? s : s + std::string(w - n, ' ');
}

using Funcs = std::map<int64_t, std::string>;

class Lifter {
public:
    Lifter(const InsnMap& insns, const Funcs& funcs) : insns(insns), funcs(funcs), flow{insns} {}
    const InsnMap& insns;
    const Funcs& funcs;
    Flow flow;
    int64_t unimpl = 0;
    std::set<int64_t> fused;               // addresses of branches whose condition was fused
    std::map<int64_t, std::string> fused_conds;
    Lines post_;

    // C rvalue for operand; appends setup statements to `pre`.
    std::string rd(const std::string& op, int size, Lines& pre) {
        if (is_num(op)) return op;
        if (is_mem(op)) {
            Mem m(op, size);
            append(pre, m.setup());
            append(post_, m.teardown());
            return "rd" + m8(size) + "(" + m.addr + ")";
        }
        if (reg(op)) return cname(op);
        raise("ValueError", op);
    }
    Lines wr(const std::string& op, int size, const std::string& val, Lines& pre) {
        if (is_mem(op)) {
            Mem m(op, size);
            append(pre, m.setup());
            append(post_, m.teardown());
            return {"wr" + m8(size) + "(" + m.addr + ", " + val + ");"};
        }
        return {cname(op) + " = " + val + ";"};
    }
    // Read-modify-write: fn(rvalue) -> C expression.
    Lines rmw(const std::string& op, int size, const Fn& fn) {
        if (is_mem(op)) {
            Mem m(op, size);
            Lines out = m.setup();
            std::string s = "{ uint32_t ea = " + m.addr + "; wr";
            s += m8(size);
            std::string inner = "rd" + m8(size) + "(ea)";
            s += "(ea, " + fn(inner) + "); }";
            out.push_back(s);
            return cat(out, m.teardown());
        }
        std::string r = cname(op);
        return {r + " = " + fn(r) + ";"};
    }

    Lines call(int64_t target, int64_t ret_addr) const {
        auto it = funcs.find(target);
        if (it != funcs.end()) return {"push32(0x" + hexs(ret_addr, 6) + "); " + it->second + "();"};
        return {"push32(0x" + hexs(ret_addr, 6) + "); call_ind(0x" + hexs(target, 6) + ");"};
    }

    Lines lift(const Insn& ins, int64_t fentry) {
        post_.clear();
        Lines out;
        try {
            out = lift_(ins, fentry);
        } catch (const PyErr& e) {
            unimpl++;
            out = {"UNIMPLEMENTED(\"" + ins.text() + "\");  /* " + e.type + ": " + e.msg + " */"};
        }
        return cat(out, post_);
    }

    std::string branch_cond(const std::string& cc, const Insn& ins) const {
        if (fused.count(ins.addr)) {
            auto it = fused_conds.find(ins.addr);
            if (it == fused_conds.end()) raise("KeyError", dec(ins.addr));
            return it->second;
        }
        return cc_expr(cc);
    }

    std::string goto_or_tail(int64_t target, int64_t fentry) const {
        auto it = funcs.find(target);
        if (it != funcs.end() && target != fentry) return "{ " + it->second + "(); return; }";   // tail call
        return "goto " + label(target) + ";";
    }

    Lines lift_(const Insn& ins, int64_t fentry);

    Lines function(int64_t entry, const std::string& name);
};

Lines Lifter::lift_(const Insn& ins, int64_t fentry) {
    const std::string m = base_mnem(ins.mnem);
    const std::vector<Op>& ops = ins.ops;
    const int64_t nxt = ins.addr + ins.size;
    const int sz = opsize(ins);
    const bool live =
        (is_full_writer(m) || in(m, {"INC", "DEC", "BIT", "CPL", "NEG"})) ? flow.flags_live_after(ins) : false;
    Lines pre;
    const Insn* n = insns.get(nxt);

    // ---------- control flow
    if (in(m, {"JR", "JRL", "JP"})) {
        std::string cc = ops.size() == 2 ? ops[0].text : "T";
        std::string tgt = at(ops, -1);
        std::string stmt;
        if (ins.has_target)
            stmt = goto_or_tail(ins.target, fentry);
        else   // JP mem: jump to the effective address (not its contents)
            stmt = "{ jump_ind(" + Mem(tgt, 4).addr + "); return; }";
        if (cc == "T") return cat(pre, {stmt});
        return cat(pre, {"if (" + branch_cond(cc, ins) + ") " + stmt});
    }
    if (m == "CALL" || m == "CALR") {
        std::string cc = ops.size() == 2 ? ops[0].text : "T";
        std::string tgt = at(ops, -1);
        Lines body;
        if (ins.has_target)
            body = call(ins.target, nxt);
        else
            body = {"push32(0x" + hexs(nxt, 6) + "); call_ind(" + Mem(tgt, 4).addr + ");"};
        if (cc == "T") return body;
        return {"if (" + branch_cond(cc, ins) + ") { " + join(body, " ") + " }"};
    }
    if (m == "RET") {
        if (!ops.empty()) return {"if (" + branch_cond(ops[0].text, ins) + ") { XSP += 4; return; }"};
        return {"XSP += 4; return;"};
    }
    if (m == "RETD") return {"XSP += 4 + (" + at(ops, 0) + "); return;"};
    if (m == "RETI") return {"reti(); return;"};
    if (m == "DJNZ") {
        std::string r = cname(at(ops, 0));
        if (!ins.has_target) raise("TypeError", "unsupported format string passed to NoneType.__format__");
        return {"if (--" + r + " != 0) " + goto_or_tail(ins.target, fentry)};
    }
    if (m == "SWI") {
        if (ops_eq(ops, {"1"})) {
            const std::string& c = ins.comment;
            return {!c.empty() ? "bios_call();  /* " + c + " */" : "bios_call();"};
        }
        return {"swi(" + at(ops, 0) + ");"};
    }

    // ---------- loads / moves
    if (m == "LD") {
        auto [d, s] = unpack2(ops);
        int size = sz;
        if (!truthy(size)) {
            const RegE* r = reg(d);
            if (!r) r = reg(s);
            size = r ? r->size : 1;
        }
        if (ins.mnem == "LDB" || ins.mnem == "LDW") {
            if (is_mem(s) && is_mem(d)) size = ins.mnem == "LDB" ? 1 : 2;
        }
        if (in(ins.mnem, {"LDB", "LDW", "LDL"})) {
            char c = ins.mnem.back();
            size = c == 'B' ? 1 : c == 'W' ? 2 : 4;
        }
        std::string v = rd(s, size, pre);
        Lines w = wr(d, size, v, pre);
        return cat(pre, w);
    }
    if (m == "LDX") {
        std::string a = Mem(at(ops, 0), 1).addr;
        return {"wr8(" + a + ", " + at(ops, 1) + ");"};
    }
    if (m == "LDA" || m == "LDAR") {
        auto [d, s] = unpack2(ops);
        std::string addr = !is_num(s) ? Mem("(" + s + ")", 4).addr : s;
        std::string cd = cname(d);
        std::string u = U(regsz(d));
        return {cd + " = (" + u + ")(" + addr + ");"};
    }
    if (m == "EX") {
        if (ops_eq(ops, {"F", "F'"})) return {"ex_f();"};
        auto [a, b] = unpack2(ops);
        int size = sz;
        std::string va = rd(a, size, pre);
        std::string vb = rd(b, size, pre);
        Lines head = pre;   // `pre + [...]` is built before the writes extend `pre`
        head.push_back("{ " + U(size) + " t = " + va + ";");
        append(head, wr(a, size, vb, pre));
        append(head, wr(b, size, "t", pre));
        head.push_back("}");
        return head;
    }
    if (m == "PUSH") {
        std::string o = at(ops, 0);
        if (o == "SR") return {"push16(get_sr());"};
        if (o == "F") return {"push8(get_f());"};
        if (o == "A") return {"push8(A);"};
        if (is_num(o)) {
            int size = ins.mnem == "PUSHW" ? 2 : 1;
            return {"push" + m8(size) + "(" + o + ");"};
        }
        int size = truthy(sz) ? sz : (ins.mnem == "PUSHW" ? 2 : 1);
        std::string v = rd(o, size, pre);
        return cat(pre, {"push" + m8(size) + "(" + v + ");"});
    }
    if (m == "POP") {
        std::string o = at(ops, 0);
        if (o == "SR") return {"set_sr(pop16());"};
        if (o == "F") return {"set_f(pop8());"};
        int size = truthy(sz) ? sz : (ins.mnem == "POPW" ? 2 : 1);
        return wr(o, size, "pop" + m8(size) + "()", pre);
    }
    if (in(m, {"LDIR", "LDDR", "LDI", "LDD"})) {
        int size = ends_with(ins.mnem, "W") ? 2 : 1;
        std::string dst = pyslice(at(ops, 0), 1, -2);
        std::string src = pyslice(at(ops, 1), 1, -2);
        std::string step = std::string(in(m, {"LDIR", "LDI"}) ? "+" : "-") + "= " + dec(size);
        std::string s8 = dec(size * 8);
        std::string body =
            "wr" + s8 + "(" + dst + ", rd" + s8 + "(" + src + ")); " + dst + " " + step + "; " + src + " " + step + "; BC--;";
        if (m == "LDIR" || m == "LDDR")
            return {"do { " + body + " } while (BC != 0);", "cpu.fh = cpu.fn = 0; cpu.fv = 0;"};
        return {body, "cpu.fh = cpu.fn = 0; cpu.fv = (BC != 0);"};
    }
    if (in(m, {"CPIR", "CPDR", "CPI", "CPD"})) {
        int size = ends_with(ins.mnem, "W") ? 2 : 1;
        std::string acc = cname(at(ops, 0));
        std::string ptr = pyslice(at(ops, 1), 1, -2);
        std::string step = std::string(in(m, {"CPIR", "CPI"}) ? "+" : "-") + "= " + dec(size);
        std::string s8 = dec(size * 8);
        std::string one = "alu_cp(" + acc + ", rd" + s8 + "(" + ptr + "), " + s8 + "); " + ptr + " " + step +
                          "; BC--; cpu.fv = (BC != 0);";
        if (m == "CPIR" || m == "CPDR") return {"do { " + one + " } while (BC != 0 && !cpu.fz);"};
        return {one};
    }

    // ---------- ALU
    if (is_alu(m)) {
        auto [d, s] = unpack2(ops);
        int size = sz;
        std::string vs = rd(s, size, pre);
        if (m == "CP") {
            std::string vd = rd(d, size, pre);
            // Fuse with a following conditional branch when flags die there.
            if (n && n->conditional && in(base_mnem(n->mnem), {"JR", "JRL", "JP", "RET", "CALL"}) &&
                !flow.flags_live_after(*n)) {
                std::string cond = fused_cmp(at(n->ops, 0), vd, vs, size);
                if (!cond.empty()) {
                    fused.insert(nxt);
                    fused_conds[nxt] = cond;
                    return cat(pre, {"/* CP " + at(ops, 0) + "," + at(ops, 1) + " */"});
                }
            }
            if (!live) return cat(pre, {"/* CP: flags unused */"});
            return cat(pre, {"alu_cp(" + vd + ", " + vs + ", " + m8(size) + ");"});
        }
        std::string op_c = m == "ADD" ? "+" : m == "SUB" ? "-" : m == "AND" ? "&" : m == "OR" ? "|" : m == "XOR" ? "^" : "";
        bool fuse_zero = in(m, {"AND", "OR", "XOR"}) && n && n->conditional &&
                         in(base_mnem(n->mnem), {"JR", "JRL", "JP", "RET"}) && in(at(n->ops, 0), {"Z", "NZ"}) &&
                         !flow.flags_live_after(*n);
        std::string ml = lower(m);
        Fn alu = [&](const std::string& x) { return "alu_" + ml + "(" + x + ", " + vs + ", " + m8(size) + ")"; };
        if ((!live || fuse_zero) && !op_c.empty()) {
            if (fuse_zero) {
                fused.insert(nxt);
                std::string dv = is_mem(d) ? "" : cname(d);
                if (!dv.empty()) {
                    std::string u = U(size);
                    fused_conds[nxt] = "(" + u + ")" + dv + " " + (at(n->ops, 0) == "Z" ? "==" : "!=") + " 0";
                } else {
                    fused.erase(nxt);
                    return rmw(d, size, alu);
                }
            }
            if (!is_mem(d) && d == s && (m == "AND" || m == "OR")) return cat(pre, {"/* " + m + " " + d + "," + d + ": test */"});
            return cat(pre, rmw(d, size, [&](const std::string& x) { return "(" + U(size) + ")(" + x + " " + op_c + " " + vs + ")"; }));
        }
        return cat(pre, rmw(d, size, alu));
    }
    if (m == "INC" || m == "DEC") {
        auto [n_, d] = unpack2(ops);
        int size = sz;
        bool flags = size == 1 || is_mem(d);
        std::string op_c = m == "INC" ? "+" : "-";
        std::string ml = lower(m);
        if (!flags || !live)
            return rmw(d, size, [&](const std::string& x) { return "(" + U(size) + ")(" + x + " " + op_c + " " + n_ + ")"; });
        return rmw(d, size, [&](const std::string& x) { return "alu_" + ml + "(" + x + ", " + n_ + ", " + m8(size) + ")"; });
    }
    if (m == "NEG") {
        int size = sz;
        std::string o = at(ops, 0);
        if (live) return rmw(o, size, [&](const std::string& x) { return "alu_sub(0, " + x + ", " + m8(size) + ")"; });
        return rmw(o, size, [&](const std::string& x) { return "(" + U(size) + ")(-" + x + ")"; });
    }
    if (m == "CPL") {
        int size = sz;
        Lines r = rmw(at(ops, 0), size, [&](const std::string& x) { return "(" + U(size) + ")~" + x; });
        r.push_back("cpu.fh = cpu.fn = 1;");
        return r;
    }
    if (is_shift(m)) {
        std::string cnt, d;
        if (ops.size() == 1) {
            cnt = "1";
            d = ops[0].text;
        } else {
            std::tie(cnt, d) = unpack2(ops);
            if (cnt == "A") cnt = "((A & 15) ? (A & 15) : 16)";
        }
        int size = truthy(sz) ? sz : 1;
        if (!live && in(m, {"SLL", "SLA", "SRL", "SRA"}) && is_num(cnt)) {
            std::string c = dec(int10(cnt));
            Fn f;
            if (m == "SLL" || m == "SLA")
                f = [&](const std::string& x) { return "(" + U(size) + ")(" + x + " << " + c + ")"; };
            else if (m == "SRL")
                f = [&](const std::string& x) { return "(" + U(size) + ")(" + x + " >> " + c + ")"; };
            else
                f = [&](const std::string& x) { return "(" + U(size) + ")((" + S(size) + ")" + x + " >> " + c + ")"; };
            return rmw(d, size, f);
        }
        std::string ml = lower(m);
        return rmw(d, size, [&](const std::string& x) { return "shift_" + ml + "(" + x + ", " + cnt + ", " + m8(size) + ")"; });
    }
    if (m == "BIT") {
        auto [b, d] = unpack2(ops);
        int size = truthy(sz) ? sz : 1;
        std::string v = rd(d, size, pre);
        if (n && n->conditional && !n->ops.empty() && in(n->ops[0].text, {"Z", "NZ"}) && !flow.flags_live_after(*n)) {
            fused.insert(nxt);
            std::string t = "((" + v + " >> " + b + ") & 1)";
            fused_conds[nxt] = n->ops[0].text == "Z" ? "!" + t : t;
            return cat(pre, {"/* BIT " + b + "," + d + " */"});
        }
        return cat(pre, {"cpu.fz = !((" + v + " >> " + b + ") & 1); cpu.fh = 1; cpu.fn = 0;"});
    }
    if (in(m, {"SET", "RES", "CHG", "TSET"})) {
        auto [b, d] = unpack2(ops);
        int size = truthy(sz) ? sz : 1;
        Fn f;
        if (m == "SET") f = [&](const std::string& x) { return "(" + U(size) + ")(" + x + " | (1u << " + b + "))"; };
        else if (m == "RES") f = [&](const std::string& x) { return "(" + U(size) + ")(" + x + " & ~(1u << " + b + "))"; };
        else if (m == "CHG") f = [&](const std::string& x) { return "(" + U(size) + ")(" + x + " ^ (1u << " + b + "))"; };
        else f = [&](const std::string& x) { return "tset(" + x + ", " + b + ")"; };
        return rmw(d, size, f);
    }
    if (in(m, {"ANDCF", "ORCF", "XORCF", "LDCF", "STCF"})) {
        auto [b, d] = unpack2(ops);
        int size = truthy(sz) ? sz : 1;
        std::string bit = b == "A" ? "(A & 15)" : b;
        if (m == "STCF")
            return rmw(d, size, [&](const std::string& x) {
                return "(" + U(size) + ")((" + x + " & ~(1u << " + bit + ")) | ((uint32_t)cpu.fc << " + bit + "))";
            });
        std::string v = rd(d, size, pre);
        std::string e = "((" + v + " >> " + bit + ") & 1)";
        std::string op = m == "ANDCF" ? "&=" : m == "ORCF" ? "|=" : m == "XORCF" ? "^=" : "=";
        return cat(pre, {"cpu.fc " + op + " " + e + ";"});
    }
    if (m == "RCF") return {"cpu.fc = 0; cpu.fh = cpu.fn = 0;"};
    if (m == "SCF") return {"cpu.fc = 1; cpu.fh = cpu.fn = 0;"};
    if (m == "CCF") return {"cpu.fh = cpu.fc; cpu.fc = !cpu.fc; cpu.fn = 0;"};
    if (m == "ZCF") return {"cpu.fc = !cpu.fz; cpu.fn = 0;"};
    if (m == "SCC") {
        auto [cc, d] = unpack2(ops);
        std::string cd = cname(d);
        return {cd + " = " + cc_expr(cc) + " ? 1 : 0;"};
    }
    if (in(m, {"MUL", "MULS", "DIV", "DIVS"})) {
        auto [rr, s] = unpack2(ops);
        int big = regsz(rr);
        int half = big / 2;
        std::string vs = rd(s, half, pre);
        std::string fn = m == "MUL" ? "mulu" : m == "MULS" ? "muls" : m == "DIV" ? "divu" : "divs";
        return cat(pre, {cname(rr) + " = " + fn + dec(half * 8) + "(" + cname(rr) + ", " + vs + ");"});
    }
    if (m == "EXTZ" || m == "EXTS") {
        std::string r = cname(at(ops, 0));
        int size = regsz(ops[0].text);
        int h = size / 2;
        std::string us = U(size);
        if (m == "EXTZ") return {r + " = (" + us + ")(" + U(h) + ")" + r + ";"};
        return {r + " = (" + us + ")(" + S(h) + ")" + r + ";"};
    }
    if (m == "PAA") {
        std::string r = cname(at(ops, 0));
        return {"if (" + r + " & 1) " + r + "++;"};
    }
    if (m == "MIRR") {
        std::string r = cname(at(ops, 0));
        return {r + " = mirr16(" + r + ");"};
    }
    if (m == "DAA") {
        std::string r = cname(at(ops, 0));
        return {r + " = daa(" + r + ");"};
    }
    if (in(m, {"MINC1", "MINC2", "MINC4", "MDEC1", "MDEC2", "MDEC4"})) {
        auto [n_, d] = unpack2(ops);
        std::string k(1, m.back());
        std::string r = cname(d);
        if (starts_with(m, "MINC"))
            return {r + " = ((" + r + " % " + n_ + ") == " + n_ + " - " + k + ") ? (" + r + " - (" + n_ + " - " + k +
                    ")) : (" + r + " + " + k + ");"};
        return {r + " = ((" + r + " % " + n_ + ") == 0) ? (" + r + " + (" + n_ + " - " + k + ")) : (" + r + " - " + k +
                ");"};
    }
    if (m == "LINK") {
        auto [r, dd] = unpack2(ops);
        return {"push32(" + cname(r) + "); " + cname(r) + " = XSP; XSP += (" + dd + ");"};
    }
    if (m == "UNLK") {
        std::string r = cname(at(ops, 0));
        return {"XSP = " + r + "; " + r + " = pop32();"};
    }
    if (m == "LDF") return {"cpu.rfp = " + at(ops, 0) + " & 3;"};
    if (m == "INCF") return {"cpu.rfp = (cpu.rfp + 1) & 3;"};
    if (m == "DECF") return {"cpu.rfp = (cpu.rfp - 1) & 3;"};
    if (m == "LDC") {
        auto [d, s] = unpack2(ops);
        if (reg(d)) return {cname(d) + " = ldc_read(\"" + s + "\");"};
        return {"ldc_write(\"" + d + "\", " + cname(s) + ");"};
    }
    if (m == "DI") return {"cpu.iff = 7;"};
    if (m == "EI") return {"cpu.iff = " + at(ops, 0) + ";"};
    if (m == "NOP") return {";"};
    if (m == "HALT") return {"halt();"};
    if (m == "RLD" || m == "RRD") {
        Mem mm(at(ops, 1), 1);
        std::string body = m == "RLD"
                               ? "wr8(ea, (uint8_t)((t << 4) | (A & 15))); A = (uint8_t)((A & 0xF0) | (t >> 4));"
                               : "wr8(ea, (uint8_t)((A << 4) | (t >> 4))); A = (uint8_t)((A & 0xF0) | (t & 15));";
        Lines out = mm.setup();
        out.push_back("{ uint32_t ea = " + mm.addr + "; uint8_t t = rd8(ea); " + body +
                      " szflags(A, 8); cpu.fv = parity(A); cpu.fh = cpu.fn = 0; }");
        return cat(out, mm.teardown());
    }
    if (m == "BS1F" || m == "BS1B") {
        std::string c(1, char(m.back() + ('a' - 'A')));
        return {"A = bs1" + c + "(" + cname(at(ops, 1)) + ");"};
    }
    raise("NotImplementedError", m);
}

// Intraprocedural reachability from entry (stops at other functions' entries).
std::set<int64_t> func_body(const InsnMap& insns, int64_t entry, const Funcs& funcs) {
    std::set<int64_t> body;
    std::deque<int64_t> work{entry};
    while (!work.empty()) {
        int64_t a = work.front();
        work.pop_front();
        if (body.count(a) || !insns.has(a)) continue;
        if (a != entry && funcs.count(a)) continue;   // tail into another function
        body.insert(a);
        const Insn& i = insns.at(a);
        if (i.has_target && !i.is_call) work.push_back(i.target);
        if (!i.ends_block) work.push_back(a + i.size);
    }
    return body;
}

Lines Lifter::function(int64_t entry, const std::string& name) {
    std::set<int64_t> body = func_body(insns, entry, funcs);
    if (body.empty())
        throw std::runtime_error("lift: function entry 0x" + hexs(entry, 6) + " (" + name + ") is not traced code");
    std::set<int64_t> targets;
    for (int64_t a : body) {
        const Insn& i = insns.at(a);
        if (i.has_target && !i.is_call && body.count(i.target))
            if (!(funcs.count(i.target) && i.target != entry)) targets.insert(i.target);
    }
    Lines lines = {"void " + name + "(void)", "{"};
    // put the entry first; everything else in address order
    int64_t first = *body.begin();
    bool have_prev = false;
    int64_t prev = 0;
    if (first != entry) lines.push_back("    goto " + label(entry) + ";");
    for (int64_t a : body) {
        const Insn& i = insns.at(a);
        if (have_prev) {
            const Insn& p = insns.at(prev);
            if (prev + p.size != a && !p.ends_block) lines.push_back("    goto " + label(prev + p.size) + ";");
        }
        if (targets.count(a) || (a == entry && first != entry)) lines.push_back(label(a) + ":");
        Lines stmts = lift(i, entry);
        lines.push_back("    " + ljust(join(stmts, " "), 60) + " /* " + hexs(a, 6) + " " + i.text() + " */");
        prev = a;
        have_prev = true;
    }
    const Insn& last = insns.at(*body.rbegin());
    if (!last.ends_block) {
        int64_t nxt = last.addr + last.size;
        lines.push_back("    " + (funcs.count(nxt) ? goto_or_tail(nxt, entry)
                                                    : "fallthrough_unknown(0x" + hexs(nxt, 6) + "); return;"));
    }
    lines.push_back("}");
    return lines;
}

// Python int(s, 16) for a symbol-file address.
int64_t int16(const std::string& s0) {
    std::string s = strip(s0);
    bool neg = false;
    size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        if (i < s.size() && s[i] == '_') i++;
    }
    if (i >= s.size()) throw std::runtime_error("invalid literal for int() with base 16: " + py_repr(s0));
    int64_t v = 0;
    bool last_us = true;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (c == '_') {
            if (last_us) throw std::runtime_error("invalid literal for int() with base 16: " + py_repr(s0));
            last_us = true;
            continue;
        }
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) throw std::runtime_error("invalid literal for int() with base 16: " + py_repr(s0));
        v = v * 16 + d;
        last_us = false;
    }
    if (last_us) throw std::runtime_error("invalid literal for int() with base 16: " + py_repr(s0));
    return neg ? -v : v;
}

bool is_ident(const std::string& s) {
    if (s.empty()) return false;
    auto alpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    if (!alpha(s[0])) return false;
    for (char c : s)
        if (!alpha(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}

}  // namespace

LiftStats lift_run(const std::string& rom_path, const std::string& outdir, const std::string& executed,
                   const std::string& symbols, const std::string& hints_path) {
    Rom rom(read_bytes(rom_path));
    RomHeader h = rom.header();
    InsnMap insns;
    Labels labels;
    DiscoverStats dst;
    Hints hints;
    if (!hints_path.empty()) hints = read_hints(hints_path);
    discover(rom, executed, true, insns, labels, dst, hints_path.empty() ? nullptr : &hints);
    // BIOS-call annotation needs a linear pass in address order per block
    {
        std::vector<int64_t> addrs;
        addrs.reserve(insns.size());
        for (auto& kv : insns) addrs.push_back(kv.first);
        std::sort(addrs.begin(), addrs.end());
        Regs regs;
        for (int64_t a : addrs) {
            Insn& i = insns.at(a);
            annotate_bios(i, regs);
            if (i.ends_block) regs.clear();
        }
    }
    // Function entries: start, IRQ handlers, call targets, dynamic heads that no other function
    // reaches by local flow.
    Funcs funcs;
    funcs[h.entry] = "start";
    for (auto& kv : insns) {
        const Insn& i = kv.second;
        if (i.is_call && i.has_target && insns.has(i.target)) funcs.emplace(i.target, "sub_" + hexs(i.target, 6));
    }
    for (auto& [addr, n] : labels)
        if (starts_with(n, "irq_") && insns.has(addr)) funcs[addr] = n;
    std::set<int64_t> reached;
    {
        std::vector<int64_t> es;
        for (auto& kv : funcs) es.push_back(kv.first);
        for (int64_t e : es) {
            auto b = func_body(insns, e, funcs);
            reached.insert(b.begin(), b.end());
        }
    }
    for (auto& [addr, n] : labels) {
        if (starts_with(n, "dyn_") && insns.has(addr) && !reached.count(addr)) {
            funcs[addr] = "fn_" + hexs(addr, 6);
            auto b = func_body(insns, addr, funcs);
            reached.insert(b.begin(), b.end());
        }
    }
    if (!symbols.empty()) {   // readable names for known functions
        std::set<std::string> taken;
        for (auto& kv : funcs) taken.insert(kv.second);
        Bytes raw_b;
        if (!read_bytes_opt(symbols, raw_b)) throw std::runtime_error("cannot read symbols file: " + symbols);
        std::string text = universal_newlines(std::string(raw_b.begin(), raw_b.end()));
        for (const std::string& raw : splitlines(text)) {
            std::string a = split_on(raw, "#")[0];
            a = split_on(a, ";")[0];
            std::vector<std::string> w = split_ws(a);
            if (w.size() >= 2 && is_ident(w[1])) {
                int64_t ad = int16(w[0]);
                auto it = funcs.find(ad);
                if (it != funcs.end() && !taken.count(w[1]) && it->second != "start") {
                    taken.erase(it->second);
                    it->second = w[1];
                    taken.insert(w[1]);
                }
            }
        }
    }
    make_dirs(outdir);
    write_text(path_join(outdir, "ngpc_rt.h"), embedded_ngpc_rt_h());
    Lines regs_h = {"/* generated by unitron lift: register names as in the disassembly */", "#ifndef NGPC_REGS_H",
                    "#define NGPC_REGS_H"};
    for (auto& [n, e] : REGS()) regs_h.push_back("#define " + n + " (" + e.expr + ")");
    regs_h.push_back("");
    regs_h.push_back("/* hardware addresses (unitron/ngpc/hwnames.py) */");
    {
        std::map<int64_t, std::string> hw = IO();
        for (auto& [a, n] : VECTORS()) hw[a] = n;
        for (auto& [a, n] : hw) regs_h.push_back("#define " + n + " 0x" + hexs(a));
    }
    regs_h.push_back("#endif");
    regs_h.push_back("");
    write_text(path_join(outdir, "ngpc_regs.h"), join(regs_h, "\n"));

    Lifter lf(insns, funcs);
    Lines src = {"/* " + h.title + " -- lifted from the ROM by unitron lift. Do not edit by hand;",
                 " * regenerate instead. One C function per discovered TLCS-900/H function. */",
                 "#include \"ngpc_rt.h\"", ""};
    for (auto& kv : funcs) src.push_back("void " + kv.second + "(void);");
    src.push_back("");
    int64_t total = 0;
    for (auto& [e, n] : funcs) {
        append(src, lf.function(e, n));
        src.push_back("");
        total++;
    }
    append(src, {"void call_ind(uint32_t a)", "{", "    switch (a & 0xFFFFFF) {"});
    for (auto& [e, n] : funcs) src.push_back("    case 0x" + hexs(e, 6) + ": " + n + "(); return;");
    append(src, {"    default: unknown_code(a); return;", "    }", "}", ""});
    append(src, {"void jump_ind(uint32_t a) { call_ind(a); }", ""});
    write_text(path_join(outdir, "game.c"), join(src, "\n") + "\n");
    return {total, int64_t(insns.size()), lf.unimpl};
}

}  // namespace unitron
