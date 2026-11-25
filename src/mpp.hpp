// Macro preprocessor: the documented subset of Toshiba's (TR part 4, ch. 12-17).
// Preprocess functions (TR ch. 14): #include, #define (object and function-like, # and ##), #undef,
// #if/#ifdef/#ifndef/#elif/#else/#endif, #error, #line. Macro functions (TR ch. 15): ?macro with
// parameters and `labels`, ?endm; calls are `name args` in the mnemonic position. Generated labels
// are the label name plus a 3-digit decimal count per name (lab000, lab001, ... TR p.65-66).
// Comments: `;` is kept, `//` and `/* */` are dropped (TR p.60). Other ?functions are rejected.
#pragma once
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "where.hpp"

namespace unitron {

struct MppError : std::runtime_error {
    Where where;
    MppError(const std::string& msg, const Where& w) : std::runtime_error(msg), where(w) {}
};

using SrcLine = std::pair<std::string, Where>;
using Defines = std::vector<std::pair<std::string, std::string>>;   // -D NAME=VAL, in order

std::vector<SrcLine> preprocess(const std::string& path, const std::vector<std::string>& include_dirs,
                                const Defines& defines);

// THOME900/include, if $THOME900 is set ("" otherwise)
std::string thome_include();

}  // namespace unitron
