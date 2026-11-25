// unitron C++ tests against the vectors frozen from the Python reference (legacy/python/tools/freeze_vectors.py).
//   unitron_tests opspace VECTOR_DIR
//   unitron_tests lines VECTOR_DIR
//   unitron_tests golden GOLDEN_DIR UNITRON_EXE
//   unitron_tests rom VECTOR_DIR UNITRON_EXE      (needs $UNITRON_ROM or ../roms/rom1/rom1.ngp)
//   unitron_tests hwinc DATA_DIR
//   unitron_tests z80opspace VECTOR_DIR
//   unitron_tests z80rom VECTOR_DIR UNITRON_EXE   (the reference ROM and/or the second ROM; skipped when absent)
//   unitron_tests z80shift
// Append --write to rewrite the expected outputs after a deliberate behavior change.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

int test_opspace(const std::string& dir, bool write);
int test_lines(const std::string& dir, bool write);
int test_golden(const std::string& dir, const std::string& exe, bool write);
int test_rom(const std::string& dir, const std::string& exe);
int test_hwinc(const std::string& dir, bool write);
int test_lcf(const std::string& dir);
int test_z80opspace(const std::string& dir, bool write);
int test_z80rom(const std::string& dir, const std::string& exe, bool write);
int test_z80shift();

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: unitron_tests opspace|lines|golden|rom DIR [UNITRON_EXE]\n";
        return 2;
    }
    std::string what = argv[1], dir = argv[2], exe = argc > 3 ? argv[3] : "";
    // --write (last argument): rewrite the expected outputs from this build (after a deliberate change)
    bool write = std::string(argv[argc - 1]) == "--write";
    try {
        if (what == "opspace") return test_opspace(dir, write);
        if (what == "lines") return test_lines(dir, write);
        if (what == "golden") return test_golden(dir, exe, write);
        if (what == "rom") return test_rom(dir, exe);
        if (what == "hwinc") return test_hwinc(dir, write);
        if (what == "lcf") return test_lcf(dir);
        if (what == "z80opspace") return test_z80opspace(dir, write);
        if (what == "z80rom") return test_z80rom(dir, exe, write);
        if (what == "z80shift") return test_z80shift();
    } catch (std::exception& e) {
        std::cerr << "test error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << "unknown test " << what << "\n";
    return 2;
}
