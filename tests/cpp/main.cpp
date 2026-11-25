// unitron C++ tests against the vectors frozen from the Python reference (tools/freeze_vectors.py, removed; in git history).
//   unitron_tests opspace VECTOR_DIR
//   unitron_tests lines VECTOR_DIR
//   unitron_tests rom VECTOR_DIR UNITRON_EXE      (needs $UNITRON_ROM)
//   unitron_tests hwinc DATA_DIR
//   unitron_tests hwnames DATA_DIR                (data/hwnames.tsv is well formed)
//   unitron_tests datasheet TABLE_DIR             (tests/datasheet/: encodings from the CPU documents)
//   unitron_tests z80opspace VECTOR_DIR
//   unitron_tests z80rom VECTOR_DIR UNITRON_EXE   ($UNITRON_ROM and/or $UNITRON_ROM2; skipped when absent)
//   unitron_tests z80shift
//   unitron_tests cart CART_DIR UNITRON_EXE      (the test cartridge, tests/cart/; needs no ROM)
//   unitron_tests cases TESTS_DIR UNITRON_EXE FOLDER[/NAME]...   (tests/asm/, tests/link/, ...; tests/README.md)
// Append --write to rewrite the expected outputs after a deliberate behavior change.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int test_opspace(const std::string& dir, bool write);
int test_lines(const std::string& dir, bool write);
int test_rom(const std::string& dir, const std::string& exe);
int test_hwinc(const std::string& dir, bool write);
int test_hwnames(const std::string& dir);
int test_datasheet(const std::string& dir);
int test_lcf(const std::string& dir);
int test_z80opspace(const std::string& dir, bool write);
int test_z80rom(const std::string& dir, const std::string& exe, bool write);
int test_z80shift();
int test_cart(const std::string& dir, const std::string& exe, bool write);
int test_cases(const std::string& tests_dir, const std::string& exe, const std::vector<std::string>& which, bool write);

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: unitron_tests TEST DIR [UNITRON_EXE] [...] [--write] (see the list at the top of tests/cpp/main.cpp)\n";
        return 2;
    }
    std::string what = argv[1], dir = argv[2], exe = argc > 3 ? argv[3] : "";
    // --write (last argument): rewrite the expected outputs from this build (after a deliberate change)
    bool write = std::string(argv[argc - 1]) == "--write";
    try {
        if (what == "opspace") return test_opspace(dir, write);
        if (what == "lines") return test_lines(dir, write);
        if (what == "rom") return test_rom(dir, exe);
        if (what == "hwinc") return test_hwinc(dir, write);
        if (what == "hwnames") return test_hwnames(dir);
        if (what == "datasheet") return test_datasheet(dir);
        if (what == "lcf") return test_lcf(dir);
        if (what == "z80opspace") return test_z80opspace(dir, write);
        if (what == "z80rom") return test_z80rom(dir, exe, write);
        if (what == "z80shift") return test_z80shift();
        if (what == "cart") return test_cart(dir, exe, write);
        if (what == "cases") {
            std::vector<std::string> which;
            for (int i = 4; i < argc; i++)
                if (std::string(argv[i]) != "--write") which.push_back(argv[i]);
            return test_cases(dir, exe, which, write);
        }
    } catch (std::exception& e) {
        std::cerr << "test error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << "unknown test " << what << "\n";
    return 2;
}
