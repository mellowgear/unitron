// Data files compiled into the binary (see CMakeLists.txt: cmake/embed.cmake).
#pragma once
#include <string>

namespace unitron {

const std::string& embedded_hwnames_tsv();   // data/hwnames.tsv
const std::string& embedded_ngpc_rt_h();     // data/ngpc_rt.h (copied next to `unitron lift` output)

}  // namespace unitron
