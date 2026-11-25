// Lift traced TLCS-900/H code to C (experimental): `unitron lift ROM -o OUTDIR`.
// Port of legacy/python/unitron/lift/lift.py. Writes OUTDIR/game.c (one C function per discovered function),
// OUTDIR/ngpc_regs.h and a copy of ngpc_rt.h. The C is instruction-faithful: CPU registers are
// globals (macros over a register-file struct), memory goes through rd8/wr8..., flags are computed
// only where a later instruction can read them.
#pragma once
#include <cstdint>
#include <string>

namespace unitron {

struct LiftStats {
    int64_t functions = 0, instructions = 0, unimplemented = 0;
};

// executed / symbols: "" = none. Throws std::runtime_error for user errors.
LiftStats lift_run(const std::string& rom_path, const std::string& outdir, const std::string& executed,
                   const std::string& symbols, const std::string& hints = "");

}  // namespace unitron
