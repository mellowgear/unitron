// NGPC hardware names from the single cited data file data/hwnames.tsv (embedded at build time).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace unitron {

struct HwName {
    int64_t addr, size;
    std::string name, kind, cite, official;
};
struct HwBit {
    int64_t reg, bit;
    std::string name, cite, official;
};
struct HwSection {
    std::string kind, title;
};

const std::vector<HwName>& hw_names();        // sorted by address
const std::vector<HwBit>& hw_bits();
const std::vector<HwSection>& hw_sections();
const char* hw_name_at(int64_t addr);           // nullptr if none
// data/ngpc_hw.inc as generated from the data file
std::string render_hw_inc();
// the embedded data file
const std::string& hw_tsv();

}  // namespace unitron
