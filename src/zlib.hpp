// zlib streams (RFC 1950) and the checksums used by PNG: thin wrappers around zlib.
#pragma once
#include <cstddef>
#include <cstdint>

#include "bytes.hpp"

namespace unitron {

// Inflate a complete zlib stream. Bytes after the trailer are ignored (as Python's
// zlib.decompress). Throws std::runtime_error on a corrupt or truncated stream, or one that
// needs a preset dictionary.
Bytes zlib_decompress(const Bytes& data);
Bytes zlib_decompress(const uint8_t* data, size_t len);

// Deflate into a zlib stream at zlib's default level (as Python's zlib.compress).
Bytes zlib_compress(const Bytes& data);

// CRC-32 (ISO-HDLC, as zlib.crc32 / binascii.crc32): pass the previous result to continue.
uint32_t crc32(const uint8_t* data, size_t len, uint32_t init = 0);

uint32_t adler32(const uint8_t* data, size_t len, uint32_t init = 1);

}  // namespace unitron
