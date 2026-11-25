// unitron command line: asm, mac, dis, roundtrip, lz, gfx, bin2db, text, lift, z80 (dis, asm, roundtrip).
// Same commands, options, messages and exit codes as the Python version it replaces.
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "assembler.hpp"
#include "bin2db.hpp"
#include "charmap.hpp"
#include "hash.hpp"
#if __has_include("lift.hpp") && !defined(UNITRON_NO_LIFT)
#include "lift.hpp"
#define HAVE_LIFT 1
#endif
#include "lzss.hpp"
#include "mpp.hpp"
#include "tiles.hpp"
#include "util.hpp"
#include "writer.hpp"
#include "z80.hpp"
#include "z80asm.hpp"
#include "z80dis.hpp"

using namespace unitron;
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- a small argparse
struct Opt {
    std::vector<std::string> flags;   // "-o", "--output"
    std::string dest, metavar, help;
    bool takes = true, repeat = false;
    std::vector<std::string> choices;
};
struct Pos {
    std::string dest, help;
    char nargs = 1;   // 1, '+', '?'
    std::vector<std::string> choices;
};

struct UsageError {
    std::string msg;
};

struct Args {
    std::map<std::string, std::vector<std::string>> v;
    bool has(const std::string& k) const { return v.count(k) && !v.at(k).empty(); }
    std::string get(const std::string& k, const std::string& def = "") const { return has(k) ? v.at(k).back() : def; }
    const std::vector<std::string>& all(const std::string& k) const {
        static const std::vector<std::string> none;
        return v.count(k) ? v.at(k) : none;
    }
};

struct Parser {
    std::string prog, help;
    std::vector<Opt> opts;
    std::vector<Pos> pos;

    std::string usage() const {
        std::string u = "usage: " + prog + " [-h]";
        for (auto& o : opts) {
            std::string f = o.flags.front();
            u += " [" + f + (o.takes ? " " + (o.metavar.empty() ? upper(o.dest) : o.metavar) : "") + "]";
        }
        for (auto& p : pos) u += p.nargs == '+' ? " " + p.dest + " [" + p.dest + " ...]" : p.nargs == '?' ? " [" + p.dest + "]" : " " + p.dest;
        return u + "\n";
    }
    std::string full_help() const {
        std::string h = usage() + "\n" + (help.empty() ? "" : help + "\n\n");
        if (!pos.empty()) {
            h += "positional arguments:\n";
            for (auto& p : pos) h += "  " + p.dest + (p.help.empty() ? "" : "\t" + p.help) + "\n";
            h += "\n";
        }
        h += "options:\n  -h, --help\tshow this help message and exit\n";
        for (auto& o : opts) {
            h += "  " + join(o.flags, ", ") + (o.takes ? " " + (o.metavar.empty() ? upper(o.dest) : o.metavar) : "");
            if (!o.help.empty()) h += "\t" + o.help;
            h += "\n";
        }
        return h;
    }
    [[noreturn]] void fail(const std::string& m) const { throw UsageError{usage() + prog + ": error: " + m + "\n"}; }

    const Opt* find(const std::string& a) const {
        for (auto& o : opts)
            for (auto& f : o.flags)
                if (f == a) return &o;
        if (starts_with(a, "--")) {   // unique prefix (argparse allow_abbrev)
            const Opt* hit = nullptr;
            std::vector<std::string> cands;
            for (auto& o : opts)
                for (auto& f : o.flags)
                    if (starts_with(f, a)) {
                        if (hit != &o) cands.push_back(f);
                        hit = &o;
                    }
            if (cands.size() == 1) return hit;
            if (cands.size() > 1) fail("ambiguous option: " + a + " could match " + join(cands, ", "));
        }
        return nullptr;
    }

    Args parse(const std::vector<std::string>& argv) const {
        Args r;
        std::vector<std::string> positional;
        bool only_pos = false;
        for (size_t i = 0; i < argv.size(); i++) {
            std::string a = argv[i];
            if (only_pos || a.size() < 2 || a[0] != '-' || (a[0] == '-' && py_digit(a[1]))) {
                positional.push_back(a);
                continue;
            }
            if (a == "--") {
                only_pos = true;
                continue;
            }
            if (a == "-h" || a == "--help") {
                std::cout << full_help();
                std::exit(0);
            }
            std::string val;
            bool has_val = false;
            const Opt* o = nullptr;
            if (starts_with(a, "--")) {
                size_t eq = a.find('=');
                if (eq != std::string::npos) {
                    val = a.substr(eq + 1);
                    has_val = true;
                    a = a.substr(0, eq);
                }
                o = find(a);
            } else if ((o = find(a)) != nullptr && a.size() > 2) {
                // a whole short option such as -MG (before -M with the value "G")
            } else {
                o = find(a.substr(0, 2));
                if (o && a.size() > 2) {
                    val = a.substr(2);
                    has_val = true;
                    if (!o->takes) o = nullptr;
                }
                if (!o) o = find(a);
            }
            if (!o) fail("unrecognized arguments: " + argv[i]);
            if (!o->takes) {
                if (has_val) fail("argument " + join(o->flags, "/") + ": ignored explicit argument '" + val + "'");
                r.v[o->dest].push_back("1");
                continue;
            }
            if (!has_val) {
                if (i + 1 >= argv.size() || (argv[i + 1].size() > 1 && argv[i + 1][0] == '-' && !py_digit(argv[i + 1][1])))
                    fail("argument " + join(o->flags, "/") + ": expected one argument");
                val = argv[++i];
            }
            if (!o->choices.empty() && std::find(o->choices.begin(), o->choices.end(), val) == o->choices.end())
                fail("argument " + join(o->flags, "/") + ": invalid choice: '" + val + "' (choose from " + join(o->choices, ", ") + ")");
            if (!o->repeat) r.v[o->dest].clear();
            r.v[o->dest].push_back(val);
        }
        size_t k = 0;
        std::vector<std::string> missing;
        for (size_t pi = 0; pi < pos.size(); pi++) {
            auto& p = pos[pi];
            size_t left_needed = 0;
            for (size_t q = pi + 1; q < pos.size(); q++) left_needed += pos[q].nargs == '?' ? 0 : 1;
            if (p.nargs == '+') {
                if (k + left_needed >= positional.size()) {
                    missing.push_back(p.dest);
                    continue;
                }
                while (k + left_needed < positional.size()) r.v[p.dest].push_back(positional[k++]);
            } else if (p.nargs == '?') {
                if (k + left_needed < positional.size()) r.v[p.dest].push_back(positional[k++]);
            } else {
                if (k >= positional.size()) {
                    missing.push_back(p.dest);
                    continue;
                }
                const std::string& val = positional[k++];
                if (!p.choices.empty() && std::find(p.choices.begin(), p.choices.end(), val) == p.choices.end())
                    fail("argument " + p.dest + ": invalid choice: '" + val + "' (choose from " + join(p.choices, ", ") + ")");
                r.v[p.dest].push_back(val);
            }
        }
        if (!missing.empty()) fail("the following arguments are required: " + join(missing, ", "));
        if (k < positional.size()) {
            std::vector<std::string> rest(positional.begin() + long(k), positional.end());
            fail("unrecognized arguments: " + join(rest, " "));
        }
        return r;
    }
};

int64_t num_arg(const Parser& p, const Args& a, const std::string& k, const std::string& flag, int64_t def) {
    if (!a.has(k)) return def;
    try {
        return py_int0(a.get(k));
    } catch (std::exception&) {
        p.fail("argument " + flag + ": invalid num value: '" + a.get(k) + "'");
    }
}
int64_t int_arg(const Parser& p, const Args& a, const std::string& k, const std::string& flag, int64_t def) {
    if (!a.has(k)) return def;
    try {
        return py_int10(a.get(k));
    } catch (std::exception&) {
        p.fail("argument " + flag + ": invalid int value: '" + a.get(k) + "'");
    }
}

Defines defines_of(const Args& a) {
    Defines d;
    std::map<std::string, size_t> at;
    for (auto& s : a.all("define")) {
        size_t eq = s.find('=');
        std::string k = s.substr(0, eq), v = eq == std::string::npos ? "" : s.substr(eq + 1);
        if (v.empty()) v = "1";
        if (at.count(k)) d[at[k]].second = v;
        else {
            at[k] = d.size();
            d.push_back({k, v});
        }
    }
    return d;
}

void out_text(const Args& a, const std::string& text) {
    if (a.has("output")) write_text(a.get("output"), text);
    else std::cout << text;
}

// A file name in a make rule: `$` doubled; space, `#`, `:`, `%` and `\` get a backslash (as another assembler).
std::string make_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '$') o += '$';
        else if (c == ' ' || c == '#' || c == ':' || c == '%' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

// asm -M FILE ("-" = stdout). With a target (-o or -MT): one "target: file" line per file read,
// and with -MP an empty rule "file:" for each include file that exists. Without a target: the
// files alone, one per line (for $(shell ...) in a Makefile).
void write_deps(const Args& a, const Assembler& as, const std::string& target) {
    std::vector<std::string> files = as.deps;
    if (a.has("lcf") && std::find(files.begin(), files.end(), path_norm(a.get("lcf"))) == files.end())
        files.push_back(path_norm(a.get("lcf")));
    std::vector<std::string> sources;
    for (auto& s : a.all("sources")) sources.push_back(path_norm(s));
    std::string out;
    for (auto& f : files) {
        if (target.empty()) {
            out += f + "\n";
            continue;
        }
        out += target + ": " + make_escape(f) + "\n";
        if (a.has("dep_phony") && std::find(sources.begin(), sources.end(), f) == sources.end() && is_file(f))
            out += make_escape(f) + ":\n";
    }
    if (a.get("depfile") == "-") std::cout << out;
    else write_text(a.get("depfile"), out);
}

// ---------------------------------------------------------------- commands
int cmd_asm(const Parser& p, const Args& a) {
    INTEL_SUFFIX = a.has("intel");
    std::map<std::string, int64_t> places;
    for (auto& s : a.all("place")) {
        size_t eq = s.find('=');
        if (eq == std::string::npos || eq + 1 == s.size()) p.fail("argument --place: --place NAME=ADDR");
        try {
            places[s.substr(0, eq)] = py_int0(s.substr(eq + 1));
        } catch (std::exception&) {
            p.fail("argument --place: invalid _place value: '" + s + "'");
        }
    }
    std::optional<int64_t> origin, size;
    if (a.has("origin")) origin = num_arg(p, a, "origin", "--origin", 0);
    if (a.has("size")) size = num_arg(p, a, "size", "--size", 0);
    int64_t fill = num_arg(p, a, "fill", "--fill", 0xFF);
    int64_t data_base = num_arg(p, a, "data_base", "--data-base", 0x4000);
    int64_t code_base = num_arg(p, a, "code_base", "--code-base", 0x200000);
    if (fill < 0 || fill > 255) {
        std::cerr << "error: --fill must be a byte (0..255), not " << fill << "\n";
        return 1;
    }
    Assembler as(a.all("include"), a.has("strict"), !a.has("no_mpp"), defines_of(a), a.get("optimize", "1") != "0");
    if (a.has("lcf")) {
        if (!places.empty()) p.fail("argument --lcf: not allowed with --place (the link file places the sections)");
        as.set_lcf(a.get("lcf"));
    }
    // -M: a make dependency file; without -o only the includes are read (scan only)
    const bool deps = a.has("depfile"), scan = deps && !a.has("output");
    std::string target;
    if (deps) {
        for (auto& t : a.all("dep_target")) target += (target.empty() ? "" : " ") + t;
        if (target.empty() && a.has("output")) target = make_escape(a.get("output"));
        if (a.has("dep_phony") && target.empty()) p.fail("argument -MP: needs a target (-o or -MT)");
        as.track_deps(a.has("dep_missing"), scan);
    } else if (a.has("dep_missing") || a.has("dep_phony") || a.has("dep_target")) {
        p.fail("argument -MG/-MP/-MT: needs -M FILE");
    }
    for (auto& src : a.all("sources")) as.add_module(src);
    if (deps && (scan || as.missing)) {
        // Scan only, or (-MG) a file still to be generated is missing: write the dependencies and
        // stop (as another assembler -MG does). A scan reports its errors (bad include lines, preprocessor
        // errors); a full assembly with a missing file doesn't, as they may come from that file.
        if (scan && !as.d.errors.empty()) {
            for (size_t i = 0; i < as.d.errors.size() && i < 200; i++) std::cerr << as.d.errors[i] << "\n";
            std::cerr << as.d.errors.size() << " error(s); no dependency file written\n";
            return 1;
        }
        write_deps(a, as, target);
        return 0;
    }
    auto chunks = as.assemble(!a.has("no_link"), places, data_base, code_base);
    std::optional<Image> img;
    if (as.d.errors.empty() && !a.has("no_link") && a.has("output"))
        img = build_image(chunks, origin ? &*origin : nullptr, size ? &*size : nullptr, int(fill), &as.d);
    for (auto& w : as.d.warnings) std::cerr << w << "\n";
    for (size_t i = 0; i < as.d.errors.size() && i < 200; i++) std::cerr << as.d.errors[i] << "\n";
    if (!as.d.errors.empty()) {
        int64_t more = int64_t(as.d.errors.size()) - 200;
        std::cerr << as.d.errors.size() << " error(s)" << (more > 0 ? " (" + dec(more) + " not shown)" : "") << "; no output written\n";
        return 1;
    }
    if (img) {
        write_bytes(a.get("output"), img->data);
        if (a.get("depfile") != "-")   // with -M - stdout holds only the dependencies
            std::cout << "wrote " << a.get("output") << ": 0x" << hexs(img->size) << " bytes at 0x" << hexs(img->origin) << "\n";
    }
    if (deps) write_deps(a, as, target);
    if (a.has("sym")) {
        auto syms = as.symbols();
        std::stable_sort(syms.begin(), syms.end(), [](auto& x, auto& y) {
            if (x.second != y.second) return x.second < y.second;
            return x.first < y.first;
        });
        std::string s;
        for (auto& kv : syms) s += hexs(kv.second & 0xFFFFFFFFLL, 6) + " " + kv.first + "\n";
        write_text(a.get("sym"), s);
    }
    return 0;
}

int cmd_mac(const Parser&, const Args& a) {
    std::vector<SrcLine> lines;
    try {
        lines = preprocess(a.get("source"), a.all("include"), defines_of(a));
    } catch (MppError& e) {
        if (!e.where.none) std::cerr << e.where.str() << ": error: " << e.what() << "\n";
        else std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::string text;
    for (auto& l : lines) text += l.first + "\n";
    out_text(a, text);
    return 0;
}

int cmd_dis(const Parser& p, const Args& a) {
    Rom rom(read_bytes(a.get("rom")));
    if (a.has("output")) {
        WriteStats st = disassemble_rom(rom, a.get("output"), a.get("executed"), !a.has("no_sweep"), a.get("hints"));
        std::cout << "wrote " << a.get("output") << "/main.asm: " << st.instructions << " instructions (" << st.db_fallback
                  << " as db), " << st.data_bytes << " data bytes, " << st.sections << " sections\n";
        return 0;
    }
    int64_t pc = a.has("start") ? num_arg(p, a, "start", "--start", 0) : (rom.data.size() > 0x20 ? rom.header().entry : CART_BASE);
    int64_t count = num_arg(p, a, "count", "--count", 64);
    MemFn mem = [&rom](int64_t x) { return rom(x); };
    for (int64_t i = 0; i < count; i++) {
        if (!rom.contains(pc)) break;
        Insn ins = decode(mem, pc);
        std::string t = ins.text();
        size_t tab = t.find('\t');
        if (tab != std::string::npos) t[tab] = ' ';
        std::string raw;
        for (size_t k = 0; k < ins.raw.size(); k++) raw += (k ? " " : "") + hexl(ins.raw[k], 2);
        while (raw.size() < 24) raw += ' ';
        std::cout << hexs(pc, 6) << "  " << raw << " " << t << (ins.illegal.empty() ? "" : "  ; " + ins.illegal) << "\n";
        pc += ins.size;
    }
    return 0;
}

std::string make_temp_dir() {
    std::random_device rd;
    std::mt19937_64 g(rd());
    for (int i = 0; i < 100; i++) {
        fs::path p = fs::temp_directory_path() / ("unitron_" + hexl(int64_t(g() & 0xFFFFFFFFFFFFLL)));
        std::error_code ec;
        if (fs::create_directory(p, ec)) return p.u8string();
    }
    throw std::runtime_error("can't create a temporary directory");
}

int cmd_roundtrip(const Parser&, const Args& a) {
    Bytes data = read_bytes(a.get("rom"));
    Rom rom(data);
    std::string work = a.get("workdir");
    bool tmp = work.empty();
    if (tmp) work = make_temp_dir();
    WriteStats st = disassemble_rom(rom, work, a.get("executed"), true, a.get("hints"));
    Assembler as;
    as.add_module(path_join(work, "main.asm"));
    auto chunks = as.assemble();
    Bytes img;
    bool have = false;
    if (as.d.errors.empty()) {
        int64_t origin = CART_BASE, size = int64_t(data.size());
        img = build_image(chunks, &origin, &size, 0xFF, nullptr).data;
        have = true;
    }
    if (tmp) {
        std::error_code ec;
        fs::remove_all(fs::u8path(work), ec);
    }
    bool ok = have && img == data;
    for (size_t i = 0; i < as.d.errors.size() && i < 20; i++) std::cerr << as.d.errors[i] << "\n";
    std::string sha = have && !img.empty() ? sha1_hex(img) : "-";
    std::cout << (ok ? "OK" : "DIFFERS") << ": " << st.instructions << " instructions (" << st.db_fallback << " as db), "
              << st.data_bytes << " data bytes; sha1 " << sha << "\n";
    return ok ? 0 : 1;
}

int cmd_lz(const Parser& p, const Args& a) {
    Bytes data = read_bytes(a.get("input"));
    Bytes out;
    if (a.get("mode") == "d") {
        if (!a.has("length")) {
            std::cerr << "error: lz d needs --length\n";
            return 1;
        }
        int64_t used = 0;
        out = lzss::decompress(data, num_arg(p, a, "length", "--length", 0), num_arg(p, a, "offset", "--offset", 0), &used);
        std::cerr << used << " input bytes -> " << out.size() << " bytes\n";
    } else {
        out = lzss::compress_okumura(data, !a.has("no_clamp"));
    }
    write_bytes(a.get("output"), out);
    return 0;
}

int cmd_bin2db(const Parser& p, const Args& a) {
    Bytes data = read_bytes(a.get("input"));
    out_text(a, to_db(data, a.get("label"), int_arg(p, a, "per_line", "--per-line", 16), a.get("input")));
    return 0;
}

int cmd_text(const Parser&, const Args& a) {
    std::string out;
    try {
        Charmap cm(a.get("charmap"));
        out = text_to_db(cm, read_text_utf8(a.get("input")), a.get("input"));
    } catch (CharmapError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    out_text(a, out);
    return 0;
}

int cmd_gfx(const Parser& p, const Args& a, const std::string& sub) {
    if (sub == "palette") {
        int64_t n = int_arg(p, a, "colors", "--colors", 4);
        if (n < 1 || n > 256) p.fail("argument --colors: must be 1 to 256, not " + dec(n));
        Bytes words = png_palette_words(read_bytes(a.get("input")), a.get("input"), n);
        write_bytes(a.get("output"), words);
        std::cout << "wrote " << a.get("output") << ": " << n << " colors\n";
        return 0;
    }
    if (sub == "frompng" && a.has("indices")) {
        if (a.has("count") || a.has("trim") || a.has("map"))
            p.fail("argument --indices: not allowed with --count, --trim or --map");
        uint32_t w = 0, h = 0;
        Bytes px = png_indices(read_bytes(a.get("input")), a.get("input"), w, h);
        write_bytes(a.get("output"), px);
        std::cout << "wrote " << a.get("output") << ": " << w << "x" << h << " pixels\n";
        return 0;
    }
    if (sub == "frompng" && a.has("trim") && a.has("map")) p.fail("argument --trim: works on a plain sheet only, not with --map");
    if (sub == "frompng" && a.has("map") && !a.has("count")) p.fail("argument --map: needs --count");
    std::optional<TileMap> tm;
    if (a.has("map"))
        tm = read_map(read_bytes(a.get("map")), a.get("map_format", "wh8"),
                      a.has("map_width") ? int_arg(p, a, "map_width", "--map-width", 0) : 0);
    int64_t base = num_arg(p, a, "base", "--base", 0);
    if (sub == "topng") {
        std::vector<Palette> pals;
        if (a.has("palette")) pals = read_palettes(read_bytes(a.get("palette")));
        Bytes data = read_bytes(a.get("input"));
        size_t n = to_png(data, a.get("output"), int_arg(p, a, "width", "--width", 16), pals, tm, base, int_arg(p, a, "pal", "--pal", 0));
        std::cout << "wrote " << a.get("output") << ": " << n << " tiles\n";
    } else {
        // --count wins over --trim; without either, every cell
        int64_t count = a.has("count") ? int_arg(p, a, "count", "--count", 0)
                                       : from_png_count(read_bytes(a.get("input")), a.get("input"), a.has("trim"));
        Bytes data = from_png(a.get("input"), count, tm, base);
        write_bytes(a.get("output"), data);
        std::cout << "wrote " << a.get("output") << ": " << count << " tiles\n";
    }
    return 0;
}

int cmd_lift(const Parser&, const Args& a) {
#ifndef HAVE_LIFT
    (void)a;
    std::cerr << "error: lift not built\n";
    return 1;
#else
    LiftStats st = lift_run(a.get("rom"), a.get("output"), a.get("executed"), a.get("symbols"), a.get("hints"));
    std::cout << "functions=" << st.functions << " instructions=" << st.instructions << " unimplemented=" << st.unimplemented << "\n";
    return 0;
#endif
}

// ---------------------------------------------------------------- z80 (the sound CPU; docs/z80.md)
int cmd_z80_dis(const Args& a) {
    Bytes img = read_bytes(a.get("file"));
    z80::DisOptions opt{a.get("executed"), a.get("hints")};
    z80::DisStats st;
    if (!a.has("output")) {
        std::cout << z80::listing(img, opt, &st);
        return 0;
    }
    std::string text = z80::source(img, opt, path_name(a.get("file")), &st);
    make_dirs(a.get("output"));
    write_text(path_join(a.get("output"), "main.z80"), text);
    std::cout << "wrote " << path_join(a.get("output"), "main.z80") << ": " << st.instructions << " instructions (" << st.db_fallback
              << " bytes as DEFB), " << st.data_bytes << " data bytes\n";
    return 0;
}

bool z80_report(const z80::Assembler& as, int limit) {
    for (size_t i = 0; i < as.d.errors.size() && int(i) < limit; i++) std::cerr << as.d.errors[i] << "\n";
    if (as.d.errors.empty()) return true;
    int64_t more = int64_t(as.d.errors.size()) - limit;
    std::cerr << as.d.errors.size() << " error(s)" << (more > 0 ? " (" + dec(more) + " not shown)" : "") << "; no output written\n";
    return false;
}

// --equ: the labels as asm900 `equ` lines at their main-CPU address (Z80 address + base; the main
// CPU sees Z80 RAM at 0x7000, SNK SysLib l.39), sorted by address. EQU and DEFL names are left out:
// they are not addresses in the image.
std::string z80_equ_text(const z80::Assembler& as, int64_t base, const std::string& source, std::string& err) {
    auto labels = as.labels;
    std::stable_sort(labels.begin(), labels.end(), [](auto& x, auto& y) {
        if (x.second != y.second) return x.second < y.second;
        return x.first < y.first;
    });
    std::string out = "; generated by unitron z80 asm from " + source + ": edit the source, not this file\n" +
                      "; Z80 labels at their main-CPU address (Z80 address + 0x" + hexs(base, 4) + ")\n";
    for (auto& kv : labels) {
        // asm900 identifiers: letters, digits, '_', '.', '\'' (docs/language.md); a Z80 label may also have '?'
        if (kv.first.find('?') != std::string::npos) {
            err = "label '" + kv.first + "' is not an asm900 name ('?'); rename it for --equ";
            return "";
        }
        if (is_reserved(kv.first)) {
            err = "label '" + kv.first + "' is an asm900 reserved word; rename it for --equ";
            return "";
        }
        out += kv.first + "\tequ\t0x" + hexs(kv.second + base, 4) + "\n";
    }
    return out;
}

int cmd_z80_asm(const Parser& p, const Args& a) {
    int64_t base = num_arg(p, a, "equ_base", "--equ-base", 0x7000);
    z80::Assembler as(a.all("include"));
    if (!as.assemble(a.get("source")) && !z80_report(as, 200)) return 1;
    std::string equ;
    if (a.has("equ")) {
        std::string err;
        equ = z80_equ_text(as, base, path_name(a.get("source")), err);
        if (!err.empty()) {
            std::cerr << "error: " << err << "; no output written\n";
            return 1;
        }
    }
    if (a.has("output")) {
        write_bytes(a.get("output"), as.image);
        std::cout << "wrote " << a.get("output") << ": 0x" << hexs(int64_t(as.image.size())) << " bytes at Z80 0x0000\n";
    }
    if (a.has("sym")) {
        auto syms = as.symbols;
        std::stable_sort(syms.begin(), syms.end(), [](auto& x, auto& y) {
            if (x.second != y.second) return x.second < y.second;
            return x.first < y.first;
        });
        std::string s;
        for (auto& kv : syms) s += hexs(kv.second & 0xFFFF, 4) + " " + kv.first + "\n";
        write_text(a.get("sym"), s);
    }
    if (a.has("equ")) write_text(a.get("equ"), equ);
    if (a.has("db")) {
        // the image as asm900 db lines, 16 per line, the same lines as `unitron bin2db` writes
        std::string db = to_db(as.image, "", 16, "");
        write_text(a.get("db"), "; generated by unitron z80 asm from " + path_name(a.get("source")) + ": edit the source, not this file\n" + db);
    }
    return 0;
}

int cmd_z80_roundtrip(const Args& a) {
    Bytes img = read_bytes(a.get("file"));
    std::string work = a.get("workdir");
    bool tmp = work.empty();
    if (tmp) work = make_temp_dir();
    else make_dirs(work);
    z80::DisStats st;
    std::string src = path_join(work, "main.z80");
    write_text(src, z80::source(img, {a.get("executed"), a.get("hints")}, path_name(a.get("file")), &st));
    z80::Assembler as;
    bool have = as.assemble(src);
    if (tmp) {
        std::error_code ec;
        fs::remove_all(fs::u8path(work), ec);
    }
    for (size_t i = 0; i < as.d.errors.size() && i < 20; i++) std::cerr << as.d.errors[i] << "\n";
    bool ok = have && as.image == img;
    std::cout << (ok ? "OK" : "DIFFERS") << ": " << st.instructions << " instructions (" << st.db_fallback << " bytes as DEFB), "
              << st.data_bytes << " data bytes; sha1 " << (have && !as.image.empty() ? sha1_hex(as.image) : "-") << "\n";
    return ok ? 0 : 1;
}

Opt O(std::vector<std::string> flags, std::string dest, std::string help = "", bool takes = true, bool repeat = false,
      std::string metavar = "") {
    Opt o;
    o.flags = std::move(flags);
    o.dest = std::move(dest);
    o.help = std::move(help);
    o.takes = takes;
    o.repeat = repeat;
    o.metavar = std::move(metavar);
    return o;
}
Pos P(std::string dest, char nargs = 1, std::string help = "") {
    Pos p;
    p.dest = std::move(dest);
    p.nargs = nargs;
    p.help = std::move(help);
    return p;
}

const char* TOP =
    "usage: unitron [-h] {asm,mac,preprocess,dis,roundtrip,lz,bin2db,text,gfx,lift,z80} ...\n\n"
    "NGPC (TLCS-900/H) toolchain\n\n"
    "positional arguments:\n"
    "  {asm,mac,preprocess,dis,roundtrip,lz,bin2db,text,gfx,lift,z80}\n"
    "    asm                 assemble (and link) asm900 sources into a raw image\n"
    "    mac (preprocess)    run the macro preprocessor only\n"
    "    dis                 disassemble: a listing, or (-o DIR) rebuildable asm900 source\n"
    "    roundtrip           dis -> asm a ROM and compare the bytes\n"
    "    lz                  LZSS (Okumura LZSS.C parameters N=1024 F=18)\n"
    "    bin2db              binary file -> labeled asm900 db lines\n"
    "    text                text file + character map -> asm900 db lines\n"
    "    gfx                 2bpp tiles <-> indexed PNG (plain sheet or map layout); PNG palette -> words\n"
    "    lift                lift traced code to C (experimental)\n"
    "    z80                 the Z80 sound CPU: dis, asm, roundtrip (docs/z80.md)\n\n"
    "options:\n  -h, --help            show this help message and exit\n";

int run(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::cerr << "usage: unitron [-h] {asm,mac,preprocess,dis,roundtrip,lz,bin2db,text,gfx,lift,z80} ...\n"
                     "unitron: error: the following arguments are required: cmd\n";
        return 2;
    }
    std::string cmd = args[0];
    if (cmd == "-h" || cmd == "--help") {
        std::cout << TOP;
        return 0;
    }
    std::vector<std::string> rest(args.begin() + 1, args.end());
    Parser p;
    p.prog = "unitron " + cmd;
    std::string sub;
    if (cmd == "asm") {
        p.help = "assemble (and link) asm900 sources into a raw image";
        p.pos = {P("sources", '+', "one module per file")};
        p.opts = {O({"-o", "--output"}, "output"),
                  O({"-I", "--include"}, "include", "", true, true),
                  O({"-D", "--define"}, "define", "", true, true),
                  O({"--origin"}, "origin", "image start address (default: lowest code/romdata byte)"),
                  O({"--size"}, "size", "image size (default: up to the highest byte)"),
                  O({"--fill"}, "fill", "byte for gaps (default 0xFF)"),
                  O({"--place"}, "place", "", true, true, "SECTION=ADDR"),
                  O({"--lcf"}, "lcf", "place relocatable sections with a Toshiba link command file (docs/lcf.md)", true, false, "FILE"),
                  O({"--data-base"}, "data_base", "first address for data sections"),
                  O({"--code-base"}, "code_base", "first address for code/romdata"),
                  O({"--no-link"}, "no_link", "check one module; externs stay unresolved", false),
                  O({"--no-mpp"}, "no_mpp", "skip the macro preprocessor", false),
                  O({"--strict"}, "strict", "enforce asm900's include limits", false),
                  O({"--sym"}, "sym", "write a symbol file (address name)"),
                  O({"-H"}, "intel", "also accept suffixed numbers (1234H, 101B, 17O/17Q, 99D), as asm900 -H", false),
                  O({"-O"}, "optimize", "j/cal: -O1 (default) shortest jr/jrl/jp and calr/call; -O0 always jp/call (asm900 -O)",
                    true, false, "LEVEL"),
                  O({"-M"}, "depfile",
                    "write a make dependency file (- = stdout) listing every file read; without -o only the includes are read",
                    true, false, "FILE"),
                  O({"-MG"}, "dep_missing", "with -M: a missing include file is listed (it will be generated), not an error", false),
                  O({"-MP"}, "dep_phony", "with -M: an empty rule for each include file, so make doesn't fail when one is deleted",
                    false),
                  O({"-MT"}, "dep_target", "with -M: the rule's target (default: the -o file); may be repeated", true, true,
                    "TARGET")};
    } else if (cmd == "mac" || cmd == "preprocess") {
        p.help = "run the macro preprocessor only";
        p.pos = {P("source")};
        p.opts = {O({"-o", "--output"}, "output"), O({"-I", "--include"}, "include", "", true, true),
                  O({"-D", "--define"}, "define", "", true, true)};
    } else if (cmd == "dis") {
        p.help = "disassemble: a listing, or (-o DIR) rebuildable asm900 source";
        p.pos = {P("rom")};
        p.opts = {O({"-o", "--output"}, "output", "write DIR/main.asm that assembles back to the ROM"),
                  O({"--start"}, "start"),
                  O({"--count"}, "count"),
                  O({"--executed"}, "executed", "file of hex code addresses (extra trace entry points)"),
                  O({"--no-sweep"}, "no_sweep", "static trace only (no gap sweep)", false),
                  O({"--hints"}, "hints", "code/data hints for the gap sweep (see docs/language.md)")};
    } else if (cmd == "roundtrip") {
        p.help = "dis -> asm a ROM and compare the bytes";
        p.pos = {P("rom")};
        p.opts = {O({"--workdir"}, "workdir", "keep the source here (default: a temporary directory)"), O({"--executed"}, "executed"),
                  O({"--hints"}, "hints", "code/data hints for the gap sweep")};
    } else if (cmd == "lz") {
        p.help = "LZSS (Okumura LZSS.C parameters N=1024 F=18)";
        Pos m = P("mode", 1, "c = compress, d = decompress");
        m.choices = {"c", "d"};
        p.pos = {m, P("input"), P("output")};
        p.opts = {O({"--length"}, "length", "output length (decompress)"),
                  O({"--offset"}, "offset", "stream start in input (decompress)"),
                  O({"--no-clamp"}, "no_clamp", "compress without cutting the last match", false)};
    } else if (cmd == "bin2db") {
        p.help = "binary file -> labeled asm900 db lines";
        p.pos = {P("input"), P("label", '?')};
        p.opts = {O({"-o", "--output"}, "output"), O({"--per-line"}, "per_line")};
    } else if (cmd == "text") {
        p.help = "text file + character map -> asm900 db lines";
        p.pos = {P("charmap"), P("input")};
        p.opts = {O({"-o", "--output"}, "output")};
    } else if (cmd == "gfx") {
        if (rest.empty() || (rest[0] != "topng" && rest[0] != "frompng" && rest[0] != "palette")) {
            if (!rest.empty() && (rest[0] == "-h" || rest[0] == "--help")) {
                std::cout << "usage: unitron gfx [-h] {topng,frompng,palette} ...\n\n"
                          << "    topng               2bpp tiles -> indexed PNG\n"
                          << "    frompng             indexed PNG -> 2bpp tiles, or one byte per pixel (--indices)\n"
                          << "    palette             the PNG's palette -> 0x0BGR words\n";
                return 0;
            }
            std::cerr << "usage: unitron gfx [-h] {topng,frompng,palette} ...\n";
            if (rest.empty()) std::cerr << "unitron gfx: error: the following arguments are required: gcmd\n";
            else std::cerr << "unitron gfx: error: argument gcmd: invalid choice: '" << rest[0] << "' (choose from topng, frompng, palette)\n";
            return 2;
        }
        sub = rest[0];
        rest.erase(rest.begin());
        p.prog = "unitron gfx " + sub;
        p.pos = {P("input"), P("output")};
        if (sub == "palette") {
            p.help = "write the indexed PNG's first palette colors as little-endian 0x0BGR words (4 bits per channel, nearest)";
            p.opts = {O({"--colors"}, "colors", "how many colors (default 4)", true, false, "N")};
        }
        Opt mf = O({"--map-format"}, "map_format", "wh8: width byte, height byte, entries (default); raw: entries only");
        mf.choices = {"wh8", "raw"};
        if (sub != "palette")
            p.opts = {O({"--map"}, "map", "tile map file: lay the sheet out as the map shows it"), mf, O({"--map-width"}, "map_width"),
                      O({"--base"}, "base", "sheet tile n is character code base+n+1 in the map")};
        if (sub == "topng") {
            p.help = "write 2bpp tiles as an indexed PNG: a plain sheet, or laid out by a tile map";
            p.opts.push_back(O({"--width"}, "width", "tiles per row (plain sheet)"));
            p.opts.push_back(O({"--palette"}, "palette", "raw u16 0x0BGR entries, 4 per palette"));
            p.opts.push_back(O({"--pal"}, "pal", "palette for a plain sheet"));
        } else if (sub == "frompng") {
            p.help = "read an indexed PNG back into 2bpp tiles (pixel = palette index & 3), or its raw pixels (--indices)";
            p.opts.push_back(O({"--count"}, "count", "how many tiles (default: every cell of the sheet, row by row)"));
            p.opts.push_back(O({"--trim"}, "trim",
                               "drop blank cells (every pixel palette index 0) at the end of the last row, keeping its first cell",
                               false));
            p.opts.push_back(O({"--indices"}, "indices", "write each pixel's palette index, one byte per pixel, row by row (no tiles)",
                               false));
        }
    } else if (cmd == "z80") {
        const char* U = "usage: unitron z80 [-h] {dis,asm,roundtrip} ...\n";
        if (rest.empty() || (rest[0] != "dis" && rest[0] != "asm" && rest[0] != "roundtrip")) {
            if (!rest.empty() && (rest[0] == "-h" || rest[0] == "--help")) {
                std::cout << U << "\nthe NGPC's Z80 sound CPU, in Zilog syntax (docs/z80.md)\n\n"
                          << "    dis                 a listing, or (-o DIR) source that assembles back to the same bytes\n"
                          << "    asm                 assemble Zilog Z80 source into a raw image at Z80 0x0000\n"
                          << "    roundtrip           dis -> asm a Z80 image and compare the bytes\n";
                return 0;
            }
            std::cerr << U;
            if (rest.empty()) std::cerr << "unitron z80: error: the following arguments are required: zcmd\n";
            else std::cerr << "unitron z80: error: argument zcmd: invalid choice: '" << rest[0] << "' (choose from dis, asm, roundtrip)\n";
            return 2;
        }
        sub = rest[0];
        rest.erase(rest.begin());
        p.prog = "unitron z80 " + sub;
        if (sub == "dis") {
            p.help = "Z80 image (Z80 0x0000 up) -> a listing, or (-o DIR) DIR/main.z80 that assembles back to it";
            p.pos = {P("file")};
            p.opts = {O({"-o", "--output"}, "output", "write DIR/main.z80"),
                      O({"--executed"}, "executed", "file of hex code addresses (extra trace entry points)"),
                      O({"--hints"}, "hints", "code/data hints (see docs/z80.md)")};
        } else if (sub == "asm") {
            p.help = "assemble Zilog Z80 source into a raw image that starts at Z80 0x0000";
            p.pos = {P("source")};
            p.opts = {O({"-o", "--output"}, "output"), O({"-I", "--include"}, "include", "", true, true),
                      O({"--sym"}, "sym", "write a symbol file (address name)"),
                      O({"--equ"}, "equ", "write the labels as asm900 equ lines at their main-CPU address"),
                      O({"--equ-base"}, "equ_base", "main-CPU address of Z80 0x0000 (default 0x7000)"),
                      O({"--db"}, "db", "write the image as asm900 db lines (as bin2db does)")};
        } else {
            p.help = "dis -> asm a Z80 image and compare the bytes";
            p.pos = {P("file")};
            p.opts = {O({"--workdir"}, "workdir", "keep the source here (default: a temporary directory)"),
                      O({"--executed"}, "executed"), O({"--hints"}, "hints")};
        }
    } else if (cmd == "lift") {
        p.help = "lift traced code to C (experimental)";
        p.pos = {P("rom")};
        p.opts = {O({"-o", "--output"}, "output"), O({"--executed"}, "executed", "file of hex code addresses (extra trace entry points)"),
                  O({"--symbols"}, "symbols", "ADDRESS NAME lines: function names"),
                  O({"--hints"}, "hints", "code/data hints for the gap sweep")};
    } else {
        std::cerr << "usage: unitron [-h] {asm,mac,preprocess,dis,roundtrip,lz,bin2db,text,gfx,lift,z80} ...\n"
                     "unitron: error: argument cmd: invalid choice: '"
                  << cmd << "' (choose from asm, mac, preprocess, dis, roundtrip, lz, bin2db, text, gfx, lift, z80)\n";
        return 2;
    }
    if (cmd == "asm")
        for (auto& r : rest)
            if (r == "-O") r = "-O1";
    try {
        Args a = p.parse(rest);
        if (cmd == "asm" && a.has("optimize") && a.get("optimize") != "0" && a.get("optimize") != "1")
            p.fail("argument -O: invalid level '" + a.get("optimize") + "' (0 or 1)");
        if (cmd == "lift" && !a.has("output")) p.fail("the following arguments are required: -o/--output");
        if (cmd == "asm") return cmd_asm(p, a);
        if (cmd == "mac" || cmd == "preprocess") return cmd_mac(p, a);
        if (cmd == "dis") return cmd_dis(p, a);
        if (cmd == "roundtrip") return cmd_roundtrip(p, a);
        if (cmd == "lz") return cmd_lz(p, a);
        if (cmd == "bin2db") return cmd_bin2db(p, a);
        if (cmd == "text") return cmd_text(p, a);
        if (cmd == "gfx") return cmd_gfx(p, a, sub);
        if (cmd == "lift") return cmd_lift(p, a);
        if (cmd == "z80" && sub == "dis") return cmd_z80_dis(a);
        if (cmd == "z80" && sub == "asm") return cmd_z80_asm(p, a);
        if (cmd == "z80") return cmd_z80_roundtrip(a);
    } catch (UsageError& e) {
        std::cerr << e.msg;
        return 2;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Same bytes on every OS: no "\n" -> "\r\n" translation on stdout/stderr.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    std::ios::sync_with_stdio(false);
    try {
        int r = run(argc, argv);
        std::cout.flush();
        return r;
    } catch (std::exception& e) {
        std::cout.flush();
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
