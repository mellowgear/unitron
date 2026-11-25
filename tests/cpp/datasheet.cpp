// datasheet: encodings taken from the CPU documents, not from unitron's own output.
//   unitron_tests datasheet TABLE_DIR
// TABLE_DIR/tlcs900.txt (Toshiba's TLCS-900 datasheet and Assembler Reference) and
// TABLE_DIR/z80.txt (Zilog's UM0080) hold lines "TEXT <tab> BYTES <tab> SOURCE" (# comments).
// Each TEXT is assembled alone (TLCS-900 at PC 0x200000, Z80 at 0000H, or at ADDR when TEXT is
// "@ADDR text") and must give BYTES.
// A fourth field "differs: XX XX" records a known disagreement between unitron and the document:
// it is reported, not failed, as long as unitron still gives exactly those bytes.
#include <iostream>
#include <string>
#include <vector>

#include "line.hpp"
#include "util.hpp"
#include "z80asm.hpp"

using namespace unitron;

namespace {

std::string hexs_of(const Bytes& b) {
    std::string s;
    for (auto x : b) s += (s.empty() ? "" : " ") + hexs(x, 2);
    return s;
}

std::string canon(const std::string& h) { return join(split_ws(upper(h)), " "); }

int check_table(const std::string& path, bool z80, int& known) {
    Bytes raw = read_bytes(path);
    int n = 0, bad = 0, line = 0;
    for (auto& l : split_on(std::string(raw.begin(), raw.end()), "\n")) {
        line++;
        if (strip(l).empty() || l[0] == '#') continue;
        auto f = split_on(l, "\t");
        if (f.size() < 3) {
            std::cerr << path << ":" << line << ": expected TEXT<tab>BYTES<tab>SOURCE\n";
            bad++;
            continue;
        }
        n++;
        // "@ADDR TEXT": the instruction's address (hex)
        int64_t pc = z80 ? 0 : 0x200000;
        std::string text = f[0];
        if (starts_with(text, "@")) {
            auto sp = text.find(' ');
            pc = std::stoll(text.substr(1, sp - 1), nullptr, 16);
            text = strip(text.substr(sp + 1));
        }
        std::string got;
        try {
            got = hexs_of(z80 ? z80::assemble_line(text, pc) : assemble_line(text, pc));
        } catch (std::exception& e) {
            got = std::string("error: ") + e.what();
        }
        std::string want = canon(f[1]), where = path_name(path) + ":" + dec(line) + ": " + f[0] + " (" + f[2] + ")";
        if (f.size() > 3 && starts_with(f[3], "differs: ")) {
            std::string frozen = strip(f[3].substr(9));
            if (got == want) {
                std::cerr << where << ": now agrees with the document; remove the differs field\n";
                bad++;
            } else if (got != (starts_with(frozen, "error") ? frozen : canon(frozen))) {
                std::cerr << where << ": want " << want << ", recorded difference " << frozen << ", got " << got << "\n";
                bad++;
            } else {
                std::cout << "  known difference: " << where << ": document " << want << ", unitron " << got << "\n";
                known++;
            }
        } else if (got != want) {
            std::cerr << where << ": want " << want << ", got " << got << "\n";
            bad++;
        }
    }
    std::cout << path_name(path) << ": " << n << " encodings, " << bad << " differ\n";
    return bad;
}

}  // namespace

int test_datasheet(const std::string& dir) {
    int known = 0;
    int bad = check_table(dir + "/tlcs900.txt", false, known) + check_table(dir + "/z80.txt", true, known);
    if (known) std::cout << known << " known difference(s) with the documents (see the tables)\n";
    return bad ? 1 : 0;
}
