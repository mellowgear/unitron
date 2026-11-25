// Golden CLI cases (tests/golden/cases.txt -> expected.txt) and the optional ROM/sample hashes.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "hash.hpp"
#include "hwnames.hpp"
#include "png.hpp"
#include "util.hpp"

using namespace unitron;
namespace fs = std::filesystem;

namespace {

struct Case {
    std::string name;
    std::vector<std::pair<std::string, Bytes>> files;
    std::vector<std::vector<std::string>> cmds;
};

// shlex.split for the simple quoting used in cases.txt
std::vector<std::string> shsplit(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in = false;
    char q = 0;
    for (char c : s) {
        if (q) {
            if (c == q) q = 0;
            else cur += c;
            continue;
        }
        if (c == '"' || c == '\'') {
            q = c;
            in = true;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (in) out.push_back(cur);
            cur.clear();
            in = false;
            continue;
        }
        cur += c;
        in = true;
    }
    if (in) out.push_back(cur);
    return out;
}

Bytes from_hex(const std::string& h) {
    Bytes b;
    std::string d;
    for (char c : h)
        if (!py_space(c)) d += c;
    for (size_t i = 0; i + 1 < d.size(); i += 2) b.push_back(uint8_t(std::stoi(d.substr(i, 2), nullptr, 16)));
    return b;
}

std::vector<Case> parse_cases(const std::string& text) {
    std::vector<Case> cases;
    struct F {
        std::string path;
        std::vector<std::string> lines;
        bool hex;
    };
    std::vector<std::vector<F>> files;
    F* f = nullptr;
    for (auto& line : split_on(text, "\n")) {
        bool marker = starts_with(line, "=== ") || starts_with(line, "--- ") || starts_with(line, "$ ");
        if (f && !marker) {
            f->lines.push_back(line);
            continue;
        }
        if (starts_with(line, "=== ")) {
            cases.push_back({strip(line.substr(4)), {}, {}});
            files.emplace_back();
            f = nullptr;
        } else if (starts_with(line, "--- ")) {
            auto parts = split_ws(line.substr(4));
            files.back().push_back({parts[0], {}, parts.size() > 1 && parts[1] == "hex"});
            f = &files.back().back();
        } else if (starts_with(line, "$ ")) {
            f = nullptr;
            cases.back().cmds.push_back(shsplit(line.substr(2)));
        } else if (!strip(line).empty() && line[0] != '#') {
            throw std::runtime_error("cases.txt: stray line " + line);
        }
    }
    for (size_t i = 0; i < cases.size(); i++)
        for (auto& fl : files[i]) {
            while (!fl.lines.empty() && fl.lines.back().empty()) fl.lines.pop_back();
            Bytes data;
            if (fl.hex) {
                data = from_hex(join(fl.lines, " "));
            } else {
                for (auto& x : fl.lines) {
                    data.insert(data.end(), x.begin(), x.end());
                    data.push_back('\n');
                }
            }
            cases[i].files.push_back({fl.path, data});
        }
    return cases;
}

std::string show_text(const std::string& t) {
    if (t.empty()) return "";
    auto lines = split_on(t, "\n");
    std::string tail = lines.back();
    lines.pop_back();
    std::string out;
    for (auto& x : lines) out += "|" + x + "\n";
    if (!tail.empty()) out += "|" + tail + "\n~\n";
    return out;
}

bool valid_utf8_no_nul(const Bytes& b) {
    std::string s(b.begin(), b.end());
    if (s.find('\0') != std::string::npos) return false;
    for (auto c : utf8_decode(s))
        if (c >= 0xDC80 && c <= 0xDCFF) return false;
    return true;
}

std::string show_bytes(const std::string& path, const Bytes& data) {
    if (ends_with(path, ".png")) {
        PngImage im = png_read(data);
        std::vector<std::string> lines = {"png " + dec(im.width) + "x" + dec(im.height) + " mode " + (im.color_type == 3 ? "P" : "?")};
        std::vector<bool> used(256, false);
        for (auto p : im.pixels) used[p] = true;
        for (int i = 0; i < 256; i++)
            if (used[i]) {
                Rgb c = size_t(i) < im.palette.size() ? im.palette[size_t(i)] : Rgb{0, 0, 0};
                lines.push_back("color " + dec(i) + " " + hexl(c[0], 2) + hexl(c[1], 2) + hexl(c[2], 2));
            }
        std::map<std::string, std::string> text;
        for (auto& t : im.text) text[t.key] = t.value;
        for (auto& kv : text) lines.push_back("text " + kv.first + " " + kv.second);
        for (uint32_t y = 0; y < im.height; y++) {
            std::string row;
            for (uint32_t x = 0; x < im.width; x++) {
                int p = im.pixels[size_t(y) * im.width + x];
                row += p < 16 ? hexl(p) : "[" + dec(p) + "]";
            }
            lines.push_back(row);
        }
        std::string out;
        for (auto& l : lines) out += "|" + l + "\n";
        return out;
    }
    if (!valid_utf8_no_nul(data)) {
        std::string h;
        for (auto b : data) h += hexl(b, 2);
        std::string out;
        for (size_t i = 0; i < h.size(); i += 64) out += "#" + h.substr(i, 64) + "\n";
        return out;
    }
    return show_text(std::string(data.begin(), data.end()));
}

std::map<std::string, Bytes> snapshot(const fs::path& d) {
    std::map<std::string, Bytes> out;
    for (auto& e : fs::recursive_directory_iterator(d)) {
        if (!e.is_regular_file()) continue;
        std::string rel = fs::relative(e.path(), d).generic_u8string();
        out[rel] = read_bytes(e.path().u8string());
    }
    return out;
}

std::string quote(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string o = "'";
    for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return o + "'";
#endif
}

struct Run {
    int code;
    Bytes out, err;
};

Run run_in(const fs::path& dir, const std::string& exe, const std::vector<std::string>& args, const fs::path& scratch) {
    fs::path so = scratch / "stdout", se = scratch / "stderr";
    std::string cmd;
#ifdef _WIN32
    cmd = "cd /d " + quote(dir.u8string()) + " && " + quote(exe);
#else
    cmd = "cd " + quote(dir.u8string()) + " && " + quote(exe);
#endif
    for (auto& a : args) cmd += " " + quote(a);
    cmd += " > " + quote(so.u8string()) + " 2> " + quote(se.u8string());
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    int st = std::system(cmd.c_str());
    Run r;
#ifdef _WIN32
    r.code = st;
#else
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
#endif
    r.out = read_bytes(so.u8string());
    r.err = read_bytes(se.u8string());
    return r;
}

fs::path fresh_dir(const std::string& tag) {
    std::random_device rd;
    fs::path p = fs::temp_directory_path() / ("unitron_test_" + tag + "_" + std::to_string(rd()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

// expected.txt -> name -> block text
std::map<std::string, std::string> expected_blocks(const std::string& text) {
    std::map<std::string, std::string> out;
    std::string cur, name;
    for (auto& line : split_on(text, "\n")) {
        if (starts_with(line, "=== ")) {
            if (!name.empty()) out[name] = cur;
            name = line.substr(4);
            cur = line + "\n";
        } else if (!name.empty()) {
            cur += line + "\n";
        }
    }
    if (!name.empty()) {
        if (ends_with(cur, "\n\n")) cur.pop_back();
        out[name] = cur;
    }
    return out;
}

// compare a produced block with the expected one; "crash" commands accept any failure
bool same_block(const std::string& want, const std::string& got, std::string& diff) {
    auto w = split_on(want, "\n"), g = split_on(got, "\n");
    size_t i = 0, j = 0;
    while (i < w.size() && j < g.size()) {
        if (w[i] == "crash") {   // skip got's output for this command
            if (j < g.size() && starts_with(g[j], "exit 0")) {
                diff = "command expected to fail succeeded";
                return false;
            }
            i++;
            j++;
            while (j < g.size() && !starts_with(g[j], "$ ") && !starts_with(g[j], "=== ")) j++;
            continue;
        }
        if (w[i] != g[j]) {
            diff = "line " + dec(int64_t(i) + 1) + ":\n  want: " + w[i] + "\n  got:  " + g[j];
            return false;
        }
        i++;
        j++;
    }
    while (i < w.size() && w[i].empty()) i++;
    while (j < g.size() && g[j].empty()) j++;
    if (i != w.size() || j != g.size()) {
        diff = "length differs: want " + dec(int64_t(w.size())) + " lines, got " + dec(int64_t(g.size()));
        return false;
    }
    return true;
}

}  // namespace

int test_golden(const std::string& dir, const std::string& exe0, bool write) {
    std::string exe = fs::absolute(fs::u8path(exe0)).u8string();
    Bytes ct = read_bytes(dir + "/cases.txt"), et = read_bytes(dir + "/expected.txt");
    auto cases = parse_cases(std::string(ct.begin(), ct.end()));
    auto want = expected_blocks(std::string(et.begin(), et.end()));
    int bad = 0;
    std::string all;
    fs::path scratch = fresh_dir("scratch");
    for (auto& c : cases) {
        fs::path d = fresh_dir("case");
        for (auto& f : c.files) {
            fs::path p = d / fs::u8path(f.first);
            fs::create_directories(p.parent_path());
            write_bytes(p.u8string(), f.second);
        }
        std::string got = "=== " + c.name + "\n";
        for (auto& args : c.cmds) {
            auto before = snapshot(d);
            Run r = run_in(d, exe, args, scratch);
            got += "$ " + join(args, " ") + "\n";
            got += "exit " + dec(r.code) + "\nstdout\n";
            if (!r.out.empty()) got += show_bytes("", r.out);
            got += "stderr\n" + show_text(std::string(r.err.begin(), r.err.end()));
            auto after = snapshot(d);
            for (auto& kv : after) {
                auto b = before.find(kv.first);
                if (b == before.end() || b->second != kv.second) got += "file " + kv.first + "\n" + show_bytes(kv.first, kv.second);
            }
        }
        fs::remove_all(d);
        all += got;
        std::string diff;
        auto w = want.find(c.name);
        if (w == want.end()) {
            std::cerr << "golden " << c.name << ": no expected output\n";
            bad++;
        } else if (!same_block(w->second, got, diff)) {
            std::cerr << "golden " << c.name << ": " << diff << "\n";
            bad++;
        }
    }
    fs::remove_all(scratch);
    std::cout << "golden: " << cases.size() << " cases, " << bad << " differ\n";
    if (write) {
        write_text(dir + "/expected.txt", all);
        std::cout << "rewrote expected.txt\n";
        return 0;
    }
    return bad ? 1 : 0;
}

int test_hwinc(const std::string& data_dir, bool write) {
    std::string path = data_dir + "/ngpc_hw.inc";
    std::string want = render_hw_inc();
    if (write) {
        write_text(path, want);
        std::cout << "wrote " << path << "\n";
        return 0;
    }
    Bytes have;
    bool ok = read_bytes_opt(path, have) && std::string(have.begin(), have.end()) == want;
    std::cout << "ngpc_hw.inc: " << (ok ? "up to date" : "STALE (run unitron_tests hwinc DATA_DIR --write)") << "\n";
    return ok ? 0 : 1;
}

// Optional: needs the the reference ROM ROM ($UNITRON_ROM, else ../roms/rom1/rom1.ngp next to the
// repo) and/or Toshiba's samples. Compares sha256 of every output with tests/vectors/rom_hashes.txt.
int test_rom(const std::string& vdir, const std::string& exe0) {
    std::string exe = fs::absolute(fs::u8path(exe0)).u8string();
    fs::path repo = fs::weakly_canonical(fs::absolute(fs::u8path(vdir))).parent_path().parent_path();
    const char* env = std::getenv("UNITRON_ROM");
    fs::path rom = env ? fs::u8path(env) : repo.parent_path() / "roms/rom1/rom1.ngp";
    fs::path samples = repo.parent_path() / "ngpc-project-template/ngpcbins/T900/SAMPLE/ASM900";
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
                {"roundtrip", {"roundtrip", "rom.ngp"}},
                {"lift", {"lift", "rom.ngp", "-o", "lifted"}}};
            for (auto& c : cmds) {
                Run r = run_in(d, exe, c.second, scratch);
                check(c.first, "exit " + dec(r.code) + " stdout " + sha256_hex(r.out) + " stderr " + sha256_hex(r.err));
            }
            for (auto f : {"src/main.asm", "src/hw.inc", "src_ns/main.asm", "lifted/game.c", "lifted/ngpc_regs.h", "lifted/ngpc_rt.h"}) {
                Bytes b;
                read_bytes_opt((d / f).u8string(), b);
                check(std::string("file ") + f, sha256_hex(b));
            }
        }
    } else {
        std::cout << "rom: no ROM (set UNITRON_ROM); skipped\n";
    }
    if (fs::is_regular_file(samples / "SAMPLE1.ASM")) {
        for (auto f : {"SAMPLE1.ASM", "SAMPLE2.ASM"}) fs::copy_file(samples / f, d / f, fs::copy_options::overwrite_existing);
        Run r = run_in(d, exe,
                       {"asm", "SAMPLE1.ASM", "SAMPLE2.ASM", "-o", "s.bin", "--origin", "0x8000", "--size", "0x500", "--place", "MAIN=0x8100",
                        "--place", "TIME30=0x8200", "--place", "subrout=0x8300", "--place", "ROM_DAT=0x8400", "--sym", "s.sym"},
                       scratch);
        check("samples", "exit " + dec(r.code) + " stdout " + sha256_hex(r.out) + " stderr " + sha256_hex(r.err));
        for (auto f : {"s.bin", "s.sym"}) {
            Bytes b;
            read_bytes_opt((d / f).u8string(), b);
            check(std::string("file ") + f, sha256_hex(b));
        }
        for (auto f : {"SAMPLE1.ASM", "SAMPLE2.ASM"}) {
            // rom_hashes.txt has one "check FILE exit N stderr SHA" line per sample
            Run c = run_in(d, exe, {"asm", f, "--no-link"}, scratch);
            std::string got = "check " + std::string(f) + " exit " + dec(c.code) + " stderr " + sha256_hex(c.err);
            bool found = false;
            for (auto& line : split_on(std::string(ht.begin(), ht.end()), "\n")) found |= line == got;
            if (!found) {
                std::cerr << got << ": not in rom_hashes.txt\n";
                bad++;
            }
            done++;
        }
    } else {
        std::cout << "samples: Toshiba samples not present; skipped\n";
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
// $UNITRON_ROM / $UNITRON_ROM2, else ../roms/ next to the repo.
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
    std::vector<Drv> drv = {{"rom1", "UNITRON_ROM", "roms/rom1/rom1.ngp"},
                            {"rom2", "UNITRON_ROM2", "roms/rom2.ngp"}};
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
        fs::path rom = env ? fs::u8path(env) : repo.parent_path() / fs::u8path(v.rel);
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
