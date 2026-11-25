// TLCS-900/H instruction encoder (asm900 operands -> bytes). Policy: docs/encoding.md.
#pragma once
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "operands.hpp"

namespace unitron {

struct EncodeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using EvalFn = std::function<VW(const ExprP&)>;

// j/cal choices (TR p.16-17): optimize = asm900 -O1 (shortest of jr/jrl/jp, calr/call), else
// -O0 (always jp/call). local(e): the target is in the instruction's own section, so its
// distance is known before linking and it may relax even when relocatable.
struct JumpOpts {
    bool optimize = true;
    std::function<bool(const ExprP&)> local;
};

const std::set<std::string>& MNEMONICS();
bool cond_first(const std::string& m);   // a condition code may be the first operand
bool bare_addr(const std::string& m);    // the address operand is written without parentheses
// 'LDW' -> ('LD', 2); 'SLL' -> ('SLL', 0). false if unknown.
bool split_mnemonic(const std::string& m, std::string& base, int& sfx);

Bytes encode(const std::string& m, int sfx, const std::vector<Operand>& ops, int64_t pc, const EvalFn& ev,
             bool final = true, const JumpOpts* jo = nullptr);

}  // namespace unitron
