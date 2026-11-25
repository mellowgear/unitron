// asm900 operand syntax -> operand objects for the encoder.
// Registers `A WA XWA RA3 QW XIX' ...`; immediates are expressions; memory `(XHL)`, `(XHL+4)`,
// `(XHL+DE)`, `(-XHL)`, `(XHL+)`, `(-XSP:4)`, `(XHL+:1)`, `(XDE-)`, `(0x6F82)`; JP/CALL/LDA take
// the address bare (`jp XHL`, `jp C,XIX+2`, `lda XHL,XHL+DE`).
#pragma once
#include <string>
#include <vector>

#include "expr.hpp"

namespace unitron {

struct OperandError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RegOp {
    std::string name;
    int size = 0, code = 0, shrt = -1;   // shrt: current-bank code or -1
};
bool reg_of(const std::string& text, RegOp& out);
RegOp make_reg(const std::string& upper_name);

struct Operand {
    enum Kind { Reg, Special, Ctl, Cond, Imm, Mem } kind = Imm;
    enum Mode { Ind, Disp, Idx, Pre, Post, Dec, Abs } mode = Ind;
    RegOp reg;            // Reg; Mem base register (has_reg)
    bool has_reg = false;
    RegOp idx;            // Mem idx
    std::string name;     // Special/Ctl/Cond name
    int code = 0, size = 0;   // Ctl code/size; Cond code
    ExprP expr;           // Imm
    ExprP disp, addr;     // Mem
    int step = 0;         // 0 = not written
    bool bare = false;
};

Operand parse_operand(const std::string& text, bool bare_addr = false, bool cond = false);
// Split an operand field at top-level commas (quotes and parentheses respected).
std::vector<std::string> split_operands(const std::string& text);

}  // namespace unitron
