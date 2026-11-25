// Opcode space and instruction-line vectors.
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "decode.hpp"
#include "line.hpp"
#include "util.hpp"
#include "zlib.hpp"

using namespace unitron;

namespace {

const int64_t PC = 0x200000;

std::string unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            o += c == 't' ? '\t' : c == 'n' ? '\n' : c;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

std::string hexstr(const Bytes& b) {
    std::string s;
    for (auto x : b) s += hexl(x, 2);
    return s;
}

std::string asm_result(const std::string& text) {
    try {
        return hexstr(assemble_line(text, PC));
    } catch (LineError& e) {
        return std::string("!") + e.what();
    } catch (std::exception& e) {
        return std::string("!!") + e.what();
    }
}

bool result_ok(const std::string& want, const std::string& got) {
    if (starts_with(want, "!!")) return starts_with(got, "!");
    return want == got;
}

std::string read_z(const std::string& path) {
    Bytes z = read_bytes(path);
    Bytes raw = zlib_decompress(z);
    return std::string(raw.begin(), raw.end());
}

void write_z(const std::string& path, const std::string& text) {
    write_bytes(path, zlib_compress(Bytes(text.begin(), text.end())));
}

// tests/opspace.py, in the same order
struct Space {
    std::vector<Bytes> tails;
    Space() {
        tails = {Bytes(8, 0), Bytes(8, 0xFF), {0x7F, 0x80, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                 {0x05, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}, {0x80, 0xFF, 0x7F, 0x00, 1, 2, 3, 4},
                 {0x10, 0, 0, 0, 0, 0, 0, 0}};
    }
    static Bytes cat(Bytes a, const Bytes& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }
    std::vector<Bytes> mem_heads() const {
        std::vector<Bytes> out;
        for (int grp : {0x80, 0x90, 0xA0, 0xB0}) {
            for (int r = 0; r < 8; r++) {
                out.push_back({uint8_t(grp | r)});
                for (int d : {0x00, 0x05, 0x7F, 0x80, 0xFB}) out.push_back({uint8_t(grp | 8 | r), uint8_t(d)});
            }
            uint8_t hi = uint8_t(grp + 0x40);
            for (Bytes a : {Bytes{0x00}, Bytes{0x82}, Bytes{0xFF}}) out.push_back(cat({hi}, a));
            for (Bytes a : {Bytes{0, 0}, Bytes{0x34, 0x12}, Bytes{0x80, 0}, Bytes{0xFF, 0xFF}}) out.push_back(cat({uint8_t(hi | 1)}, a));
            for (Bytes a : {Bytes{0, 0, 0}, Bytes{0x56, 0x34, 0x12}, Bytes{0, 0x40, 0}, Bytes{0xFF, 0xFF, 0xFF}})
                out.push_back(cat({uint8_t(hi | 2)}, a));
            for (int code = 0; code < 256; code++) {
                int m = code & 3;
                if (m == 0) out.push_back({uint8_t(hi | 3), uint8_t(code)});
                else if (m == 1)
                    for (Bytes d : {Bytes{5, 0}, Bytes{0, 1}, Bytes{0xFB, 0xFF}, Bytes{0, 0x80}})
                        out.push_back(cat({uint8_t(hi | 3), uint8_t(code)}, d));
                out.push_back({uint8_t(hi | 4), uint8_t(code)});
                out.push_back({uint8_t(hi | 5), uint8_t(code)});
            }
            for (int r : {0x00, 0xE0, 0xE4, 0xF0, 0xFC, 0x3C, 0x01})
                for (int x : {0x00, 0xE0, 0xE1, 0xE4, 0xF0, 0x3D, 0xE2}) {
                    out.push_back({uint8_t(hi | 3), 0x03, uint8_t(r), uint8_t(x)});
                    out.push_back({uint8_t(hi | 3), 0x07, uint8_t(r), uint8_t(x)});
                }
        }
        return out;
    }
    std::vector<Bytes> reg_heads() const {
        std::vector<Bytes> out;
        for (int base : {0xC8, 0xD8, 0xE8}) {
            for (int r = 0; r < 8; r++) out.push_back({uint8_t(base | r)});
            for (int code = 0; code < 256; code++) out.push_back({uint8_t(base - 1), uint8_t(code)});
        }
        return out;
    }
    template <class F>
    void each(F f) const {
        for (int p = 0; p < 256; p++)
            for (auto& t : tails) f(cat({uint8_t(p)}, t));
        for (int p = 0; p < 256; p++)
            for (int b = 0; b < 256; b++) f(cat({uint8_t(p), uint8_t(b)}, tails[0]));
        auto heads = mem_heads();
        auto rh = reg_heads();
        heads.insert(heads.end(), rh.begin(), rh.end());
        for (auto& h : heads)
            for (int op = 0; op < 256; op++)
                for (int t = 0; t < 4; t++) f(cat(cat(h, {uint8_t(op)}), tails[t]));
    }
};

std::string row_of(const Insn& i) {
    std::vector<std::string> ops, refs;
    for (size_t k = 0; k < i.ops.size(); k++) {
        ops.push_back(i.ops[k].text);
        if (i.ops[k].ref)
            refs.push_back(dec(k) + ":" + hexs(i.ops[k].addr) + ":" + (i.ops[k].kind == 'c' ? "code" : "mem"));
    }
    std::string flags;
    flags += i.is_call ? 'c' : '-';
    flags += i.is_jump ? 'j' : '-';
    flags += i.conditional ? 'q' : '-';
    flags += i.ends_block ? 'e' : '-';
    std::vector<std::string> mr;
    for (auto a : i.mem_refs) mr.push_back(hexs(a));
    std::string r = hexstr(i.raw) + "\t" + i.mnem + "\t" + join(ops, "|") + "\t" + esc(i.illegal.empty() ? "-" : i.illegal) +
                    "\t" + flags + "\t" + (i.has_target ? hexs(i.target) : "-") + "\t" + (mr.empty() ? "-" : join(mr, ",")) +
                    "\t" + (i.has_imm32 ? i.dst_reg + "=" + hexs(i.imm32) : "-") + "\t" + (refs.empty() ? "-" : join(refs, ";"));
    return r;
}

}  // namespace

int test_opspace(const std::string& dir, bool write) {
    std::string text = read_z(dir + "/opspace.tsv.z");
    Bytes bits = zlib_decompress(read_bytes(dir + "/opspace_invalid.bin.z"));
    std::unordered_map<std::string, std::string> want;   // raw hex -> row without the asm column
    std::unordered_map<std::string, std::string> want_asm;
    std::vector<std::string> order;
    for (auto& line : split_on(text, "\n")) {
        if (line.empty()) continue;
        size_t k = line.rfind('\t');
        std::string raw = line.substr(0, line.find('\t'));
        want[raw] = line.substr(0, k);
        want_asm[raw] = line.substr(k + 1);
        order.push_back(raw);
    }
    Space sp;
    size_t n = 0, bad = 0, found = 0;
    std::unordered_map<std::string, std::string> fresh;   // raw -> full row from C++
    Bytes fbits;
    std::vector<std::string> forder;
    std::unordered_map<std::string, bool> seen;
    sp.each([&](const Bytes& b) {
        Insn i = decode_bytes(b, PC);
        bool inv = (bits[n / 8] >> (n % 8)) & 1;
        if (write) {
            if (n % 8 == 0) fbits.push_back(0);
            if (i.invalid) fbits.back() |= uint8_t(1 << (n % 8));
            std::string raw = hexstr(i.raw);
            if (!i.invalid && !fresh.count(raw)) {
                std::string g = asm_result(i.text());
                fresh[raw] = row_of(i) + "\t" + (g == raw ? "=" : esc(g));
                forder.push_back(raw);
            }
        }
        n++;
        if (inv != i.invalid) {
            if (bad++ < 20) std::cerr << "invalid flag differs for " << hexstr(b) << ": " << i.text() << "\n";
            return;
        }
        if (i.invalid) return;
        std::string raw = hexstr(i.raw);
        if (seen.count(raw)) return;
        seen[raw] = true;
        auto it = want.find(raw);
        std::string got = row_of(i);
        if (it == want.end() || it->second != got) {
            if (bad++ < 20)
                std::cerr << "decode differs:\n  want " << (it == want.end() ? "(none)" : it->second) << "\n  got  " << got << "\n";
            return;
        }
        found++;
        std::string w = want_asm[raw];
        std::string g = asm_result(i.text());
        if (g == raw && w == "=") return;
        if (!result_ok(w, esc(g))) {
            if (bad++ < 20) std::cerr << "asm differs for " << i.text() << ":\n  want " << w << "\n  got  " << esc(g) << "\n";
        }
    });
    if (found != want.size() && bad == 0) {
        std::cerr << "found " << found << " of " << want.size() << " instructions\n";
        bad++;
    }
    std::cout << "opspace: " << n << " inputs, " << found << " instructions, " << bad << " differences\n";
    if (write && bad) {
        std::string t;
        for (auto& r : forder) t += fresh[r] + "\n";
        write_z(dir + "/opspace.tsv.z", t);
        write_bytes(dir + "/opspace_invalid.bin.z", zlib_compress(fbits));
        std::cout << "rewrote opspace vectors\n";
        return 0;
    }
    return bad ? 1 : 0;
}

int test_lines(const std::string& dir, bool write) {
    std::string text = read_z(dir + "/lines.tsv.z");
    std::string fresh;
    size_t n = 0, bad = 0;
    for (auto& line : split_on(text, "\n")) {
        if (line.empty()) continue;
        size_t k = line.find('\t');
        std::string src = unesc(line.substr(0, k)), want = line.substr(k + 1);
        std::string got = esc(asm_result(src));
        n++;
        fresh += line.substr(0, k) + "\t" + (result_ok(want, got) && starts_with(want, "!!") ? want : got) + "\n";
        if (!result_ok(want, got)) {
            if (bad++ < 30) std::cerr << "line " << esc(src) << ":\n  want " << want << "\n  got  " << got << "\n";
        }
    }
    std::cout << "lines: " << n << " lines, " << bad << " differences\n";
    if (write && bad) {
        write_z(dir + "/lines.tsv.z", fresh);
        std::cout << "rewrote lines.tsv.z\n";
        return 0;
    }
    return bad ? 1 : 0;
}
