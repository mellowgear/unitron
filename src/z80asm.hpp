// Z80 assembler for the NGPC's sound CPU, in Zilog's assembly language: Zilog Z80 Assembly Language
// Programming Manual (1978, "ZM" below) chapter II, with the instruction forms of UM0080 (z80.hpp).
// One source file (plus *Include files) -> one raw image that starts at Z80 address 0000H. Errors
// carry ZM's error numbers (appendix A, printed p.280-283). docs/z80.md is the reference.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "bytes.hpp"
#include "where.hpp"

namespace unitron::z80 {

constexpr int64_t RAM_SIZE = 0x1000;   // "4 KB of internal RAM" (SNK SysPro l.108)

class Assembler {
public:
    explicit Assembler(std::vector<std::string> include_dirs = {}) : include_dirs_(std::move(include_dirs)) {}
    // Assemble; false if there are errors (in d.errors).
    bool assemble(const std::string& path);
    Bytes image;
    std::vector<std::pair<std::string, int64_t>> symbols;   // definition order
    std::vector<std::pair<std::string, int64_t>> labels;    // the labels among them (not EQU/DEFL)
    Diags d;

    struct State;

private:
    std::vector<std::string> include_dirs_;
};

// One instruction (no label or comment) at address pc, no symbols: bytes, or throws
// std::runtime_error with the message. For tests.
Bytes assemble_line(const std::string& line, int64_t pc = 0);

}  // namespace unitron::z80
