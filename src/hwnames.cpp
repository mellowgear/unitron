#include "hwnames.hpp"

#include <stdexcept>

#include "embedded.hpp"
#include "util.hpp"

namespace unitron {

namespace {

struct Tables {
    std::vector<HwName> names;
    std::vector<HwBit> bits;
    std::vector<HwSection> sections;
};

const Tables& tables() {
    static const Tables t = [] {
        Tables r;
        int ln = 0;
        for (auto& line : split_on(embedded_hwnames_tsv(), "\n")) {
            ln++;
            if (line.empty() || line[0] == '#') continue;
            auto f = split_on(line, "\t");
            if (f[0] == "section" && f.size() == 3) r.sections.push_back({f[1], f[2]});
            else if (f[0] == "name" && f.size() == 7) r.names.push_back({py_int0(f[1]), py_int0(f[3]), f[2], f[4], f[5], f[6]});
            else if (f[0] == "bit" && f.size() == 6) r.bits.push_back({py_int0(f[1]), py_int0(f[3]), f[2], f[4], f[5]});
            else throw std::runtime_error("data/hwnames.tsv:" + std::to_string(ln) + ": bad record");
        }
        return r;
    }();
    return t;
}

std::string hexval(int64_t v) { return v < 0x100 ? "0x" + hexs(v, 2) : "0x" + hexs(v, 4); }

}  // namespace

const std::vector<HwName>& hw_names() { return tables().names; }
const std::vector<HwBit>& hw_bits() { return tables().bits; }
const std::vector<HwSection>& hw_sections() { return tables().sections; }
const std::string& hw_tsv() { return embedded_hwnames_tsv(); }

const char* hw_name_at(int64_t addr) {
    for (auto& h : hw_names())
        if (h.addr == addr) return h.name.c_str();
    return nullptr;
}

std::string render_hw_inc() {
    std::vector<std::string> out = {
        "; ngpc_hw.inc: NGPC hardware names. Generated from data/hwnames.tsv (unitron_tests hwinc --write); do not edit.",
        "; Sources: Toshiba IO900H.H and SNK developer docs, cited per name; see docs/hardware-names.md.",
    };
    for (auto& s : hw_sections()) {
        out.push_back("");
        out.push_back("; " + s.title);
        for (auto& h : hw_names())
            if (h.kind == s.kind) out.push_back(h.name + "\tequ\t" + hexval(h.addr) + "\t; " + h.cite + " (" + h.official + ")");
    }
    out.push_back("");
    out.push_back("; Bit numbers (mask = 1 << n)");
    for (auto& b : hw_bits()) {
        const char* reg = hw_name_at(b.reg);
        out.push_back(b.name + "\tequ\t" + dec(b.bit) + "\t; bit " + dec(b.bit) + " of " + (reg ? reg : "?") + "; " + b.cite +
                      " (" + b.official + ")");
    }
    return join(out, "\n") + "\n";
}

}  // namespace unitron
