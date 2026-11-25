#include "regs.hpp"

namespace unitron {

const char* const REG8[8] = {"W", "A", "B", "C", "D", "E", "H", "L"};
const char* const REG16[8] = {"WA", "BC", "DE", "HL", "IX", "IY", "IZ", "SP"};
const char* const REG32[8] = {"XWA", "XBC", "XDE", "XHL", "XIX", "XIY", "XIZ", "XSP"};
// Condition codes; 8 (T, always) is printed without a condition.
const char* const CC[16] = {"F", "LT", "LE", "ULE", "OV", "MI", "Z", "C",
                            "T", "GE", "GT", "UGT", "NOV", "PL", "NZ", "NC"};
const char* const ALU[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
const char* const SHIFT[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};

static const char* const PAIR[4] = {"WA", "BC", "DE", "HL"};
static const char* const LO[4] = {"A", "C", "E", "L"};
static const char* const HI[4] = {"W", "B", "D", "H"};
static const char* const IDX[4] = {"IX", "IY", "IZ", "SP"};

std::string ext_reg(int code, int size) {
    int off = code & 3, grp = (code >> 2) & 3;
    if (code < 0x40 || (code >= 0xD0 && code < 0xF0)) {
        std::string lo = LO[grp], hi = HI[grp], pair = PAIR[grp], tag, pre;
        if (code < 0x40) {
            tag = std::to_string(code >> 4);
            pre = "R";
        } else if (code < 0xE0) {
            tag = "'";
        }
        std::string base;
        if (size == 1) {
            const std::string opts[4] = {pre + lo, pre + hi, "Q" + lo, "Q" + hi};
            base = opts[off];
        } else if (size == 2) {
            base = off < 2 ? pre + pair : "Q" + pair;
        } else {
            base = "X" + pair;
        }
        return base + tag;
    }
    if (code >= 0xF0) {
        std::string r = IDX[grp];
        if (size == 1) {
            const std::string opts[4] = {r + "L", r + "H", "Q" + r + "L", "Q" + r + "H"};
            return opts[off];
        }
        if (size == 2) return off < 2 ? r : "Q" + r;
        return "X" + r;
    }
    return "";
}

static bool canonical_code(int code, int size) {
    return (size != 2 || (code & 1) == 0) && (size != 4 || (code & 3) == 0);
}

const std::unordered_map<std::string, RegInfo>& REG() {
    static const std::unordered_map<std::string, RegInfo> m = [] {
        std::unordered_map<std::string, RegInfo> r;
        for (int size : {1, 2, 4})
            for (int code = 0; code < 256; code++) {
                if (!canonical_code(code, size)) continue;
                std::string n = ext_reg(code, size);
                if (!n.empty() && !r.count(n)) r[n] = {size, code};
            }
        return r;
    }();
    return m;
}

int short_code(int size, const std::string& name) {
    const char* const* t = size == 1 ? REG8 : size == 2 ? REG16 : REG32;
    for (int i = 0; i < 8; i++)
        if (name == t[i]) return i;
    return -1;
}

int cc_code(const std::string& name) {
    for (int i = 0; i < 16; i++)
        if (name == CC[i]) return i;
    static const std::pair<const char*, int> alias[] = {{"EQ", 6}, {"NE", 14}, {"ULT", 7}, {"UGE", 15},
                                                        {"M", 5},  {"P", 13},  {"PE", 4},  {"PO", 12}};
    for (auto& a : alias)
        if (name == a.first) return a.second;
    return -1;
}

static const std::map<std::pair<int, int>, std::string>& CR() {
    static const std::map<std::pair<int, int>, std::string> m = [] {
        std::map<std::pair<int, int>, std::string> r;
        for (int i = 0; i < 4; i++) {
            r[{0x00 + 4 * i, 4}] = "DMAS" + std::to_string(i);
            r[{0x10 + 4 * i, 4}] = "DMAD" + std::to_string(i);
            r[{0x20 + 4 * i, 2}] = "DMAC" + std::to_string(i);
            r[{0x22 + 4 * i, 1}] = "DMAM" + std::to_string(i);
        }
        r[{0x3C, 2}] = "INTNEST";
        r[{0x3C, 4}] = "XNSP";
        return r;
    }();
    return m;
}

const std::map<std::string, std::pair<int, int>>& CR_CODE() {
    static const std::map<std::string, std::pair<int, int>> m = [] {
        std::map<std::string, std::pair<int, int>> r;
        for (auto& kv : CR()) r[kv.second] = kv.first;
        return r;
    }();
    return m;
}

const char* cr_name(int code, int size) {
    auto it = CR().find({code, size});
    return it == CR().end() ? nullptr : it->second.c_str();
}

char sfx_char(int size) { return size == 1 ? 'B' : size == 2 ? 'W' : 'L'; }

}  // namespace unitron
