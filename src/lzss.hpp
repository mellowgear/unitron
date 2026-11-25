// LZSS in the parameters of Haruhiko Okumura's LZSS.C (1989): N=1024 ring, F=18 longest match,
// THRESHOLD=2, zero-filled ring, first write at N-F. Port of legacy/python/unitron/codec/lzss.py.
#pragma once
#include <cstdint>
#include <string>

#include "bytes.hpp"

namespace unitron {
namespace lzss {

constexpr int N = 0x400, F = 18, THRESHOLD = 2;

// Decode until exactly out_len bytes (stops even inside a flag group). pos is the start offset in
// data with Python indexing (a negative pos counts from the end). *used receives the number of
// input bytes consumed. Throws std::out_of_range("input ends inside the LZSS stream") where the
// Python version raises IndexError.
Bytes decompress(const Bytes& data, int64_t out_len, int64_t pos = 0, int64_t* used = nullptr);

// Greedy encoder; tie picks the offset among equally long matches: "first" (lowest ring
// offset), "last" (highest), "near" (most recently written), "far" (least recently written).
// Throws std::invalid_argument for another tie name.
Bytes compress(const Bytes& raw, const std::string& tie = "first");

// Okumura's binary-tree encoder. clamp=true cuts the final match to the bytes left (LZSS.C as
// published); clamp=false lets it run past the end.
Bytes compress_okumura(const Bytes& raw, bool clamp = true);

}  // namespace lzss
}  // namespace unitron
