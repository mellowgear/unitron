#include "rom.hpp"

#include <algorithm>
#include <stdexcept>

namespace unitron {

int64_t Rom::addr_to_off(int64_t a) const {
    int64_t n = int64_t(data.size());
    if (CART_BASE <= a && a < CART_BASE + std::min<int64_t>(n, 0x200000)) return a - CART_BASE;
    if (n > 0x200000 && CART_HI_BASE <= a && a < CART_HI_BASE + n - 0x200000) return a - CART_HI_BASE + 0x200000;
    return -1;
}

static std::string ascii_replace(const Bytes& d, size_t a, size_t b) {
    std::string s;
    for (size_t i = a; i < b && i < d.size(); i++) {
        if (d[i] < 0x80) s += char(d[i]);
        else s += "\xEF\xBF\xBD";
    }
    return s;
}

RomHeader Rom::header() const {
    const Bytes& d = data;
    if (d.size() < 0x24) throw std::runtime_error("ROM too small for a cartridge header");
    RomHeader h;
    h.license = ascii_replace(d, 0, 28);
    h.entry = int64_t(d[0x1C]) | int64_t(d[0x1D]) << 8 | int64_t(d[0x1E]) << 16 | int64_t(d[0x1F]) << 24;
    h.game_id = int64_t(d[0x20]) | int64_t(d[0x21]) << 8;
    h.version = d[0x22];
    h.system = d[0x23] == 0x10 ? "color" : d[0x23] == 0 ? "mono" : "0x" + hexs(d[0x23], 2);
    std::string t = ascii_replace(d, 0x24, 0x30);
    while (!t.empty() && (t.back() == '\0' || t.back() == ' ')) t.pop_back();
    h.title = t;
    h.size = int64_t(d.size());
    return h;
}

}  // namespace unitron
