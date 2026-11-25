// Link command files (.lcf), the useful subset of the language of Toshiba's linker tulink
// (TLCS-900 Assembler Reference, chapter 8; "TR 8.x" below). See docs/lcf.md.
//
// Lcf::parse_file reads the file (syntax errors throw); Lcf::place computes where every relocatable
// input section goes: run (logical) address and load (image) address. Placement problems are
// collected in LcfResult::errors as "FILE:LINE: message".
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace unitron {

struct LcfInput {
    size_t id;
    std::string name;
    std::string module_file;  // path as given on the command line
    std::string kind;         // code|data|romdata
    int64_t size, align;
    int width;                // 8/16/24 address bits of small/medium/large
};

struct LcfPlaced {
    int64_t addr;  // run address (TR "start address", addr=)
    int64_t load;  // image address (TR "allocation address", org=)
};

// One output section as allocated (for map files; also tells which sections have no object data).
struct LcfOutput {
    std::string name;
    int64_t addr = 0, load = 0, size = 0;
    std::string attr;          // "" (normal), "DSECT", "COPY", "NOLOAD" or "OVERLAY" (TR 8.3.5)
    bool has_pad = false;      // "= value" padding field (TR 8.3.7)
    int64_t pad = 0;
    std::string memory;        // memory area holding the run address ("" if none / no memory part)
    std::vector<size_t> inputs;  // LcfInput ids in link order
    bool automatic = false;    // created for an input section not named in the file (TR 8.3.6)
};

struct LcfResult {
    std::map<size_t, LcfPlaced> placed;
    std::vector<std::pair<std::string, int64_t>> symbols;  // assign statements, in evaluation order
                                                           // (a symbol may repeat: the last one wins)
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<LcfOutput> outputs;  // in allocation order
};

struct LcfData;

class Lcf {
public:
    static Lcf parse_file(const std::string& path);  // throws std::runtime_error("FILE:LINE: msg")
    static Lcf parse_string(const std::string& text, const std::string& path);
    // used: [start,end) of absolute sections, which must not be overlapped
    LcfResult place(const std::vector<LcfInput>& inputs, const std::vector<std::pair<int64_t, int64_t>>& used) const;

private:
    std::shared_ptr<const LcfData> d_;
};

}  // namespace unitron
