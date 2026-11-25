// Z80 opcode space: every first byte, and every second byte after CB, ED, DD and FD, and every
// fourth byte after DD CB d and FD CB d, each with several operand values. Each input is decoded;
// a documented instruction must assemble back from its text to the same bytes; the rest must stay
// undecoded (written as DEFB by z80 dis). The decoded text of every input is frozen in
// tests/vectors/z80opspace.tsv, so any change to the table shows up as a diff.
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "util.hpp"
#include "z80.hpp"
#include "z80asm.hpp"

using namespace unitron;

int test_z80opspace(const std::string& dir, bool write) {
    using namespace unitron::z80;
    // operand byte samples: zero, small, the signed edges, all ones
    const std::vector<std::vector<uint8_t>> samples = {{0x00, 0x00, 0x00}, {0x05, 0x34, 0x12}, {0x7F, 0x80, 0xFF}, {0x80, 0xFF, 0x7F}};
    std::vector<Bytes> inputs;
    for (auto& s : samples) {
        for (int b0 = 0; b0 < 256; b0++) inputs.push_back({uint8_t(b0), s[0], s[1], s[2]});
        for (int pre : {0xCB, 0xED, 0xDD, 0xFD})
            for (int b1 = 0; b1 < 256; b1++) inputs.push_back({uint8_t(pre), uint8_t(b1), s[0], s[1], s[2]});
        for (int pre : {0xDD, 0xFD})
            for (int b3 = 0; b3 < 256; b3++) inputs.push_back({uint8_t(pre), 0xCB, s[0], uint8_t(b3), s[1]});
    }
    const int64_t pc = 0x0100;
    std::string table;
    int bad = 0, documented = 0, alt = 0, undocumented = 0;
    for (auto& in : inputs) {
        Insn ins = decode_bytes(in, pc);
        std::string hex;
        for (int k = 0; k < (ins.form < 0 ? 1 : ins.size); k++) hex += hexs(in[size_t(k)], 2);
        if (ins.form < 0) {
            undocumented++;
            table += hex + "\t-\n";
            continue;
        }
        std::string t = text(ins);
        size_t tab = t.find('\t');
        std::string line = tab == std::string::npos ? t : t.substr(0, tab) + " " + t.substr(tab + 1);
        table += hex + "\t" + line + (ins.f()->alt ? "\talt" : "") + "\n";
        Bytes got;
        std::string err;
        try {
            got = assemble_line(line, pc);
        } catch (std::exception& e) {
            err = e.what();
        }
        if (ins.f()->alt) {
            alt++;
            // the assembler picks the other (shorter) encoding of the same instruction
            if (!err.empty() || got == ins.raw || got.size() >= ins.raw.size()) {
                std::cerr << hex << " " << line << ": alt form assembles to " << (err.empty() ? "the same bytes" : err) << "\n";
                bad++;
            }
            continue;
        }
        documented++;
        if (got != ins.raw) {
            std::string g;
            for (auto b : got) g += hexs(b, 2);
            std::cerr << hex << " " << line << ": assembles to " << (err.empty() ? g : err) << "\n";
            bad++;
        }
    }
    // distinct documented forms in the table (the 1978 manual counts 694 opcode/operand combinations)
    int forms_n = 0;
    for (auto& f : forms()) forms_n += f.alt ? 0 : 1;
    std::string head = "# z80opspace: " + std::to_string(inputs.size()) + " inputs, " + std::to_string(documented) + " documented, " +
                       std::to_string(alt) + " second encodings, " + std::to_string(undocumented) + " undocumented; " +
                       std::to_string(forms_n) + " instruction forms\n";
    std::string all = head + table;
    std::string path = dir + "/z80opspace.tsv";
    std::cout << head.substr(2);
    if (write) {
        write_text(path, all);
        std::cout << "rewrote " << path << "\n";
        return bad ? 1 : 0;
    }
    Bytes want;
    if (!read_bytes_opt(path, want)) {
        std::cerr << "missing " << path << " (run with --write)\n";
        return 1;
    }
    std::string w(want.begin(), want.end());
    if (w != all) {
        auto wl = split_on(w, "\n"), gl = split_on(all, "\n");
        int shown = 0;
        for (size_t i = 0; i < std::max(wl.size(), gl.size()) && shown < 20; i++) {
            std::string a = i < wl.size() ? wl[i] : "(none)", b = i < gl.size() ? gl[i] : "(none)";
            if (a != b) {
                std::cerr << "line " << i + 1 << ": want " << a << "\n         got  " << b << "\n";
                shown++;
            }
        }
        bad++;
    }
    std::cout << "z80opspace: " << bad << " problem(s)\n";
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------- z80shift
// z80 dis -o writes labels (not numbers) for the end of the image and the addresses just past it
// (END_LABEL+k), for operands the code rewrites (label+k) and for every other address it knows is
// in the image. z80shift proves that these labels really carry the addresses: it puts one NOP in
// the written source in front of the first instruction whose operand is rewritten (or, if there is
// none, in front of the end label), assembles it, and checks the result byte for byte against the
// original with every such reference at or after the NOP moved by one and nothing else changed:
// - jump, call and memory operands into the image at or after the NOP: +1;
// - memory operands and 16-bit immediates at the end of the image up to 15 bytes past it: +1;
// - a data word that holds the image size: +1;
// - everything else (immediates inside the image, hardware addresses, RST): unchanged.
// Used by z80shift (a program of our own, below) and by z80rom (the drivers, when present).
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

#include "z80dis.hpp"

std::string z80_shift_check(const Bytes& img, const std::string& name, const std::string& workdir, std::string& info) {
    using namespace unitron::z80;
    namespace fs = std::filesystem;
    const int64_t size = int64_t(img.size());
    std::string src = source(img, {}, name);
    std::string p0 = (fs::u8path(workdir) / "orig.z80").u8string(), p1 = (fs::u8path(workdir) / "shifted.z80").u8string();
    write_text(p0, src);
    Assembler a0;
    if (!a0.assemble(p0) || a0.image != img) return "the source does not assemble back to the same bytes";
    std::map<std::string, int64_t> sym(a0.symbols.begin(), a0.symbols.end());
    // the insertion point: the lowest instruction label written with +k (a rewritten operand)
    auto lines = split_on(src, "\n");
    int64_t P = -1;
    std::string plabel;
    for (auto& l : lines) {
        if (l.empty() || l[0] != '\t') continue;
        for (size_t i = 0; i + 2 < l.size(); i++) {
            if (l[i] != '+' || !py_digit(l[i + 1])) continue;
            size_t b = i;
            while (b > 0 && (py_alnum((unsigned char)l[b - 1]) || l[b - 1] == '_')) b--;
            std::string id = l.substr(b, i - b);
            auto it = sym.find(id);
            if (it == sym.end() || id == "DriverEnd") continue;
            if (P < 0 || it->second < P) P = it->second, plabel = id;
        }
    }
    if (P < 0) {
        if (!sym.count("DriverEnd")) return "nothing to move (no rewritten operand, no end label)";
        P = size, plabel = "DriverEnd";
    }
    std::string shifted;
    bool put = false;
    for (auto& l : lines) {
        if (!put && l == plabel + ":") shifted += "\tNOP\n", put = true;
        shifted += l + "\n";
    }
    if (!put) return "no label line " + plabel;
    write_text(p1, shifted);
    Assembler a1;
    if (!a1.assemble(p1)) return "the shifted source does not assemble: " + (a1.d.errors.empty() ? "" : a1.d.errors[0]);
    // the expected image: the NOP at P, then every reference moved as the header comment says
    Bytes want(img.begin(), img.begin() + P);
    want.push_back(0x00);
    want.insert(want.end(), img.begin() + P, img.end());
    auto mv = [&](int64_t a) { return a + (a >= P ? 1 : 0); };
    auto in_img = [&](int64_t v) { return v >= 0 && v < size; };
    auto at_end = [&](int64_t v) { return v >= size && v < size + 16; };
    MemFn mem = [&](int64_t a) { return a >= 0 && a < size ? int(img[size_t(a)]) : -1; };
    std::set<int64_t> code_bytes;
    int moved = 0, words = 0;
    for (auto& l : split_on(listing(img, {}), "\n")) {
        if (l.size() < 21 || !std::isxdigit((unsigned char)l[0]) || l[4] != ' ') continue;
        std::string t = l.substr(21);
        if (starts_with(t, "DEF") || starts_with(t, ";")) continue;
        int64_t a = std::stoll(l.substr(0, 4), nullptr, 16);
        Insn ins = decode(mem, a);
        const Form* f = ins.f();
        if (!f) return "listing line " + l + " does not decode";
        for (int k = 0; k < ins.size; k++) code_bytes.insert(a + k);
        int64_t nn = ins.nn, target = ins.target;
        bool has_nn = std::count(f->tmpl.begin(), f->tmpl.end(), T_NN) > 0;
        if (ins.has_target() && f->flow != Flow::Rst) {
            if (in_img(target)) target = mv(target);
            if (has_nn) nn = target;
        } else if (has_nn) {
            bool memop = false;
            for (auto& p : f->ops) memop |= p.k == P::Mem;
            if (memop && in_img(nn)) nn = mv(nn);
            else if (at_end(nn)) nn += 1;
        }
        if (nn != ins.nn || target != ins.target) moved++;
        Bytes b = encode(*f, ins.n, nn, ins.d, target, mv(a));
        std::copy(b.begin(), b.end(), want.begin() + mv(a));
    }
    for (int64_t p = 0; p + 1 < size; p++) {
        if (code_bytes.count(p) || code_bytes.count(p + 1)) continue;
        if ((int64_t(img[size_t(p)]) | int64_t(img[size_t(p + 1)]) << 8) != size) continue;
        want[size_t(mv(p))] = uint8_t((size + 1) & 0xFF);
        want[size_t(mv(p) + 1)] = uint8_t((size + 1) >> 8);
        words++;
        p++;
    }
    info = "NOP at " + hexz(P, 4) + " (before " + plabel + "), " + std::to_string(moved) + " operands and " + std::to_string(words) +
           " data words moved";
    if (a1.image.size() != want.size()) return "shifted image has " + std::to_string(a1.image.size()) + " bytes, want " + std::to_string(want.size());
    for (size_t i = 0; i < want.size(); i++)
        if (a1.image[i] != want[i])
            return "byte " + hexz(int64_t(i), 4) + " of the shifted image is " + hexz(a1.image[i], 2) + ", want " + hexz(want[i], 2) + " (" + info + ")";
    return "";
}

// A program of our own with every kind of reference z80shift checks.
int test_z80shift() {
    namespace fs = std::filesystem;
    const char* prog =
        "\tORG\t0000H\n"
        "Reset:\tDI\n"
        "\tLD\tSP,0080H\n"
        "\tJP\tInit\n"
        "\tORG\t0038H\n"
        "Int38:\tEI\n"
        "\tRETI\n"
        "\tORG\t0066H\n"
        "Nmi:\tRETN\n"
        "Id:\tDEFB\t4BH\n"
        "\tDEFW\tTop\t\t; a data word that points at the end of the image\n"
        "Init:\tLD\tHL,(Top+2)\t; data right after the image\n"
        "\tLD\tDE,Top+6\n"
        "\tLD\tBC,0010H\t; a count: stays a number\n"
        "\tLD\tA,(Var)\t; before the NOP: does not move\n"
        "\tLD\t(Self+1),A\t; rewrites the operand of Self\n"
        "\tLD\t(4001H),A\t; hardware: stays a number\n"
        "\tCALL\tSelf\n"
        "\tJR\tInit\n"
        "Var:\tDEFB\t0\n"
        "Self:\tLD\tA,00H\n"
        "\tLD\t(Self2+1),HL\n"
        "\tCALL\tSub\n"
        "\tJR\tNZ,Self\n"
        "\tRST\t38H\n"
        "\tRET\n"
        "Self2:\tLD\tDE,0000H\n"
        "\tJP\t(HL)\n"
        "Sub:\tLD\tA,(Table+1)\n"
        "\tLD\tIX,(Top)\n"
        "\tRET\n"
        "Table:\tDEFB\t1\n"
        "\tDEFB\t2\n"
        "Top\tEQU\t$\n"
        "\tEND\n";
    fs::path d = fs::temp_directory_path() / "unitron_test_z80shift";
    fs::remove_all(d);
    fs::create_directories(d);
    write_text((d / "prog.z80").u8string(), prog);
    z80::Assembler as;
    int bad = 0;
    if (!as.assemble((d / "prog.z80").u8string())) {
        for (auto& e : as.d.errors) std::cerr << e << "\n";
        bad++;
    } else {
        std::string info, err = z80_shift_check(as.image, "prog.bin", d.u8string(), info);
        std::cout << "z80shift prog: " << (err.empty() ? info : err) << "\n";
        // 9 operands move: before the NOP, LD HL,(Top+2), LD DE,Top+6, LD (Self+1),A and CALL Self;
        // after it, LD (Self2+1),HL, CALL Sub, JR NZ,Self, LD A,(Table+1) and LD IX,(Top). And the
        // DEFW Top. JP Init, JR Init, LD A,(Var), LD BC,0010H, (4001H) and RST 38H stay.
        if (!err.empty()) bad++;
        else if (info.find(", 9 operands and 1 data words moved") == std::string::npos) {
            std::cerr << "z80shift prog: want 9 operands and 1 data word moved: " << info << "\n";
            bad++;
        }
    }
    fs::remove_all(d);
    std::cout << "z80shift: " << bad << " problem(s)\n";
    return bad ? 1 : 0;
}
