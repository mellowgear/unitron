// NGPC cartridge image: address mapping and the 0x40-byte header (SNK SysPro, SOFTWARE CASSETTE
// RECOGNITION HEADER INFORMATION FORMAT).
#pragma once
#include <cstdint>
#include <string>

#include "util.hpp"

namespace unitron {

constexpr int64_t CART_BASE = 0x200000;
constexpr int64_t CART_HI_BASE = 0x800000;   // second flash chip on 4 MB cartridges

struct RomHeader {
    std::string license, system, title;   // title: ASCII with U+FFFD for other bytes (UTF-8)
    int64_t entry = 0, game_id = 0, version = 0, size = 0;
};

class Rom {
public:
    explicit Rom(Bytes data) : data(std::move(data)) {}
    Bytes data;
    int64_t addr_to_off(int64_t a) const;   // -1 if outside
    int operator()(int64_t a) const {       // byte or -1
        int64_t o = addr_to_off(a);
        return o < 0 ? -1 : data[size_t(o)];
    }
    bool contains(int64_t a) const { return addr_to_off(a) >= 0; }
    RomHeader header() const;
};

}  // namespace unitron
