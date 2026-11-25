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

// A names file (`dis --names`): `ADDR NAME` lines, the `asm --sym` format ('#' or ';' starts a
// comment). Names of unitron's own hardware names and bits are skipped (hw.inc has them).
struct Names {
    std::vector<std::pair<int64_t, std::string>> entries;   // file order
    int64_t skipped = 0;                                     // hardware names
};
Names read_names(const std::string& path);   // throws std::runtime_error("FILE:LINE: ...")

struct SourceOptions {
    std::string module = "game";
    const Hints* hints = nullptr;
    const Names* names = nullptr;
    bool addr_comments = false;   // `; @ADDR` on every line
};
struct NameStats {
    int64_t labels = 0, equs = 0, unplaced = 0, skipped = 0;
};
WriteStats write_source(const Rom& rom, const InsnMap& insns, const Labels& labels, const std::string& outdir,
                        const SourceOptions& opt = {}, NameStats* ns = nullptr);

struct DisOptions {
    std::string executed, hints, names;
    bool sweep = true, addr_comments = false;
};
// names (if given) reports what the names file did, for the caller to print
WriteStats disassemble_rom(const Rom& rom, const std::string& outdir, const DisOptions& opt, NameStats* ns = nullptr);

}  // namespace unitron
