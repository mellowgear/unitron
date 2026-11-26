// Data files compiled into the binary (see CMakeLists.txt: cmake/embed.cmake).
#pragma once
#include <string>

namespace unitron {

const std::string& embedded_hwnames_tsv();   // data/hwnames.tsv
const std::string& embedded_hle_bios();      // NgpCraft hle_bios/bios_hle.bin (64 KB, fetched)

}  // namespace unitron
