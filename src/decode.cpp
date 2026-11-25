#include "decode.hpp"

#include <algorithm>

#include "regs.hpp"

namespace unitron {

std::string hx(int64_t v) { return v > 9 ? "0x" + hexs(v) : dec(v); }

std::string Insn::text() const {
    std::string m = lower(mnem);
    if (ops.empty()) return m;
    std::string s = m + "\t";
    for (size_t i = 0; i < ops.size(); i++) {
        if (i) s += ",";
        s += ops[i].text;
    }
    return s;
}

namespace {

int64_t s8(int64_t v) { return v & 0x80 ? v - 0x100 : v; }
int64_t s16(int64_t v) { return v & 0x8000 ? v - 0x10000 : v; }

struct Eof {};

struct Rd {
    const MemFn& mem;
    int64_t pc;
    int64_t u8() {
        int v = mem(pc);
        if (v < 0) throw Eof();
        pc++;
        return v;
    }
    int64_t u16() {
        int64_t a = u8();
        return a | (u8() << 8);
    }
    int64_t u24() {
        int64_t a = u16();
        return a | (u8() << 16);
    }
    int64_t u32() {
        int64_t a = u16();
        return a | (u16() << 16);
    }
    int64_t imm(int size) { return size == 1 ? u8() : size == 2 ? u16() : u32(); }
};

void bad(Insn& ins, const std::string& why) {
    if (ins.illegal.empty()) ins.illegal = why;
}

std::string reg_ext(Insn& ins, int code, int size) {
    std::string n = ext_reg(code, size);
    if (n.empty()) {
        bad(ins, "undefined register code 0x" + hexs(code, 2));
        return "r?" + hexs(code, 2);
    }
    if ((size == 2 && (code & 1)) || (size == 4 && (code & 3))) bad(ins, "register code 0x" + hexs(code, 2) + " has low bits set");
    return n;
}

std::string pm(int64_t d) { return std::string(d >= 0 ? "+" : "-") + hx(d < 0 ? -d : d); }

// Memory operand selected by prefix byte p (text with parentheses).
Op mem_operand(Rd& rd, int p, int opsize, Insn& ins) {
    if (p < 0xC0) {
        std::string r = REG32[p & 7];
        if (p & 8) {
            int64_t d = s8(rd.u8());
            return "(" + r + pm(d) + ")";
        }
        return "(" + r + ")";
    }
    int mode = p & 7;
    if (mode <= 2) {
        int64_t a = mode == 0 ? rd.u8() : mode == 1 ? rd.u16() : rd.u24();
        ins.mem_refs.push_back(a);
        return Op::Ref("(" + hx(a) + ")", a, 'm');
    }
    if (mode == 3) {
        int b = int(rd.u8());
        int m = b & 3;
        if (m == 0) return "(" + reg_ext(ins, b & 0xFC, 4) + ")";
        if (m == 1) {
            int64_t d = s16(rd.u16());
            std::string r = reg_ext(ins, b & 0xFC, 4);
            return "(" + r + pm(d) + ")";
        }
        if (b == 0x03 || b == 0x07) {
            int r = int(rd.u8()), x = int(rd.u8());
            std::string rn = reg_ext(ins, r, 4);
            std::string xn = reg_ext(ins, x, b == 3 ? 1 : 2);
            return "(" + rn + "+" + xn + ")";
        }
        ins.invalid = true;
        return "(?)";
    }
    if (mode == 4 || mode == 5) {
        int b = int(rd.u8());
        int inc = (b & 3) == 0 ? 1 : (b & 3) == 1 ? 2 : (b & 3) == 2 ? 4 : 0;
        if (!inc) ins.invalid = true;
        std::string r = reg_ext(ins, b & 0xFC, 4);
        std::string note = inc == (opsize ? opsize : 1) ? "" : ":" + dec(inc);
        return mode == 4 ? "(-" + r + note + ")" : "(" + r + "+" + note + ")";
    }
    ins.invalid = true;
    return "(?)";
}

std::string dbl(int R, int size) { return size == 1 ? REG16[R >> 1] : REG32[R]; }
std::string regname(int R, int size) { return (size == 1 ? REG8 : size == 2 ? REG16 : REG32)[R]; }

void set(Insn& ins, const std::string& mnem, std::vector<Op> ops = {}) {
    ins.mnem = mnem;
    ins.ops = std::move(ops);
}

const char* const MULN[4] = {"MUL", "MULS", "DIV", "DIVS"};
const char* const CFN[5] = {"ANDCF", "ORCF", "XORCF", "LDCF", "STCF"};

void decode_reg(Rd& rd, int p, Insn& ins) {
    int size = (p >> 4) == 0xC ? 1 : (p >> 4) == 0xD ? 2 : 4;
    std::string r, rr;
    if ((p & 0xF) == 7) {
        int code = int(rd.u8());
        r = reg_ext(ins, code, size);
        if (size < 4) {
            rr = ext_reg(code & (size == 1 ? 0xFE : 0xFC), size * 2);
            if (rr.empty()) rr = "?";
        } else {
            rr = "?";
        }
    } else {
        int idx = p & 7;
        r = regname(idx, size);
        rr = size < 4 ? dbl(idx, size) : "?";
    }
    int op = int(rd.u8());
    int R = op & 7;
    std::string Rn = regname(R, size);

    if (op == 0x03) {
        int64_t v = rd.imm(size);
        set(ins, "LD", {r, hx(v)});
        if (size == 4) {
            ins.has_imm32 = true;
            ins.imm32 = v;
            ins.dst_reg = r;
        }
    } else if (op == 0x04 || op == 0x05) {
        set(ins, op == 4 ? "PUSH" : "POP", {r});
    } else if (op == 0x06 || op == 0x07) {
        set(ins, op == 6 ? "CPL" : "NEG", {r});
        if (size == 4) bad(ins, "CPL/NEG are byte/word only");
    } else if (op >= 0x08 && op <= 0x0B && size < 4) {
        std::string v = hx(rd.imm(size));
        set(ins, MULN[op - 8], {rr, v});
    } else if (op == 0x0C && size == 4) {
        set(ins, "LINK", {r, dec(s16(rd.u16()))});
    } else if (op == 0x0D && size == 4) {
        set(ins, "UNLK", {r});
    } else if ((op == 0x0E || op == 0x0F) && size == 2) {
        set(ins, op == 0x0E ? "BS1F" : "BS1B", {"A", r});
    } else if (op == 0x10 && size == 1) {
        set(ins, "DAA", {r});
    } else if ((op == 0x12 || op == 0x13 || op == 0x14) && size > 1) {
        static const char* const ex[3] = {"EXTZ", "EXTS", "PAA"};
        set(ins, ex[op - 0x12], {r});
    } else if (op == 0x16 && size == 2) {
        set(ins, "MIRR", {r});
    } else if (op == 0x19 && size == 2) {
        set(ins, "MULA", {r});
    } else if (op == 0x1C && size < 4) {
        int64_t d = s8(rd.u8());
        ins.has_target = true;
        ins.target = (rd.pc + d) & 0xFFFFFF;
        ins.is_jump = ins.conditional = true;
        set(ins, "DJNZ", {r, Op::Ref(hx(ins.target), ins.target, 'c')});
    } else if (op >= 0x20 && op <= 0x24 && size < 4) {
        int64_t b = rd.u8();
        if (b > 15) bad(ins, "bit number above 15");
        set(ins, CFN[op - 0x20], {hx(b), r});
    } else if (op >= 0x28 && op <= 0x2C && size < 4) {
        set(ins, CFN[op - 0x28], {"A", r});
    } else if (op == 0x2E || op == 0x2F) {
        int c = int(rd.u8());
        const char* crn = cr_name(c, size);
        std::string cr;
        if (!crn) {
            cr = "cr" + hexs(c, 2);
            bad(ins, "undefined control register 0x" + hexs(c, 2));
        } else {
            cr = crn;
        }
        if (op == 0x2E) set(ins, "LDC", {cr, r});
        else set(ins, "LDC", {r, cr});
    } else if (op >= 0x30 && op <= 0x34 && size < 4) {
        int64_t b = rd.u8();
        if (b > 15) bad(ins, "bit number above 15");
        static const char* const bn[5] = {"RES", "SET", "CHG", "BIT", "TSET"};
        set(ins, bn[op - 0x30], {hx(b), r});
    } else if ((op == 0x38 || op == 0x39 || op == 0x3A || op == 0x3C || op == 0x3D || op == 0x3E) && size == 2) {
        int nn = (op & 3) == 0 ? 1 : (op & 3) == 1 ? 2 : 4;
        int64_t v = (rd.u16() + nn) & 0xFFFF;
        set(ins, std::string(op < 0x3C ? "MINC" : "MDEC") + dec(nn), {hx(v), r});
    } else if (op >= 0x40 && op <= 0x5F && size < 4 && (size == 2 || (R & 1))) {
        set(ins, MULN[(op - 0x40) >> 3], {dbl(R, size), r});
    } else if (op >= 0x60 && op <= 0x6F) {
        set(ins, op < 0x68 ? "INC" : "DEC", {dec(R ? R : 8), r});
    } else if (op >= 0x70 && op <= 0x7F && size < 4) {
        set(ins, "SCC", {CC[op & 0xF], r});
    } else if (op >= 0x80) {
        int hi = op >> 4, lo8 = op & 8;
        if (!lo8) {
            set(ins, ALU[hi - 8], {Rn, r});
        } else if (hi == 8) {
            set(ins, "LD", {Rn, r});
        } else if (hi == 9) {
            set(ins, "LD", {r, Rn});
        } else if (hi == 0xA) {
            set(ins, "LD", {r, dec(R)});
        } else if (hi == 0xB) {
            if (size == 4) ins.invalid = true;
            set(ins, "EX", {Rn, r});
        } else if (hi == 0xC) {
            std::string v = hx(rd.imm(size));
            set(ins, ALU[R], {r, v});
        } else if (hi == 0xD) {
            set(ins, "CP", {r, dec(R)});
            if (size == 4) bad(ins, "CP r,#3 is byte/word only");
        } else if (hi == 0xE) {
            int64_t b = rd.u8();
            if (b > 15) bad(ins, "shift count byte above 15");
            set(ins, SHIFT[R], {dec((b & 0xF) ? (b & 0xF) : 16), r});
        } else {
            set(ins, SHIFT[R], {"A", r});
        }
    } else {
        ins.invalid = true;
    }
}

void decode_src(Rd& rd, int p, Insn& ins) {
    int g = p >> 4;
    int size = (g == 0x8 || g == 0xC) ? 1 : (g == 0x9 || g == 0xD) ? 2 : 4;
    Op mem = mem_operand(rd, p, size, ins);
    int op = int(rd.u8());
    int R = op & 7;
    std::string Rn = regname(R, size);
    std::string sfx(1, sfx_char(size));
    bool bw = size < 4;

    if (op == 0x04 && bw) {
        set(ins, "PUSH" + sfx, {mem});
    } else if ((op == 0x06 || op == 0x07) && size == 1) {
        set(ins, op == 0x06 ? "RLD" : "RRD", {"A", mem});
    } else if (op >= 0x10 && op <= 0x17 && bw && p < 0xA0 && (p & 8) == 0 && (op >= 0x14 || (p & 7) == 3 || (p & 7) == 5)) {
        static const char* const names[8] = {"LDI", "LDIR", "LDD", "LDDR", "CPI", "CPIR", "CPD", "CPDR"};
        std::string name = names[op - 0x10];
        if (size == 2) name += "W";
        std::string d = (op == 0x10 || op == 0x11 || op == 0x14 || op == 0x15) ? "+" : "-";
        if (op < 0x14) {
            std::string a = (p & 7) == 3 ? "XDE" : "XIX", b = (p & 7) == 3 ? "XHL" : "XIY";
            set(ins, name, {"(" + a + d + ")", "(" + b + d + ")"});
        } else {
            set(ins, name, {size == 1 ? "A" : "WA", "(" + std::string(REG32[p & 7]) + d + ")"});
        }
    } else if (op == 0x19 && bw) {
        int64_t a = rd.u16();
        ins.mem_refs.push_back(a);
        set(ins, "LD" + sfx, {Op::Ref("(" + hx(a) + ")", a, 'm'), mem});
    } else if (op >= 0x20 && op <= 0x27) {
        set(ins, "LD", {Rn, mem});
    } else if (op >= 0x30 && op <= 0x37 && bw) {
        set(ins, "EX", {mem, Rn});
    } else if (op >= 0x38 && op <= 0x3F && bw) {
        std::string v = hx(rd.imm(size));
        set(ins, ALU[R] + sfx, {mem, v});
    } else if (op >= 0x40 && op <= 0x5F && bw && (size == 2 || (R & 1))) {
        set(ins, MULN[(op - 0x40) >> 3], {dbl(R, size), mem});
    } else if (op >= 0x60 && op <= 0x6F && bw) {
        set(ins, std::string(op < 0x68 ? "INC" : "DEC") + sfx, {dec(R ? R : 8), mem});
    } else if (op >= 0x78 && op <= 0x7F && bw) {
        set(ins, SHIFT[R] + sfx, {mem});
    } else if (op >= 0x80) {
        std::string name = ALU[(op >> 4) - 8];
        if (!(op & 8)) set(ins, name, {Rn, mem});
        else set(ins, name, {mem, Rn});
    } else {
        ins.invalid = true;
    }
}

Op bare(const Op& m) {
    Op o = m;
    o.text = m.text.substr(1, m.text.size() - 2);
    return o;
}

std::string respell_step(const std::string& mem, int size) {
    if (starts_with(mem, "(-")) {
        std::string body = mem.substr(2, mem.size() - 3);
        size_t k = body.find(':');
        std::string r = k == std::string::npos ? body : body.substr(0, k);
        int n = k == std::string::npos || k + 1 == body.size() ? 1 : std::stoi(body.substr(k + 1));
        return "(-" + r + (n == size ? "" : ":" + dec(n)) + ")";
    }
    if (ends_with(mem, "+)") || mem.find("+:") != std::string::npos) {
        std::string body = mem.substr(1, mem.size() - 2);
        size_t k = body.find('+');
        std::string r = k == std::string::npos ? body : body.substr(0, k);
        std::string nn = k == std::string::npos ? "" : body.substr(k + 1);
        int n = starts_with(nn, ":") ? std::stoi(nn.substr(1)) : 1;
        return "(" + r + "+" + (n == size ? "" : ":" + dec(n)) + ")";
    }
    return mem;
}

void decode_dst(Rd& rd, int p, Insn& ins) {
    if (p == 0xF3 && rd.mem(rd.pc) == 0x13) {
        // LDAR R,$+4+d16: F3 13 d16 (20+R | 30+R) [datasheet LDAR, "$ + 4 + d16"]
        rd.u8();
        int64_t d = s16(rd.u16());
        int64_t base = rd.pc;
        int op = int(rd.u8());
        int64_t tgt = (base + d) & 0xFFFFFF;
        int size = (op & 0xF8) == 0x20 ? 2 : (op & 0xF8) == 0x30 ? 4 : 0;
        if (!size) {
            ins.invalid = true;
            return;
        }
        ins.mem_refs.push_back(tgt);
        set(ins, "LDAR", {regname(op & 7, size), Op::Ref(hx(tgt), tgt, 'm')});
        return;
    }
    Op mem = mem_operand(rd, p, 0, ins);
    int op = int(rd.u8());
    int R = op & 7;
    int dsize = 0;
    switch (op) {
        case 0x00: case 0x04: case 0x14: dsize = 1; break;
        case 0x02: case 0x06: case 0x16: dsize = 2; break;
    }
    if (!dsize) dsize = (op >> 4) == 4 ? 1 : (op >> 4) == 5 ? 2 : (op >> 4) == 6 ? 4 : 0;
    if (!dsize && ((op >= 0x80 && op <= 0xCF) || (op >= 0x28 && op <= 0x2C))) dsize = 1;
    if (dsize && dsize != 1 && !mem.ref) mem.text = respell_step(mem.text, dsize);
    if (op == 0x00) {
        set(ins, "LDB", {mem, hx(rd.u8())});
    } else if (op == 0x02) {
        set(ins, "LDW", {mem, hx(rd.u16())});
    } else if (op == 0x04) {
        set(ins, "POPB", {mem});
    } else if (op == 0x06) {
        set(ins, "POPW", {mem});
    } else if (op == 0x14 || op == 0x16) {
        int64_t a = rd.u16();
        ins.mem_refs.push_back(a);
        set(ins, op == 0x14 ? "LDB" : "LDW", {mem, Op::Ref("(" + hx(a) + ")", a, 'm')});
    } else if (op >= 0x20 && op <= 0x27) {
        set(ins, "LDA", {REG16[R], bare(mem)});
    } else if (op >= 0x30 && op <= 0x37) {
        set(ins, "LDA", {REG32[R], bare(mem)});
    } else if (op >= 0x28 && op <= 0x2C) {
        set(ins, CFN[op - 0x28], {"A", mem});
    } else if (op >= 0x40 && op <= 0x47) {
        set(ins, "LD", {mem, REG8[R]});
    } else if (op >= 0x50 && op <= 0x57) {
        set(ins, "LD", {mem, REG16[R]});
    } else if (op >= 0x60 && op <= 0x67) {
        set(ins, "LD", {mem, REG32[R]});
    } else if (op >= 0x80 && op <= 0xCF) {
        static const char* const names[10] = {"ANDCF", "ORCF", "XORCF", "LDCF", "STCF", "TSET", "RES", "SET", "CHG", "BIT"};
        set(ins, names[(op - 0x80) >> 3], {dec(R), mem});
    } else if (op >= 0xD0 && op <= 0xEF) {
        int cc = op & 0xF;
        bool call = op >= 0xE0;
        ins.is_call = call;
        ins.is_jump = !call;
        ins.conditional = cc != 8;
        ins.ends_block = cc == 8 && !call;
        Op tgt;
        if (mem.ref) {
            ins.has_target = true;
            ins.target = mem.addr;
            auto it = std::find(ins.mem_refs.begin(), ins.mem_refs.end(), mem.addr);
            if (it != ins.mem_refs.end()) ins.mem_refs.erase(it);
            tgt = Op::Ref(hx(mem.addr), mem.addr, 'c');
        } else {
            tgt = bare(mem);
        }
        if (cc == 8) set(ins, call ? "CALL" : "JP", {tgt});
        else set(ins, call ? "CALL" : "JP", {CC[cc], tgt});
    } else if (op >= 0xF0 && p == 0xB0) {
        int cc = op & 0xF;
        ins.ends_block = cc == 8;
        ins.conditional = cc != 8;
        if (cc == 8) set(ins, "RET");
        else set(ins, "RET", {CC[cc]});
    } else {
        ins.invalid = true;
    }
}

}  // namespace

Insn decode(const MemFn& mem, int64_t addr) {
    Rd rd{mem, addr};
    Insn ins;
    ins.addr = addr;
    auto code_ref = [&](int64_t t) {
        t &= 0xFFFFFF;
        ins.has_target = true;
        ins.target = t;
        return Op::Ref(hx(t), t, 'c');
    };
    try {
        int p = int(rd.u8());
        if (p == 0x00) {
            set(ins, "NOP");
        } else if (p == 0x01 || p == 0x04) {
            set(ins, p == 1 ? "NORMAL" : "MAX");
            bad(ins, "900 minimum/maximum mode switch; undefined on 900/H");
        } else if (p == 0x02) {
            set(ins, "PUSH", {"SR"});
        } else if (p == 0x03) {
            set(ins, "POP", {"SR"});
        } else if (p == 0x05) {
            set(ins, "HALT");
        } else if (p == 0x06) {
            int64_t n = rd.u8();
            if (n > 7) bad(ins, "EI level above 7");
            if (n == 7) set(ins, "DI");
            else set(ins, "EI", {dec(n)});
        } else if (p == 0x07) {
            set(ins, "RETI");
            ins.ends_block = true;
        } else if (p == 0x08) {
            int64_t a = rd.u8();
            int64_t v = rd.u8();
            ins.mem_refs.push_back(a);
            set(ins, "LDB", {Op::Ref("(" + hx(a) + ")", a, 'm'), hx(v)});
        } else if (p == 0x09) {
            set(ins, "PUSH", {hx(rd.u8())});
        } else if (p == 0x0A) {
            int64_t a = rd.u8();
            int64_t v = rd.u16();
            ins.mem_refs.push_back(a);
            set(ins, "LDW", {Op::Ref("(" + hx(a) + ")", a, 'm'), hx(v)});
        } else if (p == 0x0B) {
            set(ins, "PUSHW", {hx(rd.u16())});
        } else if (p == 0x0C) {
            set(ins, "INCF");
        } else if (p == 0x0D) {
            set(ins, "DECF");
        } else if (p == 0x0E) {
            set(ins, "RET");
            ins.ends_block = true;
        } else if (p == 0x0F) {
            set(ins, "RETD", {dec(s16(rd.u16()))});
            ins.ends_block = true;
        } else if (p >= 0x10 && p <= 0x13) {
            static const char* const n[4] = {"RCF", "SCF", "CCF", "ZCF"};
            set(ins, n[p - 0x10]);
        } else if (p == 0x14) {
            set(ins, "PUSH", {"A"});
        } else if (p == 0x15) {
            set(ins, "POP", {"A"});
        } else if (p == 0x16) {
            set(ins, "EX", {"F", "F'"});
        } else if (p == 0x17) {
            int64_t n = rd.u8();
            if (n > 7) bad(ins, "LDF bank above 7");
            set(ins, "LDF", {dec(n)});
        } else if (p == 0x18) {
            set(ins, "PUSH", {"F"});
        } else if (p == 0x19) {
            set(ins, "POP", {"F"});
        } else if (p >= 0x1A && p <= 0x1D) {
            int64_t a = (p == 0x1A || p == 0x1C) ? rd.u16() : rd.u24();
            bool call = p >= 0x1C;
            ins.is_call = call;
            ins.is_jump = !call;
            ins.ends_block = !call;
            set(ins, call ? "CALL" : "JP", {code_ref(a)});
        } else if (p == 0x1E) {
            int64_t d = s16(rd.u16());
            ins.is_call = true;
            set(ins, "CALR", {code_ref(rd.pc + d)});
        } else if (p >= 0x20 && p <= 0x27) {
            set(ins, "LD", {REG8[p & 7], hx(rd.u8())});
        } else if (p >= 0x28 && p <= 0x2F) {
            set(ins, "PUSH", {REG16[p & 7]});
        } else if (p >= 0x30 && p <= 0x37) {
            set(ins, "LD", {REG16[p & 7], hx(rd.u16())});
        } else if (p >= 0x38 && p <= 0x3F) {
            set(ins, "PUSH", {REG32[p & 7]});
        } else if (p >= 0x40 && p <= 0x47) {
            int64_t v = rd.u32();
            ins.has_imm32 = true;
            ins.imm32 = v;
            ins.dst_reg = REG32[p & 7];
            set(ins, "LD", {REG32[p & 7], hx(v)});
        } else if (p >= 0x48 && p <= 0x4F) {
            set(ins, "POP", {REG16[p & 7]});
        } else if (p >= 0x58 && p <= 0x5F) {
            set(ins, "POP", {REG32[p & 7]});
        } else if (p >= 0x60 && p <= 0x7F) {
            int64_t d = p < 0x70 ? s8(rd.u8()) : s16(rd.u16());
            int cc = p & 0xF;
            ins.is_jump = true;
            ins.conditional = cc != 8;
            ins.ends_block = cc == 8;
            Op t = code_ref(rd.pc + d);
            const char* mn = p < 0x70 ? "JR" : "JRL";
            if (cc == 8) set(ins, mn, {t});
            else set(ins, mn, {CC[cc], t});
        } else if ((p & 0xF0) == 0x80 || (p & 0xF0) == 0x90 || (p & 0xF0) == 0xA0 ||
                   (((p & 0xF0) == 0xC0 || (p & 0xF0) == 0xD0 || (p & 0xF0) == 0xE0) && (p & 0xF) <= 5)) {
            decode_src(rd, p, ins);
        } else if ((p & 0xF0) == 0xB0 || ((p & 0xF0) == 0xF0 && (p & 0xF) <= 5)) {
            decode_dst(rd, p, ins);
        } else if (((p & 0xF0) == 0xC0 || (p & 0xF0) == 0xD0 || (p & 0xF0) == 0xE0) && (p & 0xF) >= 7) {
            decode_reg(rd, p, ins);
        } else if (p == 0xF7) {
            // LDX (#8),#: F7 00 #8 00 # 00
            int64_t z1 = rd.u8(), a = rd.u8(), z2 = rd.u8(), b = rd.u8(), z3 = rd.u8();
            if (z1 || z2 || z3) bad(ins, "LDX filler bytes not zero");
            ins.mem_refs.push_back(a);
            set(ins, "LDX", {Op::Ref("(" + hx(a) + ")", a, 'm'), hx(b)});
        } else if (p >= 0xF8) {
            set(ins, "SWI", {dec(p & 7)});
        } else {
            ins.invalid = true;
        }
    } catch (Eof&) {
        ins.invalid = true;
    }
    if (ins.invalid) {
        rd.pc = addr + 1;
        int b = mem(addr);
        ins.mnem = "DB";
        ins.ops = {Op("0x" + hexs(b < 0 ? 0 : b, 2))};
        ins.has_target = false;
        ins.target = 0;
        ins.is_call = ins.is_jump = ins.conditional = ins.ends_block = false;
        ins.mem_refs.clear();
        ins.illegal.clear();
    }
    ins.size = int(rd.pc - addr);
    for (int64_t a = addr; a < rd.pc; a++) {
        int v = mem(a);
        ins.raw.push_back(uint8_t(v < 0 ? 0 : v));
    }
    return ins;
}

Insn decode_bytes(const Bytes& b, int64_t addr) {
    MemFn m = [&](int64_t a) -> int { return (a - addr >= 0 && a - addr < int64_t(b.size())) ? b[size_t(a - addr)] : -1; };
    return decode(m, addr);
}

}  // namespace unitron
