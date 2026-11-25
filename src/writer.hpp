// Any NGPC ROM -> asm900 source that assembles back to the same bytes.
// Code found by the trace becomes instructions; everything else becomes `db` lines. A non-default
// encoding gets asm900's `:n` width (`ld A,(0x62AA:24)`); one with no asm900 spelling at all, or a
// form the datasheet doesn't define, is written as `db` with the instruction in the comment.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "trace.hpp"

namespace unitron {

struct WriteStats {
    int64_t instructions = 0, db_fallback = 0, width_marked = 0, data_bytes = 0, sections = 0;
    DiscoverStats discover;
};

using WidthVar = std::vector<std::pair<size_t, int>>;   // (operand index, `:n` width)
// changes that make ins.text() assemble to ins.raw; false if none do
bool spell(const Insn& ins, int64_t pc, WidthVar& out);

WriteStats write_source(const Rom& rom, const InsnMap& insns, const Labels& labels, const std::string& outdir,
                        const std::string& module = "game");
WriteStats disassemble_rom(const Rom& rom, const std::string& outdir, const std::string& executed, bool sweep = true,
                           const std::string& hints = "");

}  // namespace unitron
