// One instruction line: mnemonic + operand text -> parsed operands -> bytes.
#pragma once
#include <string>
#include <vector>

#include "encode.hpp"

namespace unitron {

struct LineError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ParsedInsn {
    std::string base;
    int sfx = 0;
    std::vector<Operand> ops;
};

ParsedInsn parse_insn(const std::string& mnem, const std::string& field);   // throws LineError
// Encode one instruction written as 'mnem operands' (no label/comment); no symbol table.
Bytes assemble_line(const std::string& text, int64_t pc = 0, const EvalFn* ev = nullptr);
// assemble_line, or false instead of an error
bool assemble_or_none(const std::string& text, int64_t pc, Bytes& out);

}  // namespace unitron
