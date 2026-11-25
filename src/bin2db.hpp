// Binary data -> asm900 `db` lines (generated data for `$include`). Port of legacy/python/unitron/data/bin2db.py.
#pragma once
#include <cstdint>
#include <string>

#include "bytes.hpp"

namespace unitron {

// An empty label or source means None (no label line / no header comment), as in Python where
// both are tested for truth. per_line == 0 throws std::runtime_error("range() arg 3 must not be
// zero"); a negative per_line gives no data lines (an empty Python range).
std::string to_db(const Bytes& data, const std::string& label = "", int64_t per_line = 16,
                  const std::string& source = "");

}  // namespace unitron
