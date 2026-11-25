// Z80 disassembler for the NGPC's sound CPU program: a raw image that starts at Z80 0000H (the
// bytes the main CPU copies to its 0x7000). Code is found by tracing from the Z80's fixed entry
// points (UM0080): 0000H after reset (p.7), 0038H for the mode 1 interrupt (p.19) and 0066H for the
// nonmaskable interrupt (p.19), plus --executed addresses and `code` hints. Everything else is data.
// The source it writes is Zilog syntax (docs/z80.md) and assembles back to the same bytes.
#pragma once
#include <cstdint>
#include <string>

#include "bytes.hpp"

namespace unitron::z80 {

struct DisOptions {
    std::string executed;   // file of hex code addresses (extra entry points)
    std::string hints;      // `code ADDR [NAME]` / `data START END` (as for the main CPU)
};

struct DisStats {
    int64_t instructions = 0;   // traced instructions written as instructions
    int64_t db_fallback = 0;    // traced bytes written as DEFB (undocumented, or a second encoding)
    int64_t data_bytes = 0;     // bytes no trace reached
};

// address, bytes and instruction, one line each, with the labels
std::string listing(const Bytes& img, const DisOptions& opt, DisStats* st = nullptr);
// the source text; `name` goes into the header comment
std::string source(const Bytes& img, const DisOptions& opt, const std::string& name, DisStats* st = nullptr);

}  // namespace unitron::z80
