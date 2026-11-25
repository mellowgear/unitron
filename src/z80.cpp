// Z80 instruction forms, decoder and encoder (see z80.hpp). Every form below is one row or cell of
// UM0080's op code tables; `page` is the printed page of that table.
#include "z80.hpp"

#include <algorithm>
#include <map>

#include "util.hpp"

namespace unitron::z80 {

int Form::size() const { return int(tmpl.size()) + int(std::count(tmpl.begin(), tmpl.end(), T_NN)); }

bool Insn::has_target() const {
    const Form* fp = f();
    if (!fp) return false;
    for (auto& p : fp->ops)
        if (p.k == P::Rel) return true;
    if (fp->flow == Flow::Rst) return true;
    if (fp->flow == Flow::Jump || fp->flow == Flow::JumpCond || fp->flow == Flow::Call || fp->flow == Flow::CallCond)
        for (auto& p : fp->ops)
            if (p.k == P::NN) return true;
    return false;
}

namespace {

Pat L(const std::string& s) {
    Pat p;
    p.lit = s;
    return p;
}
Pat K(P k) {
    Pat p;
    p.k = k;
    return p;
}
Pat C(int v, bool hex = false) {
    Pat p;
    p.k = P::Const;
    p.v = v;
    p.hex = hex;
    return p;
}

// r: the 3-bit register field (UM0080 p.71 "LD r, r'": 111 A, 000 B, 001 C, 010 D, 011 E, 100 H, 101 L;
// 110 is (HL), p.74)
const char* R8[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
const char* SS[4] = {"BC", "DE", "HL", "SP"};    // dd / ss (p.99)
const char* QQ[4] = {"BC", "DE", "HL", "AF"};    // qq (p.115)
const char* CC[8] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};   // cc (p.263)

std::vector<Form> build() {
    std::vector<Form> v;
    auto add = [&](const std::string& m, std::vector<Pat> ops, std::vector<int> t, int page, Flow fl = Flow::Next, bool alt = false) {
        Form f;
        f.mnem = m;
        f.ops = std::move(ops);
        f.tmpl = std::move(t);
        f.flow = fl;
        f.alt = alt;
        f.page = page;
        v.push_back(std::move(f));
    };
    // ---- 8-bit load group, table 6 (p.42)
    for (int d = 0; d < 8; d++)
        for (int s = 0; s < 8; s++)
            if (!(d == 6 && s == 6)) add("LD", {L(R8[d]), L(R8[s])}, {0x40 | d << 3 | s}, 42);
    for (int d = 0; d < 8; d++) add("LD", {L(R8[d]), K(P::N)}, {0x06 | d << 3, T_N}, 42);
    for (int x = 0; x < 2; x++) {
        int pre = x ? 0xFD : 0xDD;
        P idx = x ? P::IY : P::IX;
        for (int r = 0; r < 8; r++)
            if (r != 6) add("LD", {L(R8[r]), K(idx)}, {pre, 0x46 | r << 3, T_D}, 42);
        for (int r = 0; r < 8; r++)
            if (r != 6) add("LD", {K(idx), L(R8[r])}, {pre, 0x70 | r, T_D}, 42);
        add("LD", {K(idx), K(P::N)}, {pre, 0x36, T_D, T_N}, 42);
    }
    add("LD", {L("A"), L("(BC)")}, {0x0A}, 42);
    add("LD", {L("A"), L("(DE)")}, {0x1A}, 42);
    add("LD", {L("A"), K(P::Mem)}, {0x3A, T_NN}, 42);
    add("LD", {L("(BC)"), L("A")}, {0x02}, 42);
    add("LD", {L("(DE)"), L("A")}, {0x12}, 42);
    add("LD", {K(P::Mem), L("A")}, {0x32, T_NN}, 42);
    add("LD", {L("A"), L("I")}, {0xED, 0x57}, 42);
    add("LD", {L("A"), L("R")}, {0xED, 0x5F}, 42);
    add("LD", {L("I"), L("A")}, {0xED, 0x47}, 42);
    add("LD", {L("R"), L("A")}, {0xED, 0x4F}, 42);
    // ---- 16-bit load group, table 7 (p.45)
    for (int i = 0; i < 4; i++) add("LD", {L(SS[i]), K(P::NN)}, {0x01 | i << 4, T_NN}, 45);
    add("LD", {L("IX"), K(P::NN)}, {0xDD, 0x21, T_NN}, 45);
    add("LD", {L("IY"), K(P::NN)}, {0xFD, 0x21, T_NN}, 45);
    add("LD", {L("HL"), K(P::Mem)}, {0x2A, T_NN}, 45);
    for (int i = 0; i < 4; i++) add("LD", {L(SS[i]), K(P::Mem)}, {0xED, 0x4B | i << 4, T_NN}, 45, Flow::Next, i == 2);
    add("LD", {L("IX"), K(P::Mem)}, {0xDD, 0x2A, T_NN}, 45);
    add("LD", {L("IY"), K(P::Mem)}, {0xFD, 0x2A, T_NN}, 45);
    add("LD", {K(P::Mem), L("HL")}, {0x22, T_NN}, 45);
    for (int i = 0; i < 4; i++) add("LD", {K(P::Mem), L(SS[i])}, {0xED, 0x43 | i << 4, T_NN}, 45, Flow::Next, i == 2);
    add("LD", {K(P::Mem), L("IX")}, {0xDD, 0x22, T_NN}, 45);
    add("LD", {K(P::Mem), L("IY")}, {0xFD, 0x22, T_NN}, 45);
    add("LD", {L("SP"), L("HL")}, {0xF9}, 45);
    add("LD", {L("SP"), L("IX")}, {0xDD, 0xF9}, 45);
    add("LD", {L("SP"), L("IY")}, {0xFD, 0xF9}, 45);
    for (int i = 0; i < 4; i++) add("PUSH", {L(QQ[i])}, {0xC5 | i << 4}, 45);
    add("PUSH", {L("IX")}, {0xDD, 0xE5}, 45);
    add("PUSH", {L("IY")}, {0xFD, 0xE5}, 45);
    for (int i = 0; i < 4; i++) add("POP", {L(QQ[i])}, {0xC1 | i << 4}, 45);
    add("POP", {L("IX")}, {0xDD, 0xE1}, 45);
    add("POP", {L("IY")}, {0xFD, 0xE1}, 45);
    // ---- exchanges, table 8 (p.47)
    add("EX", {L("DE"), L("HL")}, {0xEB}, 47);
    add("EX", {L("AF"), L("AF'")}, {0x08}, 47);
    add("EXX", {}, {0xD9}, 47);
    add("EX", {L("(SP)"), L("HL")}, {0xE3}, 47);
    add("EX", {L("(SP)"), L("IX")}, {0xDD, 0xE3}, 47);
    add("EX", {L("(SP)"), L("IY")}, {0xFD, 0xE3}, 47);
    // ---- block transfer and search, tables 9 and 10 (p.48-49)
    add("LDI", {}, {0xED, 0xA0}, 48);
    add("LDIR", {}, {0xED, 0xB0}, 48);
    add("LDD", {}, {0xED, 0xA8}, 48);
    add("LDDR", {}, {0xED, 0xB8}, 48);
    add("CPI", {}, {0xED, 0xA1}, 49);
    add("CPIR", {}, {0xED, 0xB1}, 49);
    add("CPD", {}, {0xED, 0xA9}, 49);
    add("CPDR", {}, {0xED, 0xB9}, 49);
    // ---- 8-bit arithmetic and logic, table 11 (p.50). ADD, ADC and SBC name A; the others don't.
    const char* ALU[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
    for (int o = 0; o < 8; o++) {
        bool a = o == 0 || o == 1 || o == 3;
        auto ops = [&](Pat p) { return a ? std::vector<Pat>{L("A"), p} : std::vector<Pat>{p}; };
        for (int r = 0; r < 8; r++) add(ALU[o], ops(L(R8[r])), {0x80 | o << 3 | r}, 50);
        add(ALU[o], ops(K(P::N)), {0xC6 | o << 3, T_N}, 50);
        add(ALU[o], ops(K(P::IX)), {0xDD, 0x86 | o << 3, T_D}, 50);
        add(ALU[o], ops(K(P::IY)), {0xFD, 0x86 | o << 3, T_D}, 50);
    }
    for (int r = 0; r < 8; r++) add("INC", {L(R8[r])}, {0x04 | r << 3}, 50);
    add("INC", {K(P::IX)}, {0xDD, 0x34, T_D}, 50);
    add("INC", {K(P::IY)}, {0xFD, 0x34, T_D}, 50);
    for (int r = 0; r < 8; r++) add("DEC", {L(R8[r])}, {0x05 | r << 3}, 50);
    add("DEC", {K(P::IX)}, {0xDD, 0x35, T_D}, 50);
    add("DEC", {K(P::IY)}, {0xFD, 0x35, T_D}, 50);
    // ---- general-purpose AF operations, table 12 (p.51)
    add("DAA", {}, {0x27}, 51);
    add("CPL", {}, {0x2F}, 51);
    add("NEG", {}, {0xED, 0x44}, 51);
    add("CCF", {}, {0x3F}, 51);
    add("SCF", {}, {0x37}, 51);
    // ---- 16-bit arithmetic, table 13 (p.52)
    for (int i = 0; i < 4; i++) add("ADD", {L("HL"), L(SS[i])}, {0x09 | i << 4}, 52);
    for (int i = 0; i < 4; i++) add("ADC", {L("HL"), L(SS[i])}, {0xED, 0x4A | i << 4}, 52);
    for (int i = 0; i < 4; i++) add("SBC", {L("HL"), L(SS[i])}, {0xED, 0x42 | i << 4}, 52);
    const char* PP[4] = {"BC", "DE", "IX", "SP"};
    const char* RR[4] = {"BC", "DE", "IY", "SP"};
    for (int i = 0; i < 4; i++) add("ADD", {L("IX"), L(PP[i])}, {0xDD, 0x09 | i << 4}, 52);
    for (int i = 0; i < 4; i++) add("ADD", {L("IY"), L(RR[i])}, {0xFD, 0x09 | i << 4}, 52);
    for (int i = 0; i < 4; i++) add("INC", {L(SS[i])}, {0x03 | i << 4}, 52);
    add("INC", {L("IX")}, {0xDD, 0x23}, 52);
    add("INC", {L("IY")}, {0xFD, 0x23}, 52);
    for (int i = 0; i < 4; i++) add("DEC", {L(SS[i])}, {0x0B | i << 4}, 52);
    add("DEC", {L("IX")}, {0xDD, 0x2B}, 52);
    add("DEC", {L("IY")}, {0xFD, 0x2B}, 52);
    // ---- rotate and shift (p.53-54, figure "Rotates and Shifts"); 30-37 after CB is not documented
    add("RLCA", {}, {0x07}, 53);
    add("RLA", {}, {0x17}, 53);
    add("RRCA", {}, {0x0F}, 53);
    add("RRA", {}, {0x1F}, 53);
    const char* ROT[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", nullptr, "SRL"};
    for (int o = 0; o < 8; o++) {
        if (!ROT[o]) continue;
        for (int r = 0; r < 8; r++) add(ROT[o], {L(R8[r])}, {0xCB, o << 3 | r}, 53);
        add(ROT[o], {K(P::IX)}, {0xDD, 0xCB, T_D, o << 3 | 6}, 53);
        add(ROT[o], {K(P::IY)}, {0xFD, 0xCB, T_D, o << 3 | 6}, 53);
    }
    add("RLD", {}, {0xED, 0x6F}, 54);
    add("RRD", {}, {0xED, 0x67}, 54);
    // ---- bit set, reset and test, table 14 (p.55-57)
    const char* BITOP[3] = {"BIT", "RES", "SET"};
    for (int o = 0; o < 3; o++)
        for (int b = 0; b < 8; b++) {
            int base = (o + 1) << 6 | b << 3;
            for (int r = 0; r < 8; r++) add(BITOP[o], {C(b), L(R8[r])}, {0xCB, base | r}, 55);
            add(BITOP[o], {C(b), K(P::IX)}, {0xDD, 0xCB, T_D, base | 6}, 55);
            add(BITOP[o], {C(b), K(P::IY)}, {0xFD, 0xCB, T_D, base | 6}, 55);
        }
    // ---- jump, call and return, table 15 (p.59)
    add("JP", {K(P::NN)}, {0xC3, T_NN}, 59, Flow::Jump);
    for (int c = 0; c < 8; c++) add("JP", {L(CC[c]), K(P::NN)}, {0xC2 | c << 3, T_NN}, 59, Flow::JumpCond);
    add("JR", {K(P::Rel)}, {0x18, T_E}, 59, Flow::Jump);
    for (int c = 0; c < 4; c++) add("JR", {L(CC[c]), K(P::Rel)}, {0x20 | c << 3, T_E}, 59, Flow::JumpCond);
    add("JP", {L("(HL)")}, {0xE9}, 59, Flow::JumpInd);
    add("JP", {L("(IX)")}, {0xDD, 0xE9}, 59, Flow::JumpInd);
    add("JP", {L("(IY)")}, {0xFD, 0xE9}, 59, Flow::JumpInd);
    add("CALL", {K(P::NN)}, {0xCD, T_NN}, 59, Flow::Call);
    for (int c = 0; c < 8; c++) add("CALL", {L(CC[c]), K(P::NN)}, {0xC4 | c << 3, T_NN}, 59, Flow::CallCond);
    add("DJNZ", {K(P::Rel)}, {0x10, T_E}, 59, Flow::JumpCond);
    add("RET", {}, {0xC9}, 59, Flow::Ret);
    for (int c = 0; c < 8; c++) add("RET", {L(CC[c])}, {0xC0 | c << 3}, 59, Flow::RetCond);
    add("RETI", {}, {0xED, 0x4D}, 59, Flow::Ret);
    add("RETN", {}, {0xED, 0x45}, 59, Flow::Ret);
    // ---- restart, table 17 (p.61)
    for (int p = 0; p < 8; p++) add("RST", {C(p << 3, true)}, {0xC7 | p << 3}, 61, Flow::Rst);
    // ---- input and output, tables 18 and 19 (p.62-63); r = (HL) (ED 70, ED 71) is not documented
    add("IN", {L("A"), K(P::Port)}, {0xDB, T_N}, 62);
    for (int r = 0; r < 8; r++)
        if (r != 6) add("IN", {L(R8[r]), L("(C)")}, {0xED, 0x40 | r << 3}, 62);
    add("INI", {}, {0xED, 0xA2}, 62);
    add("INIR", {}, {0xED, 0xB2}, 62);
    add("IND", {}, {0xED, 0xAA}, 62);
    add("INDR", {}, {0xED, 0xBA}, 62);
    add("OUT", {K(P::Port), L("A")}, {0xD3, T_N}, 63);
    for (int r = 0; r < 8; r++)
        if (r != 6) add("OUT", {L("(C)"), L(R8[r])}, {0xED, 0x41 | r << 3}, 63);
    add("OUTI", {}, {0xED, 0xA3}, 63);
    add("OTIR", {}, {0xED, 0xB3}, 63);
    add("OUTD", {}, {0xED, 0xAB}, 63);
    add("OTDR", {}, {0xED, 0xBB}, 63);
    // ---- miscellaneous CPU control, table 20 (p.64)
    add("NOP", {}, {0x00}, 64);
    add("HALT", {}, {0x76}, 64);
    add("DI", {}, {0xF3}, 64);
    add("EI", {}, {0xFB}, 64);
    add("IM", {C(0)}, {0xED, 0x46}, 64);
    add("IM", {C(1)}, {0xED, 0x56}, 64);
    add("IM", {C(2)}, {0xED, 0x5E}, 64);
    return v;
}

// decode key: the fixed bytes of a form, packed (at most 3: DD CB op)
uint32_t key_of(const std::vector<int>& t) {
    uint32_t k = 0;
    int n = 0;
    for (int b : t)
        if (b >= 0) {
            k = k << 8 | uint32_t(b);
            n++;
        }
    return k | uint32_t(n) << 24;
}

const std::map<uint32_t, int>& decode_map() {
    static const std::map<uint32_t, int> m = [] {
        std::map<uint32_t, int> r;
        auto& fs = forms();
        for (size_t i = 0; i < fs.size(); i++) r.emplace(key_of(fs[i].tmpl), int(i));
        return r;
    }();
    return m;
}

}  // namespace

const std::vector<Form>& forms() {
    static const std::vector<Form> v = build();
    return v;
}

Insn decode(const MemFn& mem, int64_t addr) {
    Insn ins;
    ins.addr = addr;
    int b0 = mem(addr);
    if (b0 < 0) {
        ins.size = 0;
        return ins;
    }
    ins.raw = {uint8_t(b0)};
    std::vector<int> fixed;
    int b1 = mem(addr + 1);
    if (b0 == 0xCB || b0 == 0xED) {
        if (b1 < 0) return ins;
        fixed = {b0, b1};
    } else if (b0 == 0xDD || b0 == 0xFD) {
        if (b1 < 0) return ins;
        if (b1 == 0xCB) {
            int b3 = mem(addr + 3);
            if (b3 < 0) return ins;
            fixed = {b0, b1, b3};
        } else {
            fixed = {b0, b1};
        }
    } else {
        fixed = {b0};
    }
    auto& dm = decode_map();
    auto it = dm.find(key_of(fixed));
    if (it == dm.end()) return ins;
    const Form& f = forms()[size_t(it->second)];
    Bytes raw;
    int64_t a = addr;
    for (int t : f.tmpl) {
        int b = mem(a);
        if (b < 0) return ins;   // runs off the end
        raw.push_back(uint8_t(b));
        a++;
        if (t == T_D) ins.d = int8_t(uint8_t(b));
        else if (t == T_N) ins.n = b;
        else if (t == T_E) ins.target = (addr + int64_t(f.size()) + int8_t(uint8_t(b))) & 0xFFFF;
        else if (t == T_NN) {
            int hi = mem(a);
            if (hi < 0) return ins;
            raw.push_back(uint8_t(hi));
            a++;
            ins.nn = b | hi << 8;
        }
    }
    ins.form = it->second;
    ins.size = int(raw.size());
    ins.raw = raw;
    if (f.flow == Flow::Rst) ins.target = f.ops[0].v;
    else if (f.flow != Flow::Next)
        for (auto& p : f.ops)
            if (p.k == P::NN) ins.target = ins.nn;
    return ins;
}

Insn decode_bytes(const Bytes& b, int64_t addr) {
    return decode([&](int64_t a) { return a >= addr && a - addr < int64_t(b.size()) ? int(b[size_t(a - addr)]) : -1; }, addr);
}

std::string hexz(int64_t v, int digits) {
    std::string s = hexs(v, digits);
    if (!py_digit(s[0])) s = "0" + s;
    return s + "H";
}

std::string operand_text(const Pat& p, const Insn& ins, const NameFn* name) {
    auto nm = [&](int64_t a, char k) { return name ? (*name)(a, k) : std::string(); };
    switch (p.k) {
    case P::Lit: return p.lit;
    case P::N: return hexz(ins.n, 2);
    case P::Port: return "(" + hexz(ins.n, 2) + ")";
    case P::NN: {
        const Form* f = ins.f();
        bool code = f && f->flow != Flow::Next;
        std::string s = nm(ins.nn, code ? 'c' : 'i');
        return s.empty() ? hexz(ins.nn, 4) : s;
    }
    case P::Mem: {
        std::string s = nm(ins.nn, 'm');
        return "(" + (s.empty() ? hexz(ins.nn, 4) : s) + ")";
    }
    case P::Rel: {
        std::string s = nm(ins.target, 'r');
        if (!s.empty()) return s;
        int64_t off = int8_t(uint8_t(ins.raw.back())) + 2;   // $ is the instruction's own address
        return off == 0 ? "$" : off > 0 ? "$+" + std::to_string(off) : "$-" + std::to_string(-off);
    }
    case P::IX:
    case P::IY: {
        std::string r = p.k == P::IX ? "IX" : "IY";
        return "(" + r + (ins.d < 0 ? "-" + hexz(-ins.d, 2) : "+" + hexz(ins.d, 2)) + ")";
    }
    case P::Const: return p.hex ? hexz(p.v, 2) : std::to_string(p.v);
    }
    return "?";
}

std::string text(const Insn& ins, const NameFn* name) {
    const Form* f = ins.f();
    if (!f) return "?";
    std::string s = f->mnem;
    for (size_t i = 0; i < f->ops.size(); i++) s += (i ? "," : "\t") + operand_text(f->ops[i], ins, name);
    return s;
}

Bytes encode(const Form& f, int64_t n, int64_t nn, int64_t d, int64_t target, int64_t pc) {
    Bytes b;
    for (int t : f.tmpl) {
        if (t >= 0) b.push_back(uint8_t(t));
        else if (t == T_D) b.push_back(uint8_t(d & 0xFF));
        else if (t == T_N) b.push_back(uint8_t(n & 0xFF));
        else if (t == T_NN) {
            b.push_back(uint8_t(nn & 0xFF));
            b.push_back(uint8_t(nn >> 8 & 0xFF));
        } else if (t == T_E) {
            b.push_back(uint8_t((target - (pc + f.size())) & 0xFF));
        }
    }
    return b;
}

}  // namespace unitron::z80
