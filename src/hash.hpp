// SHA-1 and SHA-256 (FIPS 180-4), lowercase hex digests like hashlib's hexdigest().
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "bytes.hpp"

namespace unitron {

std::string sha1_hex(const Bytes& data);
std::string sha256_hex(const Bytes& data);
std::string sha1_hex(const uint8_t* data, size_t len);
std::string sha256_hex(const uint8_t* data, size_t len);

}  // namespace unitron
