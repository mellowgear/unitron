// TLCS-900/H instruction decoder (the NGPC CPU: the 900 core in permanent maximum mode).
// Built from the Toshiba TLCS-900 CPU datasheet (instruction lists and code maps). Text output is
// asm900 syntax: lowercase mnemonic, uppercase registers, `jp XHL`, `(XHL+:1)` for a step that
// differs from the operand size.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "util.hpp"

namespace unitron {

// Operand text; a Ref also carries an absolute address (for labels). kind: 'c' code, 'm' mem.
struct Op {
    std::string text;
    bool ref = false;
    int64_t addr = 0;
    char kind = 0;
    Op() = default;
    Op(std::string t) : text(std::move(t)) {}
    Op(const char* t) : text(t) {}
    static Op Ref(std::string t, int64_t a, char k) {
        Op o(std::move(t));
        o.ref = true;
        o.addr = a;
        o.kind = k;
        return o;
    }
};

struct Insn {
    int64_t addr = 0;
    int size = 0;
    std::string mnem = "?";
    std::vector<Op> ops;
    bool has_target = false;
    int64_t target = 0;       // direct branch/call target
    bool is_call = false, is_jump = false, conditional = false, ends_block = false;
    bool invalid = false;     // no instruction decodes here
    std::string illegal;      // decodes, but is not a datasheet form (reason); "" = none
    Bytes raw;
    std::vector<int64_t> mem_refs;   // absolute data addresses referenced
    bool has_imm32 = false;
    int64_t imm32 = 0;        // 32-bit immediate loaded into a register
    std::string dst_reg;
    std::string comment;
    std::string text() const;           // asm900 text (mnemonic, tab, operands)
};

// mem(addr) -> byte or -1
using MemFn = std::function<int(int64_t)>;
Insn decode(const MemFn& mem, int64_t addr);
Insn decode_bytes(const Bytes& b, int64_t addr = 0);

std::string hx(int64_t v);

}  // namespace unitron
