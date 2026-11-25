// Z80 instruction set: one table of every documented instruction form, used both to decode bytes
// and to encode assembler lines. Built from Zilog's Z80 CPU User Manual UM0080 (rev. UM008011-0816),
// "Instruction Op Codes" tables 6-20 (printed pages 41-64), with the operand notation of its
// table 4 (page 39). Text is Zilog's: upper-case mnemonics and registers, hex numbers as 0FFH
// (Zilog Z80 Assembly Language Programming Manual, 1978, printed page 13). See docs/z80.md.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bytes.hpp"

namespace unitron::z80 {

// One operand of a form.
//   Lit    a fixed keyword: "A", "HL", "(HL)", "(C)", "AF'", "NZ", ...
//   N      n, an 8-bit immediate               NN     nn, a 16-bit immediate
//   Mem    (nn), a memory address              Port   (n), an I/O port
//   Rel    e, a relative jump target           IX/IY  (IX+d) / (IY+d)
//   Const  a value fixed by the opcode (BIT b, IM m, RST p)
enum class P : uint8_t { Lit, N, NN, Mem, Port, Rel, IX, IY, Const };
struct Pat {
    P k = P::Lit;
    std::string lit;
    int v = 0;
    bool hex = false;   // Const written in hex (RST p)
};

// How control flow continues after an instruction (for the tracer).
enum class Flow : uint8_t { Next, Jump, JumpCond, Call, CallCond, Ret, RetCond, JumpInd, Rst };

// Byte template: values >= 0 are fixed bytes; the others are operand fields.
constexpr int T_D = -1, T_N = -2, T_NN = -3, T_E = -4;

struct Form {
    std::string mnem;
    std::vector<Pat> ops;
    std::vector<int> tmpl;
    Flow flow = Flow::Next;
    bool alt = false;   // a documented second encoding the assembler never picks (ED 63/ED 6B)
    int page = 0;       // UM0080 printed page of its op code table
    int size() const;   // bytes
};

const std::vector<Form>& forms();

// A decoded instruction. form < 0: no documented instruction starts here (size 1).
struct Insn {
    int64_t addr = 0;
    int size = 1;
    int form = -1;
    int64_t n = 0, nn = 0, d = 0, target = 0;
    Bytes raw;
    const Form* f() const { return form < 0 ? nullptr : &forms()[size_t(form)]; }
    bool has_target() const;   // a direct jump/call/rst target in `target`
};

// mem(addr) -> byte or -1 (outside the image)
using MemFn = std::function<int(int64_t)>;
Insn decode(const MemFn& mem, int64_t addr);
Insn decode_bytes(const Bytes& b, int64_t addr = 0);

// Zilog hex: 2 or 4 digits, a leading 0 when the first digit is a letter, an H suffix: 0FFH, 00C0H.
std::string hexz(int64_t v, int digits);

// Operand text. name(addr, kind) may return a label for an address ("" = write the number);
// kind: 'c' a jump/call target, 'r' a relative target, 'm' a memory operand, 'i' a 16-bit immediate
// (LD rr,nn).
using NameFn = std::function<std::string(int64_t addr, char kind)>;
std::string operand_text(const Pat& p, const Insn& ins, const NameFn* name = nullptr);
std::string text(const Insn& ins, const NameFn* name = nullptr);   // "MNEM\tOP,OP" (or "MNEM")

// Bytes of a form with its operand values (already range-checked); pc is the instruction's address.
Bytes encode(const Form& f, int64_t n, int64_t nn, int64_t d, int64_t target, int64_t pc);

}  // namespace unitron::z80
