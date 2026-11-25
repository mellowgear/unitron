// The test cartridge (tests/cart/): a small NGPC program of our own in asm900, with a Z80 program
// of our own, so the trace / dis / round-trip paths run without any commercial ROM.
//   unitron_tests cart CART_DIR UNITRON_EXE [--write]
// In a fresh directory with the cartridge's files it:
// 1. builds it: z80 asm (with --equ/--db), bin2db of a tile file the test writes, text, then asm
//    of the three modules with cart.lcf;
// 2. checks that every `code ADDR NAME` line of hints.txt and sound.hints still names that
//    label's address in the symbol files (so the hints follow the source);
// 3. runs dis (listings), dis -o with the sweep, with --hints and with --no-sweep, and assembles
//    each source back: each must give the image byte for byte, and no branch or call in it may be
//    written as a bare number (every target is labelled); roundtrip with and without hints;
// 4. runs z80 dis (listings, -o with and without hints, assembled back), z80 roundtrip, and the
//    z80shift check (tests/cpp/z80.cpp) on the Z80 image.
// Then it compares with tests/cart/expected/: commands.txt (every command with its exit code,
// stdout and stderr), files.txt (size and sha256 of the image, the Z80 image and the other
// generated files, and the z80shift result) and, as full text, every source dis and z80 dis wrote
// `--write` rewrites expected/ from this build; review `git diff tests/` after.
// On a difference the work directory is kept and its path printed.
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "hash.hpp"
#include "testutil.hpp"
#include "util.hpp"

using namespace unitron;
namespace fs = std::filesystem;

std::string z80_shift_check(const Bytes& img, const std::string& name, const std::string& workdir, std::string& info);   // z80.cpp

namespace {

// The tiles gfx.asm includes (through bin2db): 37 tiles of 16 bytes; byte k of tile n is
// (n * 37 + k * 11) & 0xFF, tile 0 (the space) is all zeros. A pattern, not a font.
Bytes make_tiles() {
    Bytes b;
    for (int n = 0; n < 37; n++)
        for (int k = 0; k < 16; k++) b.push_back(n == 0 ? 0 : uint8_t((n * 37 + k * 11) & 0xFF));
    return b;
}

std::string text_of(const Bytes& b) { return std::string(b.begin(), b.end()); }

// "ADDR NAME" lines (asm --sym, z80 asm --sym) -> name -> address
std::map<std::string, int64_t> read_sym(const fs::path& p) {
    std::map<std::string, int64_t> m;
    Bytes b;
    if (!read_bytes_opt(p.u8string(), b)) return m;
    for (auto& line : split_on(text_of(b), "\n")) {
        auto w = split_ws(line);
        if (w.size() == 2) m[w[1]] = int64_t(std::stoll(w[0], nullptr, 16));
    }
    return m;
}

// every `code ADDR NAME` line of a hints file must be NAME's address in the symbol file
int check_hints(const fs::path& hints, const std::map<std::string, int64_t>& sym, std::string& log) {
    Bytes b = read_bytes(hints.u8string());
    int bad = 0, n = 0;
    for (auto& line0 : split_on(text_of(b), "\n")) {
        std::string line = line0.substr(0, std::min(line0.find('#'), line0.find(';')));
        auto w = split_ws(line);
        if (w.size() != 3 || w[0] != "code") continue;
        n++;
        int64_t a = int64_t(std::stoll(w[1], nullptr, 16));
        auto it = sym.find(w[2]);
        if (it == sym.end() || it->second != a) {
            std::cerr << hints.filename().u8string() << ": code " << w[1] << " " << w[2] << ": the source has "
                      << (it == sym.end() ? "no such label" : w[2] + " at " + hexs(it->second)) << "\n";
            bad++;
        }
    }
    log += "hints " + hints.filename().u8string() + ": " + dec(n) + " code lines, " + dec(bad) + " stale\n";
    return bad;
}

// branch and call lines whose target is a bare number (`jr Z,0x200085`, `call 0x2005AA`). dis gives
// every target that is an instruction start a label, and the cartridge has no branch into data, so
// there must be none.
std::vector<std::string> bare_branches(const std::string& text) {
    std::vector<std::string> out;
    for (auto& line : split_on(text, "\n")) {
        auto w = split_on(line, "\t");
        if (w.size() != 3 || !w[0].empty()) continue;
        if (w[1] != "jr" && w[1] != "jrl" && w[1] != "jp" && w[1] != "call" && w[1] != "calr" && w[1] != "djnz") continue;
        auto ops = split_on(w[2], ",");
        if (starts_with(ops.back(), "0x")) out.push_back(line);
    }
    return out;
}

// the first line where two texts differ, for the error message
std::string first_diff(const std::string& want, const std::string& got) {
    auto w = split_on(want, "\n"), g = split_on(got, "\n");
    for (size_t i = 0; i < std::max(w.size(), g.size()); i++) {
        std::string a = i < w.size() ? w[i] : "(end)", b = i < g.size() ? g[i] : "(end)";
        if (a != b) return "line " + dec(int64_t(i) + 1) + ":\n  want: " + a + "\n  got:  " + b;
    }
    return "";
}

}  // namespace

int test_cart(const std::string& cart_dir, const std::string& exe0, bool write) {
    std::string exe = fs::absolute(fs::u8path(exe0)).u8string();
    fs::path src = fs::weakly_canonical(fs::absolute(fs::u8path(cart_dir)));
    fs::path exp = src / "expected", data = src.parent_path().parent_path() / "data";
    fs::path d = fresh_dir("cart"), scratch = fresh_dir("cartscratch");
    for (auto& e : fs::directory_iterator(src))
        if (e.is_regular_file()) fs::copy_file(e.path(), d / e.path().filename());
    write_bytes((d / "tiles.bin").u8string(), make_tiles());

    int bad = 0;
    std::string log;   // commands.txt
    auto run = [&](const std::vector<std::string>& args) {
        Run r = run_in(d, exe, args, scratch);
        std::string shown;
        for (auto& a : args) shown += (shown.empty() ? "" : " ") + (a == data.u8string() ? std::string("DATA") : a);
        std::string out = text_of(r.out), err = text_of(r.err);
        log += "$ " + shown + "\nexit " + dec(r.code) + "\nstdout\n" + out + (out.empty() || out.back() == '\n' ? "" : "\n") + "stderr\n" + err +
               (err.empty() || err.back() == '\n' ? "" : "\n");
        if (r.code != 0) {
            std::cerr << "cart: `" << shown << "` exited " << r.code << ": " << err;
            bad++;
        }
    };
    auto same = [&](const std::string& a, const std::string& b) {
        Bytes x, y;
        bool ok = read_bytes_opt((d / a).u8string(), x) && read_bytes_opt((d / b).u8string(), y) && x == y;
        log += "compare " + a + " " + b + ": " + (ok ? "byte-identical" : "DIFFERENT") + "\n";
        if (!ok) {
            std::cerr << "cart: " << b << " is not byte-identical to " << a << "\n";
            bad++;
        }
    };
    const std::vector<std::string> place = {"--origin", "0x200000", "--size", "0x8000"};
    auto with = [](std::vector<std::string> a, const std::vector<std::string>& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };

    // 1. build
    run({"z80", "asm", "sound.z80", "-o", "sound.bin", "--equ", "sound.inc", "--db", "sound.db", "--sym", "sound.sym"});
    run({"bin2db", "tiles.bin", "-o", "tiles.db"});
    run({"text", "charmap.txt", "strings.txt", "-o", "strings.db"});
    run(with({"asm", "main.asm", "sound.asm", "gfx.asm", "--lcf", "cart.lcf", "-I", data.u8string(), "-o", "cart.ngp", "--sym", "cart.sym"}, place));
    // 2. the hints still match the source
    bad += check_hints(d / "hints.txt", read_sym(d / "cart.sym"), log);
    bad += check_hints(d / "sound.hints", read_sym(d / "sound.sym"), log);
    // 3. the main CPU's tools
    run({"dis", "cart.ngp"});
    run({"dis", "cart.ngp", "--start", "0x200580", "--count", "40"});
    for (auto& v : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"src", {}}, {"src_hints", {"--hints", "hints.txt"}}, {"src_nosweep", {"--no-sweep"}}}) {
        run(with({"dis", "cart.ngp", "-o", v.first}, v.second));
        run(with({"asm", v.first + "/main.asm", "-o", v.first + ".ngp"}, place));
        same("cart.ngp", v.first + ".ngp");
        Bytes b;
        read_bytes_opt((d / v.first / "main.asm").u8string(), b);
        auto bare = bare_branches(text_of(b));
        log += v.first + "/main.asm: " + dec(int64_t(bare.size())) + " branches to a bare number\n";
        for (auto& l : bare) {
            std::cerr << "cart: " << v.first << "/main.asm: a branch to a bare number:" << l << "\n";
            bad++;
        }
    }
    run({"roundtrip", "cart.ngp"});
    run({"roundtrip", "cart.ngp", "--hints", "hints.txt"});
    // 4. the Z80's tools
    run({"z80", "dis", "sound.bin"});
    run({"z80", "dis", "sound.bin", "--hints", "sound.hints"});
    for (auto& v : std::vector<std::pair<std::string, std::vector<std::string>>>{{"zsrc", {}}, {"zsrc_hints", {"--hints", "sound.hints"}}}) {
        run(with({"z80", "dis", "sound.bin", "-o", v.first}, v.second));
        run({"z80", "asm", v.first + "/main.z80", "-o", v.first + ".bin"});
        same("sound.bin", v.first + ".bin");
    }
    run({"z80", "roundtrip", "sound.bin"});
    run({"z80", "roundtrip", "sound.bin", "--hints", "sound.hints"});

    std::string files;   // files.txt
    {
        Bytes z;
        std::string info, err = "no sound.bin";
        if (read_bytes_opt((d / "sound.bin").u8string(), z)) {
            fs::path sd = fresh_dir("cartshift");
            err = z80_shift_check(z, "sound.bin", sd.u8string(), info);
            fs::remove_all(sd);
        }
        if (!err.empty()) {
            std::cerr << "cart: z80shift on sound.bin: " << err << "\n";
            bad++;
        }
        files += "z80shift sound.bin: " + (err.empty() ? info : "FAILED") + "\n";
    }
    // the generated files that are not ours to read line by line, by size and sha256
    for (auto f : {"cart.ngp", "cart.sym", "sound.bin", "sound.sym", "sound.inc", "sound.db", "tiles.db", "strings.db"}) {
        Bytes b;
        bool ok = read_bytes_opt((d / f).u8string(), b);
        files += std::string(f) + " " + (ok ? "size 0x" + hexs(int64_t(b.size())) + " sha256 " + sha256_hex(b) : std::string("missing")) + "\n";
    }

    // compare (or write) expected/
    std::vector<std::pair<std::string, std::string>> outs = {{"commands.txt", log}, {"files.txt", files}};
    for (auto f : {"src/main.asm", "src/hw.inc", "src_hints/main.asm", "src_hints/hw.inc", "src_nosweep/main.asm", "src_nosweep/hw.inc",
                   "zsrc/main.z80", "zsrc_hints/main.z80"}) {
        Bytes b;
        read_bytes_opt((d / f).u8string(), b);
        outs.push_back({f, text_of(b)});
    }
    for (auto& o : outs) {
        fs::path p = exp / fs::u8path(o.first);
        if (write) {
            fs::create_directories(p.parent_path());
            write_text(p.u8string(), o.second);
            continue;
        }
        Bytes want;
        if (!read_bytes_opt(p.u8string(), want)) {
            std::cerr << "cart: missing " << p.u8string() << " (run with --write)\n";
            bad++;
        } else if (text_of(want) != o.second) {
            std::cerr << "cart: expected/" << o.first << " differs, " << first_diff(text_of(want), o.second) << "\n";
            bad++;
        }
    }
    fs::remove_all(scratch);
    if (write) {
        std::cout << "cart: rewrote " << exp.u8string() << " (" << outs.size() << " files)\n";
    } else {
        std::cout << "cart: " << outs.size() << " expected files, " << bad << " problem(s)\n";
    }
    if (bad && !write) {
        std::cout << "cart: the outputs are kept in " << d.u8string() << "\n";
    } else {
        fs::remove_all(d);
    }
    return bad ? 1 : 0;
}
