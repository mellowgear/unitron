// The optional ROM tests: a reference commercial ROM by hash (rom), and the Z80 sound drivers of two
// commercial ROMs by hash (z80rom). Both skip what is absent. (Toshiba's sample programs are the
// case tests/toshiba/samples.)
#include <cstdlib>
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

// Optional: needs the reference ROM ($UNITRON_ROM; the ROM whose sha256 is in rom_hashes.txt).
// Compares sha256 of every output with tests/vectors/rom_hashes.txt.
int test_rom(const std::string& vdir, const std::string& exe0) {
    std::string exe = fs::absolute(fs::u8path(exe0)).u8string();
    const char* env = std::getenv("UNITRON_ROM");
    fs::path rom = env ? fs::u8path(env) : fs::path();
    Bytes ht = read_bytes(vdir + "/rom_hashes.txt");
    std::map<std::string, std::string> want;
    for (auto& line : split_on(std::string(ht.begin(), ht.end()), "\n")) {
        auto w = split_ws(line);
        if (w.empty()) continue;
        if (w[0] == "file") want["file " + w[1]] = w[2];
        else if (w[0] == "rom") want["rom"] = w[2];
        else want[w[0]] = line.substr(w[0].size() + 1);
    }
    int bad = 0, done = 0;
    fs::path d = fresh_dir("rom"), scratch = fresh_dir("romscratch");
    auto check = [&](const std::string& key, const std::string& got) {
        auto it = want.find(key);
        if (it == want.end() || it->second != got) {
            std::cerr << key << ": want " << (it == want.end() ? "(none)" : it->second) << "\n" << std::string(key.size(), ' ') << "  got  " << got << "\n";
            bad++;
        }
        done++;
    };
    Bytes rb;
    if (want.count("rom") && read_bytes_opt(rom.u8string(), rb)) {
        if (sha256_hex(rb) != want["rom"]) {
            std::cout << "rom: a different ROM than the frozen hashes (skipped)\n";
        } else {
            write_bytes((d / "rom.ngp").u8string(), rb);
            std::vector<std::pair<std::string, std::vector<std::string>>> cmds = {
                {"dis_listing", {"dis", "rom.ngp", "--count", "4000"}},
                {"dis_o", {"dis", "rom.ngp", "-o", "src"}},
                {"dis_nosweep", {"dis", "rom.ngp", "-o", "src_ns", "--no-sweep"}},
                {"roundtrip", {"roundtrip", "rom.ngp"}}};
            for (auto& c : cmds) {
                Run r = run_in(d, exe, c.second, scratch);
                check(c.first, "exit " + dec(r.code) + " stdout " + sha256_hex(r.out) + " stderr " + sha256_hex(r.err));
            }
            for (auto f : {"src/main.asm", "src/hw.inc", "src/cpu.inc", "src/system.inc", "src/k2ge.inc", "src_ns/main.asm"}) {
                Bytes b;
                read_bytes_opt((d / f).u8string(), b);
                check(std::string("file ") + f, sha256_hex(b));
            }
        }
    } else {
        std::cout << "rom: no ROM (set UNITRON_ROM); skipped\n";
    }
    fs::remove_all(d);
    fs::remove_all(scratch);
    std::cout << "rom: " << done << " checks, " << bad << " differ\n";
    return bad ? 1 : 0;
}

std::string z80_shift_check(const Bytes& img, const std::string& name, const std::string& workdir, std::string& info);   // z80.cpp

// Optional: the Z80 sound drivers in two commercial ROMs (not ours, so only hashes are kept, in
// tests/vectors/z80_rom_hashes.txt). Each driver is cut out of its ROM (offset and length in that
// file), checked by sha256, then `z80 dis` (listing), `z80 dis -o` and `z80 roundtrip` run on it and
// every output is compared by sha256, and the z80shift check (tests/cpp/z80.cpp) runs on it. A ROM that is absent is skipped. The ROMs are found at
// $UNITRON_ROM / $UNITRON_ROM2.
int test_z80rom(const std::string& vdir, const std::string& exe0, bool write) {
    std::string exe = fs::absolute(fs::u8path(exe0)).u8string();
    fs::path repo = fs::weakly_canonical(fs::absolute(fs::u8path(vdir))).parent_path().parent_path();
    std::string hpath = vdir + "/z80_rom_hashes.txt";
    Bytes ht = read_bytes(hpath);
    std::vector<std::string> lines = split_on(std::string(ht.begin(), ht.end()), "\n");
    struct Drv {
        std::string name, env, rel;
        int64_t off = -1, len = 0;
        std::string sha;
    };
    std::vector<Drv> drv = {{"rom1", "UNITRON_ROM", ""}, {"rom2", "UNITRON_ROM2", ""}};
    std::map<std::string, std::string> want;
    for (auto& line : lines) {
        auto w = split_ws(line);
        if (w.size() < 2 || w[0][0] == '#') continue;
        if (w[0] == "driver" && w.size() == 8) {
            for (auto& d : drv)
                if (d.name == w[1]) {
                    d.off = py_int0(w[3]);
                    d.len = py_int0(w[5]);
                    d.sha = w[7];
                }
            continue;
        }
        want[w[0] + " " + w[1]] = line.substr(w[0].size() + w[1].size() + 2);
    }
    int bad = 0, done = 0;
    std::map<std::string, std::string> got;
    fs::path d = fresh_dir("z80rom"), scratch = fresh_dir("z80romscratch");
    for (auto& v : drv) {
        const char* env = std::getenv(v.env.c_str());
        fs::path rom = env ? fs::u8path(env) : fs::path();
        Bytes rb;
        if (v.off < 0 || !read_bytes_opt(rom.u8string(), rb)) {
            std::cout << "z80rom " << v.name << ": no ROM (set " << v.env << "); skipped\n";
            continue;
        }
        if (int64_t(rb.size()) < v.off + v.len || sha256_hex(rb.data() + v.off, size_t(v.len)) != v.sha) {
            std::cout << "z80rom " << v.name << ": the driver bytes differ from the frozen sha256 (a different ROM?); skipped\n";
            continue;
        }
        std::string bin = v.name + ".bin";
        write_bytes((d / bin).u8string(), Bytes(rb.begin() + v.off, rb.begin() + v.off + v.len));
        std::vector<std::pair<std::string, std::vector<std::string>>> cmds = {
            {"listing", {"z80", "dis", bin}}, {"dis_o", {"z80", "dis", bin, "-o", "src_" + v.name}}, {"roundtrip", {"z80", "roundtrip", bin}}};
        for (auto& c : cmds) {
            Run r = run_in(d, exe, c.second, scratch);
            got[v.name + " " + c.first] = "exit " + dec(r.code) + " stdout " + sha256_hex(r.out) + " stderr " + sha256_hex(r.err);
        }
        Bytes src;
        read_bytes_opt((d / ("src_" + v.name) / "main.z80").u8string(), src);
        got[v.name + " main.z80"] = sha256_hex(src);
        // z80shift on the driver: one NOP in its source moves every labeled reference by one
        {
            fs::path sd = fresh_dir("z80shift");
            std::string info, err = z80_shift_check(Bytes(rb.begin() + v.off, rb.begin() + v.off + v.len), bin, sd.u8string(), info);
            fs::remove_all(sd);
            done++;
            if (!err.empty()) {
                std::cerr << v.name << " shift: " << err << "\n";
                bad++;
            } else {
                std::cout << "z80rom " << v.name << " shift: " << info << "\n";
            }
        }
        for (auto& kv : got) {
            if (!starts_with(kv.first, v.name + " ")) continue;
            auto it = want.find(kv.first);
            done++;
            if (it == want.end() || it->second != kv.second) {
                std::cerr << kv.first << ": want " << (it == want.end() ? "(none)" : it->second) << "\n" << std::string(kv.first.size(), ' ')
                          << "  got  " << kv.second << "\n";
                bad++;
            }
        }
    }
    fs::remove_all(d);
    fs::remove_all(scratch);
    if (write) {
        std::string out;
        for (auto& line : lines) {
            auto w = split_ws(line);
            if (!w.empty() && w[0][0] != '#' && w[0] != "driver" && got.count(w[0] + " " + (w.size() > 1 ? w[1] : ""))) continue;
            if (line.empty()) continue;
            out += line + "\n";
        }
        for (auto& kv : got) out += kv.first + " " + kv.second + "\n";
        write_text(hpath, out);
        std::cout << "rewrote " << hpath << "\n";
        return 0;
    }
    std::cout << "z80rom: " << done << " checks, " << bad << " differ\n";
    return bad ? 1 : 0;
}
