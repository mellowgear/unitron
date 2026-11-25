// Forms and codes follow the datasheet instruction lists ("900/L1 Instruction Lists", the 900 core
// the NGPC's 900/H shares). Where several encodings are legal the policy is ours (docs/encoding.md):
//   1. the shortest legal encoding; equal lengths: the lowest byte string; but a byte `ld R,#n`
//      is always 20+R unless written `n:3` (asm900), and `:n` forbids fields narrower than n;
//   2. `ld r,r`: the prefix holds the source (`LD R,r`, 88+R);
//   3. byte MUL/MULS/DIV/DIVS rr,#: the prefix names the low byte of rr (odd short code);
//   4. (r+)/(-r) step = operand size unless `:n` is written;
//   5. `(r+0)` is an 8-bit displacement of 0, `(r)` has none;
//   6. absolute addresses: shortest width for the value, or the displacement width of a
//      relocatable/extern symbol, or the `:n` width written in the source;
//   7. jr/jrl/jp/call/calr as written; only the pseudo-instructions j/cal choose.
#include "encode.hpp"

#include <algorithm>
#include <map>

#include "regs.hpp"

namespace unitron {

namespace {

using K = Operand::Kind;
using M = Operand::Mode;

const std::map<std::string, std::string>& FORMS() {
    static const std::map<std::string, std::string> f = {
        {"LD", "LD R,r | LD r,# | LD R,(mem) | LD (mem),R | LD<W> (mem),# | LD<W> (#16),(mem) | LD<W> (mem),(#16)"},
        {"PUSH", "PUSH F | PUSH A | PUSH SR | PUSH r | PUSH<W> # | PUSH<W> (mem)"},
        {"POP", "POP F | POP A | POP SR | POP r | POP<W> (mem)"},
        {"LDA", "LDA R,mem (R word or long)"},
        {"LDAR", "LDAR R,label (R word or long)"},
        {"EX", "EX F,F' | EX R,r | EX (mem),R"},
        {"ALU", "{m} R,r | {m} r,# | {m} R,(mem) | {m} (mem),R | {m}<W> (mem),#"},
        {"CP", "CP R,r | CP r,# | CP R,(mem) | CP (mem),R | CP<W> (mem),#"},
        {"INC", "{m} #3,r | {m}<W> #3,(mem)"},
        {"MUL", "{m} RR,r | {m} rr,# | {m} RR,(mem)"},
        {"SHIFT", "{m} #4,r | {m} A,r | {m}<W> (mem)"},
        {"CF", "{m} #4,r | {m} A,r | {m} #3,(mem) | {m} A,(mem)"},
        {"BIT", "{m} #4,r | {m} #3,(mem)"},
        {"JP", "JP #16 | JP #24 | JP [cc,]mem"},
        {"CALL", "CALL #16 | CALL #24 | CALL [cc,]mem"},
    };
    return f;
}

int idx_of(const char* const* t, int n, const std::string& m) {
    for (int i = 0; i < n; i++)
        if (m == t[i]) return i;
    return -1;
}

const char* const MULN[4] = {"MUL", "MULS", "DIV", "DIVS"};
const char* const CFN[5] = {"ANDCF", "ORCF", "XORCF", "LDCF", "STCF"};
const std::map<std::string, int> BITS_REG = {{"RES", 0x30}, {"SET", 0x31}, {"CHG", 0x32}, {"BIT", 0x33}, {"TSET", 0x34}};
const std::map<std::string, int> BITS_MEM = {{"TSET", 0xA8}, {"RES", 0xB0}, {"SET", 0xB8}, {"CHG", 0xC0}, {"BIT", 0xC8}};
const std::map<std::string, Bytes> SIMPLE = {{"NOP", {0x00}},  {"HALT", {0x05}}, {"RETI", {0x07}}, {"INCF", {0x0C}},
                                             {"DECF", {0x0D}}, {"RCF", {0x10}},  {"SCF", {0x11}},  {"CCF", {0x12}},
                                             {"ZCF", {0x13}},  {"DI", {0x06, 0x07}}};
struct Single {
    int op;
    std::vector<int> allowed;
};
const std::map<std::string, Single> SINGLE = {{"NEG", {0x07, {1, 2}}}, {"CPL", {0x06, {1, 2}}}, {"EXTZ", {0x12, {2, 4}}},
                                              {"EXTS", {0x13, {2, 4}}}, {"PAA", {0x14, {2, 4}}}, {"DAA", {0x10, {1}}},
                                              {"MIRR", {0x16, {2}}},    {"MULA", {0x19, {2}}},   {"UNLK", {0x0D, {4}}}};
const std::map<std::string, std::pair<int, int>> MINC = {{"MINC1", {0x38, 1}}, {"MINC2", {0x39, 2}}, {"MINC4", {0x3A, 4}},
                                                         {"MDEC1", {0x3C, 1}}, {"MDEC2", {0x3D, 2}}, {"MDEC4", {0x3E, 4}}};
const std::map<std::string, int> BLOCK = {{"LDI", 0x10}, {"LDIR", 0x11}, {"LDD", 0x12}, {"LDDR", 0x13}};
const std::map<std::string, int> CPBLK = {{"CPI", 0x14}, {"CPIR", 0x15}, {"CPD", 0x16}, {"CPDR", 0x17}};
const std::set<std::string> JUMPS = {"JR", "JRL", "JP", "CALL", "CALR", "DJNZ", "J", "CAL"};

Bytes le(int64_t v, int n) {
    Bytes b;
    uint64_t u = uint64_t(v);
    for (int i = 0; i < n; i++) b.push_back(uint8_t(u >> (8 * i)));
    return b;
}
Bytes cat(Bytes a, const Bytes& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
Bytes cat(Bytes a, std::initializer_list<int> b) {
    for (int x : b) a.push_back(uint8_t(x));
    return a;
}
Bytes best(const std::vector<Bytes>& c) {
    return *std::min_element(c.begin(), c.end(), [](const Bytes& a, const Bytes& b) {
        if (a.size() != b.size()) return a.size() < b.size();
        return a < b;
    });
}

const char* size_name(int a) { return a == 1 ? "byte" : a == 2 ? "word" : "long"; }

struct E {
    std::string m;
    int sfx;
    int64_t pc;
    const EvalFn& ev;
    bool final;

    EncodeError err(const std::string& msg) const { return EncodeError(msg); }
    EncodeError mismatch(const std::string& key = "") const {
        auto it = FORMS().find(key.empty() ? m : key);
        if (it != FORMS().end())
            return EncodeError("operand type mismatch; legal forms: " + replace_all(it->second, "{m}", m));
        return EncodeError("operand type mismatch");
    }
    VW val(const ExprP& e) const { return ev(e); }
    int64_t imm(const ExprP& e, int size) const { return immw(e, size).first; }
    int64_t imm(const ExprP& e, int /*size*/, int64_t lo, int64_t hi, const std::string& what) const {
        VW r = ev(e);
        if (final && !(lo <= r.first && r.first <= hi))
            throw EncodeError(what + " " + dec(r.first) + " out of range " + dec(lo) + ".." + dec(hi));
        return r.first;
    }
    VW immw(const ExprP& e, int size) const {
        VW r = ev(e);
        int64_t lo = -(int64_t(1) << (8 * size - 1)), hi = (int64_t(1) << (8 * size)) - 1;
        if (final && r.second > 8 * size)
            throw EncodeError("a " + dec(r.second) + "-bit value does not fit a " + dec(8 * size) + "-bit operand");
        if (final && !(lo <= r.first && r.first <= hi))
            throw EncodeError("value " + dec(r.first) + " out of range " + dec(lo) + ".." + dec(hi));
        return r;
    }
    int size(int opsize, std::initializer_list<int> allowed, int def = 0) const {
        int s = sfx;
        if (opsize && s && s != opsize)
            throw EncodeError("size suffix does not match the " + dec(opsize * 8) + "-bit operand");
        int z = opsize ? opsize : s ? s : def;
        if (!z) throw EncodeError("operand size unknown; add a B/W/L suffix");
        if (std::find(allowed.begin(), allowed.end(), z) == allowed.end()) {
            std::string names;
            for (int a : allowed) names += (names.empty() ? "" : "/") + std::string(size_name(a));
            throw EncodeError(m + " is " + names + " only");
        }
        return z;
    }
    int size_v(int opsize, const std::vector<int>& allowed) const {
        int s = sfx;
        if (opsize && s && s != opsize)
            throw EncodeError("size suffix does not match the " + dec(opsize * 8) + "-bit operand");
        int z = opsize ? opsize : s;
        if (!z) throw EncodeError("operand size unknown; add a B/W/L suffix");
        if (std::find(allowed.begin(), allowed.end(), z) == allowed.end()) {
            std::string names;
            for (int a : allowed) names += (names.empty() ? "" : "/") + std::string(size_name(a));
            throw EncodeError(m + " is " + names + " only");
        }
        return z;
    }
    Bytes rel(int64_t target, int64_t end, int bits) const {
        int64_t d = target - end;
        int64_t lim = int64_t(1) << (bits - 1);
        if (final && !(-lim <= d && d < lim))
            throw EncodeError("relative target out of range (" + std::string(d >= 0 ? "+" : "") + dec(d) +
                              " bytes, E227)");
        return le(d, bits / 8);
    }
};

Bytes reg_prefix(const RegOp& r) {
    int base = r.size == 1 ? 0xC8 : r.size == 2 ? 0xD8 : 0xE8;
    if (r.shrt >= 0) return {uint8_t(base | r.shrt)};
    return {uint8_t(base - 1), uint8_t(r.code)};
}

int need_short(const RegOp& r, const E& e) {
    if (r.shrt < 0)
        throw e.err(r.name + " must be a current-bank register (W A B C D E H L / WA..SP / XWA..XSP) here");
    return r.shrt;
}

// Prefix bytes for a memory operand. group: 1/2/4 = source group of that size, 0 = dst.
Bytes mem_prefix(const E& e, const Operand& m, int group, int opsize) {
    int base = group == 1 ? 0x80 : group == 2 ? 0x90 : group == 4 ? 0xA0 : 0xB0;
    int hi = base + 0x40;
    M mode = m.mode;
    const RegOp& r = m.reg;
    if (mode == M::Ind || mode == M::Disp || mode == M::Idx || mode == M::Pre || mode == M::Post) {
        if (r.size != 4) throw e.err("memory base register " + r.name + " must be 32-bit");
    }
    if (mode == M::Ind) {
        if (r.shrt >= 0) return {uint8_t(base | r.shrt)};
        return {uint8_t(hi | 3), uint8_t(r.code)};
    }
    if (mode == M::Disp) {
        VW dw = e.val(m.disp);
        int64_t d = dw.first;
        int w = dw.second;
        if (w > 16 && e.final) throw e.err("a displacement is at most 16 bits, not " + dec(w));
        if (w > 8 && 0x8000 <= d && d <= 0xFFFF) d -= 0x10000;
        if (r.shrt >= 0 && -128 <= d && d <= 127 && w <= 8) return {uint8_t(base | 8 | r.shrt), uint8_t(d & 0xFF)};
        if ((-32768 <= d && d <= 32767) || !e.final) return cat(Bytes{uint8_t(hi | 3), uint8_t(r.code | 1)}, le(d, 2));
        throw e.err("displacement " + dec(d) + " out of range -32768..32767");
    }
    if (mode == M::Idx) {
        const RegOp& x = m.idx;
        return {uint8_t(hi | 3), uint8_t(x.size == 1 ? 3 : 7), uint8_t(r.code), uint8_t(x.code)};
    }
    if (mode == M::Pre || mode == M::Post) {
        int step = m.step ? m.step : opsize ? opsize : 1;
        int sc = step == 1 ? 0 : step == 2 ? 1 : 2;
        return {uint8_t(hi | (mode == M::Pre ? 4 : 5)), uint8_t(r.code | sc)};
    }
    if (mode == M::Abs) {
        VW aw = e.val(m.addr);
        int64_t a = aw.first;
        int w = aw.second;
        if (a < 0 || a > 0xFFFFFF) {
            if (e.final) throw e.err("address 0x" + hexs(a & 0xFFFFFFFFLL) + " out of range");
            a &= 0xFFFFFF;
        }
        int need = a < 0x100 ? 1 : a < 0x10000 ? 2 : 3;
        if (w > 24 && e.final) throw e.err("an absolute address is at most 24 bits, not " + dec(w));
        int n = std::max(need, (w + 7) / 8);
        if (w && need * 8 > w && e.final)
            throw e.err("address 0x" + hexs(a) + " does not fit the " + dec(w) + "-bit width of its symbol");
        return cat(Bytes{uint8_t(hi | (n - 1))}, le(a, n));
    }
    throw e.err("block-transfer operand not allowed here");
}

// (#16) operand of LD<W> (#16),(mem) / (mem),(#16)
Bytes abs16(const E& e, const Operand& m) {
    VW aw = e.val(m.addr);
    int64_t a = aw.first;
    if (e.final && !(0 <= a && a <= 0xFFFF)) throw e.err("address 0x" + hexs(a) + " is not 16-bit");
    if (e.final && aw.second > 16) throw e.err("a large (24-bit) symbol cannot be a #16 address");
    return le(a, 2);
}

VW target_of(const E& e, const Operand& op) {
    if (op.kind == K::Imm) return e.val(op.expr);
    if (op.kind == K::Mem && op.mode == M::Abs) return e.val(op.addr);
    throw e.err("branch target must be an address");
}

Bytes jump(const E& e, const std::string& m, std::vector<Operand> ops, const JumpOpts* jo) {
    int64_t pc = e.pc;
    int cc = 8;
    if (!ops.empty() && ops[0].kind == K::Cond) {
        cc = ops[0].code;
        ops.erase(ops.begin());
    }
    if (m == "DJNZ") {
        RegOp r;
        const Operand* t;
        if (ops.size() == 2 && ops[0].kind == K::Reg) {
            r = ops[0].reg;
            t = &ops[1];
        } else if (ops.size() == 1) {
            r = make_reg("B");
            t = &ops[0];
        } else {
            throw e.err("DJNZ takes [r,]label");
        }
        e.size(r.size, {1, 2});
        Bytes pre = reg_prefix(r);
        int64_t tv = target_of(e, *t).first;
        if (pre.size() == 1 && tv - (pc + 3) == 128) pre = {uint8_t(pre[0] - 1 - (pre[0] & 7)), uint8_t(r.code)};
        int64_t end = pc + int64_t(pre.size()) + 2;
        return cat(cat(pre, {0x1C}), e.rel(tv, end, 8));
    }
    if (ops.size() != 1) throw e.err(m + " takes [cc,]address");
    const Operand& t = ops[0];
    if (m == "JR" || m == "JRL") {
        int64_t tv = target_of(e, t).first;
        if (m == "JR") return cat(Bytes{uint8_t(0x60 | cc)}, e.rel(tv, pc + 2, 8));
        return cat(Bytes{uint8_t(0x70 | cc)}, e.rel(tv, pc + 3, 16));
    }
    if (m == "CALR") {
        if (cc != 8) throw e.err("CALR has no condition");
        int64_t tv = target_of(e, t).first;
        return cat(Bytes{0x1E}, e.rel(tv, pc + 3, 16));
    }
    bool call = m == "CALL" || m == "CAL";
    if (t.kind == K::Mem && t.mode != M::Abs) return cat(mem_prefix(e, t, 0, 0), {(call ? 0xE0 : 0xD0) | cc});
    VW tw = target_of(e, t);
    int64_t tv = tw.first;
    int w = tw.second;
    std::vector<Bytes> c;
    ExprP texpr = t.kind == K::Imm ? t.expr : t.addr;
    bool relax = (m == "J" || m == "CAL") && (!jo || jo->optimize) && (w == 0 || (jo && jo->local && jo->local(texpr)));
    if (relax) {
        if (m == "J") {
            if (-128 <= tv - (pc + 2) && tv - (pc + 2) < 128) c.push_back(cat(Bytes{uint8_t(0x60 | cc)}, le(tv - (pc + 2), 1)));
            if (-32768 <= tv - (pc + 3) && tv - (pc + 3) < 32768) c.push_back(cat(Bytes{uint8_t(0x70 | cc)}, le(tv - (pc + 3), 2)));
        } else if (cc == 8 && -32768 <= tv - (pc + 3) && tv - (pc + 3) < 32768) {
            c.push_back(cat(Bytes{0x1E}, le(tv - (pc + 3), 2)));
        }
    }
    if (cc == 8) {
        if ((w && w <= 16) || (w == 0 && 0 <= tv && tv < 0x10000)) {
            if (e.final && !(0 <= tv && tv < 0x10000))
                throw e.err("address 0x" + hexs(tv & 0xFFFFFFFFLL) + " does not fit the symbol's medium displacement");
            c.push_back(cat(Bytes{uint8_t(call ? 0x1C : 0x1A)}, le(tv, 2)));
        } else {
            if (e.final && !(0 <= tv && tv <= 0xFFFFFF))
                throw e.err("address 0x" + hexs(tv & 0xFFFFFFFFLL) + " out of range");
            c.push_back(cat(Bytes{uint8_t(call ? 0x1D : 0x1B)}, le(tv, 3)));
        }
    } else {
        Operand a;
        a.kind = K::Mem;
        a.mode = M::Abs;
        a.addr = t.kind == K::Imm ? t.expr : t.addr;
        c.push_back(cat(mem_prefix(e, a, 0, 0), {(call ? 0xE0 : 0xD0) | cc}));
    }
    return best(c);
}

bool kinds(const std::vector<Operand>& ops, K a, K b) { return ops.size() == 2 && ops[0].kind == a && ops[1].kind == b; }

}  // namespace

const std::set<std::string>& MNEMONICS() {
    static const std::set<std::string> s = [] {
        std::set<std::string> r;
        for (auto& kv : SIMPLE) r.insert(kv.first);
        for (auto& kv : SINGLE) r.insert(kv.first);
        for (auto& kv : MINC) r.insert(kv.first);
        for (auto& kv : BLOCK) r.insert(kv.first);
        for (auto& kv : CPBLK) r.insert(kv.first);
        for (auto x : ALU) r.insert(x);
        for (auto x : SHIFT) r.insert(x);
        for (auto x : MULN) r.insert(x);
        for (auto x : CFN) r.insert(x);
        for (auto& kv : BITS_REG) r.insert(kv.first);
        for (auto& x : JUMPS) r.insert(x);
        for (auto x : {"LD", "PUSH", "POP", "LDA", "LDAR", "EX", "INC", "DEC", "EI", "SWI", "LDF", "RET", "RETD", "LDC",
                       "LINK", "SCC", "RLD", "RRD", "BS1F", "BS1B", "LDX", "NORMAL", "MAX"})
            r.insert(x);
        return r;
    }();
    return s;
}

bool cond_first(const std::string& m) {
    static const std::set<std::string> s = {"JR", "JRL", "JP", "CALL", "RET", "SCC", "J", "CAL"};
    return s.count(m) > 0;
}
bool bare_addr(const std::string& m) {
    static const std::set<std::string> s = {"JP", "CALL", "LDA", "J", "CAL"};
    return s.count(m) > 0;
}

bool split_mnemonic(const std::string& m, std::string& base, int& sfx) {
    std::string u = upper(m);
    if (MNEMONICS().count(u)) {
        base = u;
        sfx = 0;
        return true;
    }
    if (u.size() > 1) {
        char c = u.back();
        int s = c == 'B' ? 1 : c == 'W' ? 2 : c == 'L' ? 4 : 0;
        if (s && MNEMONICS().count(u.substr(0, u.size() - 1))) {
            base = u.substr(0, u.size() - 1);
            sfx = s;
            return true;
        }
    }
    return false;
}

Bytes encode(const std::string& m, int sfx, const std::vector<Operand>& ops_in, int64_t pc, const EvalFn& ev, bool final,
             const JumpOpts* jo) {
    E e{m, sfx, pc, ev, final};
    std::vector<Operand> ops = ops_in;
    size_t n = ops.size();
    auto k = [&](size_t i) { return ops[i].kind; };

    if (SIMPLE.count(m)) {
        if (n) throw e.err(m + " takes no operands");
        return SIMPLE.at(m);
    }
    if (m == "NORMAL" || m == "MAX") throw e.err(m + " is undefined on the 900/H");

    if (m == "LD") {
        if (n != 2) throw e.mismatch();
        const Operand &a = ops[0], &b = ops[1];
        if (kinds(ops, K::Reg, K::Reg)) {
            if (a.reg.size != b.reg.size) throw e.err("register sizes differ");
            e.size(a.reg.size, {1, 2, 4});
            if (a.reg.shrt >= 0) return cat(reg_prefix(b.reg), {0x88 | a.reg.shrt});
            if (b.reg.shrt >= 0) return cat(reg_prefix(a.reg), {0x98 | b.reg.shrt});
            throw e.err("one of the registers must be a current-bank register");
        }
        if (kinds(ops, K::Reg, K::Imm)) {
            int s = e.size(a.reg.size, {1, 2, 4});
            VW vw = e.immw(b.expr, s);
            int64_t v = vw.first;
            int w = vw.second;
            bool short3 = 0 <= v && v <= 7 && w <= 3;
            // byte: LD r,#3 only when written n:3, or for a non-standard register (the reference ROM: 82/82 `ld RW3,n`)
            if (short3 && (s > 1 || w || a.reg.shrt < 0)) return cat(reg_prefix(a.reg), {0xA8 | int(v)});
            if (a.reg.shrt >= 0) return cat(Bytes{uint8_t((s == 1 ? 0x20 : s == 2 ? 0x30 : 0x40) | a.reg.shrt)}, le(v, s));
            return cat(cat(reg_prefix(a.reg), {0x03}), le(v, s));
        }
        if (kinds(ops, K::Reg, K::Mem)) {
            int s = e.size(a.reg.size, {1, 2, 4});
            Bytes p = mem_prefix(e, b, s, s);
            return cat(p, {0x20 | need_short(a.reg, e)});
        }
        if (kinds(ops, K::Mem, K::Reg)) {
            int s = e.size(b.reg.size, {1, 2, 4});
            Bytes p = mem_prefix(e, a, 0, s);
            return cat(p, {(s == 1 ? 0x40 : s == 2 ? 0x50 : 0x60) | need_short(b.reg, e)});
        }
        if (kinds(ops, K::Mem, K::Imm)) {
            int s = e.size(0, {1, 2}, 1);
            int64_t v = e.imm(b.expr, s);
            std::vector<Bytes> c{cat(cat(mem_prefix(e, a, 0, s), {s == 1 ? 0 : 2}), le(v, s))};
            if (a.mode == M::Abs) {
                VW av = e.val(a.addr);
                if (0 <= av.first && av.first < 0x100 && av.second <= 8)
                    c.push_back(cat(Bytes{uint8_t(s == 1 ? 0x08 : 0x0A), uint8_t(av.first)}, le(v, s)));
            }
            return best(c);
        }
        if (kinds(ops, K::Mem, K::Mem)) {
            int s = e.size(0, {1, 2}, 1);
            std::vector<Bytes> c;
            bool have_why = false;
            std::string why;
            for (int form = 0; form < 2; form++) {
                const Operand& dst_abs = form == 0 ? a : b;
                if (dst_abs.mode != M::Abs) continue;
                try {
                    if (form == 0) {
                        Bytes p = mem_prefix(e, b, s, s);
                        c.push_back(cat(cat(p, {0x19}), abs16(e, a)));
                    } else {
                        Bytes p = mem_prefix(e, a, 0, s);
                        c.push_back(cat(cat(p, {s == 1 ? 0x14 : 0x16}), abs16(e, b)));
                    }
                } catch (EncodeError& x) {
                    have_why = true;
                    why = x.what();
                }
            }
            if (c.empty()) {
                if (have_why) throw EncodeError(why);
                throw e.err("one side of a memory-to-memory LD must be a 16-bit address");
            }
            return best(c);
        }
        throw e.mismatch();
    }

    if (m == "PUSH" || m == "POP") {
        if (n != 1) throw e.mismatch();
        const Operand& a = ops[0];
        bool push = m == "PUSH";
        if (a.kind == K::Special) {
            if (a.name == "F") return {uint8_t(push ? 0x18 : 0x19)};
            if (a.name == "SR") return {uint8_t(push ? 0x02 : 0x03)};
            throw e.mismatch();
        }
        if (a.kind == K::Reg) {
            int s = e.size(a.reg.size, {1, 2, 4});
            std::vector<Bytes> c{cat(reg_prefix(a.reg), {push ? 0x04 : 0x05})};
            if (a.reg.name == "A") c.push_back({uint8_t(push ? 0x14 : 0x15)});
            if (s > 1 && a.reg.shrt >= 0) c.push_back({uint8_t((push ? 0x28 : 0x48) + (s == 4 ? 0x10 : 0) + a.reg.shrt)});
            return best(c);
        }
        if (a.kind == K::Imm && push) {
            int s = e.size(0, {1, 2}, 1);
            return cat(Bytes{uint8_t(s == 1 ? 0x09 : 0x0B)}, le(e.imm(a.expr, s), s));
        }
        if (a.kind == K::Mem) {
            int s = e.size(0, {1, 2}, 1);
            if (push) return cat(mem_prefix(e, a, s, s), {0x04});
            return cat(mem_prefix(e, a, 0, s), {s == 1 ? 4 : 6});
        }
        throw e.mismatch();
    }

    if (m == "LDA") {
        if (n != 2 || k(0) != K::Reg || k(1) != K::Mem) throw e.mismatch();
        int s = e.size(ops[0].reg.size, {2, 4});
        Bytes p = mem_prefix(e, ops[1], 0, 0);
        return cat(p, {(s == 2 ? 0x20 : 0x30) | need_short(ops[0].reg, e)});
    }

    if (m == "LDAR") {
        if (n != 2 || k(0) != K::Reg || (k(1) != K::Imm && k(1) != K::Mem)) throw e.mismatch();
        const Operand &a = ops[0], &b = ops[1];
        int s = e.size(a.reg.size, {2, 4});
        ExprP ex = b.kind == K::Imm ? b.expr : (b.mode == M::Abs ? b.addr : nullptr);
        if (!ex) throw e.mismatch();
        int64_t t = e.val(ex).first;
        Bytes p = cat(Bytes{0xF3, 0x13}, e.rel(t, pc + 4, 16));
        return cat(p, {(s == 2 ? 0x20 : 0x30) | need_short(a.reg, e)});
    }

    if (m == "EX") {
        if (n != 2) throw e.mismatch();
        const Operand &a = ops[0], &b = ops[1];
        if (kinds(ops, K::Special, K::Special) && a.name == "F" && b.name == "F'") return {0x16};
        if (kinds(ops, K::Reg, K::Reg)) {
            if (a.reg.size != b.reg.size) throw e.err("register sizes differ");
            e.size(a.reg.size, {1, 2});
            if (a.reg.shrt >= 0) return cat(reg_prefix(b.reg), {0xB8 | a.reg.shrt});
            if (b.reg.shrt >= 0) return cat(reg_prefix(a.reg), {0xB8 | b.reg.shrt});
            throw e.err("one of the registers must be a current-bank register");
        }
        if (kinds(ops, K::Mem, K::Reg) || kinds(ops, K::Reg, K::Mem)) {
            const Operand& mem = k(0) == K::Mem ? a : b;
            const Operand& r = k(0) == K::Mem ? b : a;
            int s = e.size(r.reg.size, {1, 2});
            Bytes p = mem_prefix(e, mem, s, s);
            return cat(p, {0x30 | need_short(r.reg, e)});
        }
        throw e.mismatch();
    }

    if (SINGLE.count(m)) {
        auto& sg = SINGLE.at(m);
        if (n != 1 || k(0) != K::Reg) throw e.err(m + " takes one register");
        e.size_v(ops[0].reg.size, sg.allowed);
        return cat(reg_prefix(ops[0].reg), {sg.op});
    }

    if (BLOCK.count(m)) {
        int s = e.size(0, {1, 2}, 1);
        int p;
        if (n == 0) {
            p = 3;
        } else if (n == 2 && kinds(ops, K::Mem, K::Mem)) {
            M want = (m == "LDD" || m == "LDDR") ? M::Dec : M::Post;
            const Operand &a = ops[0], &b = ops[1];
            std::string na = a.has_reg ? a.reg.name : "", nb = b.has_reg ? b.reg.name : "";
            bool ok_mode = a.mode == want && b.mode == want && !a.step && !b.step;
            if (ok_mode && na == "XDE" && nb == "XHL") p = 3;
            else if (ok_mode && na == "XIX" && nb == "XIY") p = 5;
            else {
                std::string sign = want == M::Dec ? "-" : "+";
                throw e.err(m + " operands are (XDE" + sign + "),(XHL" + sign + ") or (XIX" + sign + "),(XIY" + sign + ")");
            }
        } else {
            throw e.mismatch();
        }
        return {uint8_t((s == 1 ? 0x80 : 0x90) | p), uint8_t(BLOCK.at(m))};
    }

    if (CPBLK.count(m)) {
        M want = (m == "CPD" || m == "CPDR") ? M::Dec : M::Post;
        int s, r;
        if (n == 0) {
            s = e.size(0, {1, 2}, 1);
            r = 3;
        } else if (n == 2 && kinds(ops, K::Reg, K::Mem) && (ops[0].reg.name == "A" || ops[0].reg.name == "WA") &&
                   ops[1].mode == want && !ops[1].step && ops[1].reg.shrt >= 0) {
            s = e.size(ops[0].reg.size, {1, 2});
            r = ops[1].reg.shrt;
        } else {
            std::string sign = want == M::Dec ? "-" : "+";
            throw e.err(m + " operands are A,(R" + sign + ") or WA,(R" + sign + ")");
        }
        return {uint8_t((s == 1 ? 0x80 : 0x90) | r), uint8_t(CPBLK.at(m))};
    }

    int ai = idx_of(ALU, 8, m);
    if (ai >= 0) {
        std::string key = m == "CP" ? "CP" : "ALU";
        if (n != 2) throw e.mismatch(key);
        const Operand &a = ops[0], &b = ops[1];
        if (kinds(ops, K::Reg, K::Reg)) {
            if (a.reg.size != b.reg.size) throw e.err("register sizes differ");
            e.size(a.reg.size, {1, 2, 4});
            return cat(reg_prefix(b.reg), {0x80 + 0x10 * ai + need_short(a.reg, e)});
        }
        if (kinds(ops, K::Reg, K::Imm)) {
            int s = e.size(a.reg.size, {1, 2, 4});
            VW vw = e.immw(b.expr, s);
            if (m == "CP" && s < 4 && 0 <= vw.first && vw.first <= 7 && vw.second <= 3)
                return cat(reg_prefix(a.reg), {0xD8 | int(vw.first)});
            return cat(cat(reg_prefix(a.reg), {0xC8 + ai}), le(vw.first, s));
        }
        if (kinds(ops, K::Reg, K::Mem)) {
            int s = e.size(a.reg.size, {1, 2, 4});
            Bytes p = mem_prefix(e, b, s, s);
            return cat(p, {0x80 + 0x10 * ai + need_short(a.reg, e)});
        }
        if (kinds(ops, K::Mem, K::Reg)) {
            int s = e.size(b.reg.size, {1, 2, 4});
            Bytes p = mem_prefix(e, a, s, s);
            return cat(p, {0x88 + 0x10 * ai + need_short(b.reg, e)});
        }
        if (kinds(ops, K::Mem, K::Imm)) {
            int s = e.size(0, {1, 2}, 1);
            Bytes p = mem_prefix(e, a, s, s);
            return cat(cat(p, {0x38 + ai}), le(e.imm(b.expr, s), s));
        }
        throw e.mismatch(key);
    }

    if (m == "INC" || m == "DEC") {
        if (n != 2 || k(0) != K::Imm || (k(1) != K::Reg && k(1) != K::Mem)) throw e.mismatch("INC");
        int64_t c = e.imm(ops[0].expr, 1, 1, 8, "count");
        int op = (m == "INC" ? 0x60 : 0x68) | int(c & 7);
        const Operand& b = ops[1];
        if (b.kind == K::Reg) {
            e.size(b.reg.size, {1, 2, 4});
            return cat(reg_prefix(b.reg), {op});
        }
        int s = e.size(0, {1, 2}, 1);
        return cat(mem_prefix(e, b, s, s), {op});
    }

    int mi = idx_of(MULN, 4, m);
    if (mi >= 0) {
        if (n != 2 || k(0) != K::Reg) throw e.mismatch("MUL");
        const RegOp& rr = ops[0].reg;
        const Operand& b = ops[1];
        if (rr.size != 2 && rr.size != 4)
            throw e.err(m + ": the first operand is the double-size register (WA.. or XWA..)");
        int s = e.size(rr.size / 2, {1, 2});
        if (b.kind == K::Imm) {
            int64_t v = e.imm(b.expr, s);
            Bytes pre;
            if (s == 1) pre = rr.shrt >= 0 && rr.shrt < 4 ? Bytes{uint8_t(0xC8 | (2 * rr.shrt + 1))} : Bytes{0xC7, uint8_t(rr.code)};
            else pre = rr.shrt >= 0 ? Bytes{uint8_t(0xD8 | rr.shrt)} : Bytes{0xD7, uint8_t(rr.code)};
            return cat(cat(pre, {0x08 + mi}), le(v, s));
        }
        int R;
        if (s == 1) {
            if (rr.shrt < 0 || rr.shrt > 3) throw e.err(m + " RR,r: RR must be WA, BC, DE or HL");
            R = 2 * rr.shrt + 1;
        } else {
            R = need_short(rr, e);
        }
        int op = 0x40 + 8 * mi + R;
        if (b.kind == K::Reg) {
            if (b.reg.size != s) throw e.err("register sizes do not fit");
            return cat(reg_prefix(b.reg), {op});
        }
        if (b.kind == K::Mem) return cat(mem_prefix(e, b, s, s), {op});
        throw e.mismatch("MUL");
    }

    if (MINC.count(m)) {
        auto mc = MINC.at(m);
        if (!kinds(ops, K::Imm, K::Reg)) throw e.err(m + " takes #,r");
        e.size(ops[1].reg.size, {2});
        int64_t v = e.imm(ops[0].expr, 2);
        return cat(cat(reg_prefix(ops[1].reg), {mc.first}), le(v - mc.second, 2));
    }

    int ci = idx_of(CFN, 5, m);
    if (ci >= 0 || BITS_REG.count(m)) {
        std::string key = ci >= 0 ? "CF" : "BIT";
        if (n != 2) throw e.mismatch(key);
        const Operand &a = ops[0], &b = ops[1];
        if (a.kind == K::Reg && a.reg.name == "A" && ci >= 0) {
            if (b.kind == K::Reg) {
                e.size(b.reg.size, {1, 2});
                return cat(reg_prefix(b.reg), {0x28 + ci});
            }
            if (b.kind == K::Mem) {
                e.size(0, {1}, 1);
                return cat(mem_prefix(e, b, 0, 1), {0x28 + ci});
            }
            throw e.mismatch(key);
        }
        if (a.kind != K::Imm) throw e.mismatch(key);
        if (b.kind == K::Reg) {
            e.size(b.reg.size, {1, 2});
            int64_t v = e.imm(a.expr, 1, 0, 15, "bit number");
            int op = ci >= 0 ? 0x20 + ci : BITS_REG.at(m);
            return cat(reg_prefix(b.reg), {op, int(v & 0xFF)});
        }
        if (b.kind == K::Mem) {
            e.size(0, {1}, 1);
            int64_t v = e.imm(a.expr, 1, 0, 7, "bit number");
            int op = ci >= 0 ? 0x80 + 8 * ci : BITS_MEM.at(m);
            return cat(mem_prefix(e, b, 0, 1), {op + int(v & 7)});
        }
        throw e.mismatch(key);
    }

    int si = idx_of(SHIFT, 8, m);
    if (si >= 0) {
        if (n == 1 && k(0) == K::Mem) {
            int s = e.size(0, {1, 2}, 1);
            return cat(mem_prefix(e, ops[0], s, s), {0x78 + si});
        }
        if (n == 2 && k(1) == K::Reg) {
            const Operand &a = ops[0], &b = ops[1];
            e.size(b.reg.size, {1, 2, 4});
            if (a.kind == K::Reg && a.reg.name == "A") return cat(reg_prefix(b.reg), {0xF8 + si});
            if (a.kind == K::Imm) {
                int64_t v = e.imm(a.expr, 1, 1, 16, "shift count");
                return cat(reg_prefix(b.reg), {0xE8 + si, int(v & 0xF)});
            }
        }
        throw e.mismatch("SHIFT");
    }

    if (m == "RLD" || m == "RRD") {
        if (n == 2 && k(0) == K::Reg && ops[0].reg.name == "A") {
            ops.erase(ops.begin());
            n = 1;
        }
        if (n != 1 || ops[0].kind != K::Mem) throw e.err(m + " takes [A,](mem)");
        e.size(0, {1}, 1);
        return cat(mem_prefix(e, ops[0], 1, 1), {m == "RLD" ? 0x06 : 0x07});
    }

    if (m == "BS1F" || m == "BS1B") {
        if (!kinds(ops, K::Reg, K::Reg) || ops[0].reg.name != "A") throw e.err(m + " takes A,r");
        e.size(ops[1].reg.size, {2});
        return cat(reg_prefix(ops[1].reg), {m == "BS1F" ? 0x0E : 0x0F});
    }

    if (m == "EI") {
        int64_t v = (n == 1 && k(0) == K::Imm) ? e.imm(ops[0].expr, 1, 0, 7, "interrupt level") : 0;
        if (n > 1 || (n == 1 && k(0) != K::Imm)) throw e.err("EI takes [#3]");
        return {0x06, uint8_t(v)};
    }
    if (m == "SWI") {
        if (n > 1 || (n == 1 && k(0) != K::Imm)) throw e.err("SWI takes [#3]");
        int64_t v = n ? e.imm(ops[0].expr, 1, 0, 7, "SWI number") : 7;
        return {uint8_t(0xF8 | v)};
    }
    if (m == "LDF") {
        if (n != 1 || k(0) != K::Imm) throw e.err("LDF takes #3");
        return {0x17, uint8_t(e.imm(ops[0].expr, 1, 0, 7, "register bank"))};
    }
    if (m == "RETD") {
        if (n != 1 || k(0) != K::Imm) throw e.err("RETD takes d16");
        return cat(Bytes{0x0F}, le(e.imm(ops[0].expr, 2, -32768, 65535, "value"), 2));
    }
    if (m == "RET") {
        if (n == 0) return {0x0E};
        if (n == 1 && k(0) == K::Cond) {
            int cc = ops[0].code;
            if (cc == 8) return {0x0E};
            return {0xB0, uint8_t(0xF0 | cc)};
        }
        throw e.err("RET takes [cc]");
    }

    if (m == "LDC") {
        if (n != 2) throw e.err("LDC takes cr,r or r,cr");
        const Operand *cr, *r;
        int op;
        if (kinds(ops, K::Ctl, K::Reg)) {
            cr = &ops[0];
            r = &ops[1];
            op = 0x2E;
        } else if (kinds(ops, K::Reg, K::Ctl)) {
            r = &ops[0];
            cr = &ops[1];
            op = 0x2F;
        } else {
            throw e.err("LDC takes cr,r or r,cr");
        }
        if (r->reg.size != cr->size) throw e.err(cr->name + " is " + dec(cr->size * 8) + "-bit");
        e.size(r->reg.size, {1, 2, 4});
        return cat(reg_prefix(r->reg), {op, cr->code});
    }

    if (m == "LINK") {
        if (!kinds(ops, K::Reg, K::Imm)) throw e.err("LINK takes r,d16");
        e.size(ops[0].reg.size, {4});
        return cat(cat(reg_prefix(ops[0].reg), {0x0C}), le(e.imm(ops[1].expr, 2, -32768, 65535, "value"), 2));
    }

    if (m == "SCC") {
        if (!kinds(ops, K::Cond, K::Reg)) throw e.err("SCC takes cc,r");
        e.size(ops[1].reg.size, {1, 2});
        return cat(reg_prefix(ops[1].reg), {0x70 | ops[0].code});
    }

    if (m == "LDX") {
        if (!kinds(ops, K::Mem, K::Imm) || ops[0].mode != M::Abs) throw e.err("LDX takes (#8),#");
        int64_t a = e.val(ops[0].addr).first;
        if (final && !(0 <= a && a <= 0xFF)) throw e.err("LDX address must be 8-bit");
        int64_t v = e.imm(ops[1].expr, 1);
        return {0xF7, 0, uint8_t(a & 0xFF), 0, uint8_t(v & 0xFF), 0};
    }

    if (JUMPS.count(m)) return jump(e, m, ops, jo);

    throw e.err("unknown mnemonic " + m);
}

}  // namespace unitron
