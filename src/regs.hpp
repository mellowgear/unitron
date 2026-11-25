// TLCS-900/H register names and codes, shared by the decoder and the encoder.
// Register codes follow the datasheet's extended register map ("r" codes, 900/H maximum mode).
#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace unitron {

extern const char* const REG8[8];
extern const char* const REG16[8];
extern const char* const REG32[8];
extern const char* const CC[16];
extern const char* const ALU[8];
extern const char* const SHIFT[8];

struct RegInfo {
    int size, code;
};
// name -> (size, code): the first (canonical) code for each name
const std::unordered_map<std::string, RegInfo>& REG();
// current-bank short code (0..7) of a name at a size, or -1
int short_code(int size, const std::string& name);
// condition name (incl. aliases) -> code, or -1
int cc_code(const std::string& name);
// control register name -> (code, size); (code, size) -> name
const std::map<std::string, std::pair<int, int>>& CR_CODE();
const char* cr_name(int code, int size);   // nullptr if none

// name of an extended register code at a size, or "" if the code has no register
std::string ext_reg(int code, int size);
char sfx_char(int size);   // 1 -> 'B', 2 -> 'W', 4 -> 'L'

}  // namespace unitron
