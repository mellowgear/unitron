// The hardware-name table data/hwnames.tsv (docs/hardware-names.md):
//   hwinc:   data/ngpc_hw.inc is generated from it and must be up to date;
//   hwnames: the table itself is well formed (unitron_tests hwnames DATA_DIR).
// hwnames reads the file, not the copy embedded in the binary, and checks:
//   - every record has its fields; KIND is io, sysram, vector, k2ge or mem; sizes and bit numbers fit;
//   - names are unique (registers and bits together), addresses are unique and sorted;
//   - every entry cites its source in the table's form: "IO900H.H l.N" for io entries (a line of
//     docs/toshiba/IO900H.H), "SNK DOC PLACE[; [DOC] PLACE]..." for the rest: DOC one of SNK's
//     documents, PLACE ", SECTION" (a heading of docs/snk/DOC/DOC.md) or " §X-Y" (a numbered
//     section of it); bits may add " (diagram)" or " (diagram text)"; never a line number;
//   - the naming rule (docs/hardware-names.md): uppercase letters, digits and _; io, sysram and k2ge names are the
//     official spelling normalized ("." -> "_"), a k2ge register named from a multi-word SNK name
//     has its words joined with a leading digit word moved to the end ("2D Status" -> STATUS_2D);
//     vectors are VEC_*; a bit is SNK's own name, or REGISTER_ + its official name (or the
//     official name's first word);
//   - bits point at named registers, within the register's width;
//   - no name is an asm900 reserved word;
//   - each vector is 4 bytes, 4-aligned, in 0x6FB8..0x6FFC, and the reserved 0x6FEC has none.
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "assembler.hpp"
#include "hwnames.hpp"
#include "util.hpp"

using namespace unitron;

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

namespace {

std::string normalize(const std::string& official) { return upper(replace_all(official, ".", "_")); }

// "2D Status" -> STATUS_2D, "Scroll Priority" -> SCROLL_PRIORITY
std::string from_words(const std::string& official) {
    auto w = split_ws(official);
    if (!w.empty() && py_digit(uint8_t(w[0][0]))) {
        w.push_back(w[0]);
        w.erase(w.begin());
    }
    for (auto& x : w) x = normalize(x);
    return join(w, "_");
}

const std::set<std::string> SNK_DOCS = {"SysPro", "SysWork", "SysLib", "K2GEres", "K2GETechRef", "K2GEpal"};

// a heading line's text without its markdown: "#### *4-1***.****MEMORY LAYOUT**" -> "4-1.MEMORY LAYOUT"
std::string heading_text(std::string h) {
    static const std::regex marks(R"(^#+\s*|<sup>.*?</sup>|[*\\])"), ws(R"(\s+)");
    h = std::regex_replace(h, marks, "");
    return strip(std::regex_replace(h, ws, " "));
}

// The section headings of SNK's documents (docs/snk/DOC/DOC.md, the copies in the repository).
struct SnkDocs {
    std::string dir;
    std::map<std::string, std::set<std::string>> heads;
    const std::set<std::string>& of(const std::string& doc) {
        auto it = heads.find(doc);
        if (it != heads.end()) return it->second;
        std::set<std::string>& h = heads[doc];
        Bytes raw;
        if (read_bytes_opt(dir + "/" + doc + "/" + doc + ".md", raw))
            for (auto& l : split_on(std::string(raw.begin(), raw.end()), "\n"))
                if (!l.empty() && l[0] == '#') h.insert(heading_text(strip(l)));
        return h;
    }
};

// A citation part's place in document DOC: ", SECTION" (a heading of the document, as it is
// written there without its markup) or " §X-Y" (a heading that starts "X-Y."); a part without a
// document is "§X-Y" in the document before it. Bits may end in " (diagram)" or " (diagram text)".
// A line number is not a place: the copies' line numbers are not SNK's.
std::string snk_cite_problem(SnkDocs& docs, const std::string& cite, bool bit) {
    static const std::regex line_no(R"(\bl\.[0-9])"), sec_no(R"(§([0-9]+(-[0-9]+)*))");
    if (std::regex_search(cite, line_no)) return "cites a line number; cite the document's section";
    if (!starts_with(cite, "SNK ")) return "does not start with 'SNK '";
    std::string body = cite.substr(4);
    if (bit)
        for (std::string tail : {" (diagram)", " (diagram text)"})
            if (body.size() > tail.size() && body.compare(body.size() - tail.size(), tail.size(), tail) == 0) {
                body.resize(body.size() - tail.size());
                break;
            }
    std::string doc;
    for (auto& part : split_on(body, "; ")) {
        std::string place = part;
        size_t sp = part.find_first_of(" ,");
        if (sp != std::string::npos && SNK_DOCS.count(part.substr(0, sp))) {
            doc = part.substr(0, sp);
            place = part.substr(sp);
            if (starts_with(place, ", ")) {
                if (!docs.of(doc).count(place.substr(2))) return "'" + place.substr(2) + "' is not a heading of " + doc;
                continue;
            }
            if (!starts_with(place, " ")) return "'" + part + "' has no place";
            place = place.substr(1);
        } else if (doc.empty()) {
            return "the first part names one of SNK's documents";
        }
        std::smatch m;
        if (!std::regex_match(place, m, sec_no)) return "'" + place + "' is not ', SECTION' or '§X-Y'";
        bool found = false;
        for (auto& h : docs.of(doc)) found |= starts_with(h, m[1].str() + ".");
        if (!found) return doc + " has no section " + m[1].str();
    }
    return "";
}

}  // namespace

int test_hwnames(const std::string& data_dir) {
    std::string path = data_dir + "/hwnames.tsv";
    Bytes raw = read_bytes(path);
    std::vector<std::string> bad;
    auto fail = [&](int line, const std::string& m) { bad.push_back("hwnames.tsv:" + dec(line) + ": " + m); };
    static const std::regex name_re("[A-Z][A-Z0-9_]*");
    static const std::regex io_cite(R"(IO900H\.H l\.[0-9]+)");
    SnkDocs snk{data_dir + "/../docs/snk", {}};
    const std::set<std::string> kinds = {"io", "sysram", "vector", "k2ge", "mem"};
    struct Reg {
        int64_t size;
        std::string name;
    };
    std::map<int64_t, Reg> regs;
    std::map<std::string, int> names;   // name -> line
    std::set<std::string> section_kinds, used_kinds;
    int64_t last = -1;
    int n_names = 0, n_bits = 0, line = 0;
    struct Bit {
        int line;
        int64_t reg, bit;
        std::string name, official;
    };
    std::vector<Bit> bits;
    auto check_name = [&](int ln, const std::string& name) {
        if (!std::regex_match(name, name_re)) fail(ln, name + ": not uppercase letters, digits and _");
        if (is_reserved(name)) fail(ln, name + ": an asm900 reserved word");
        auto it = names.find(name);
        if (it != names.end()) fail(ln, name + ": also defined at line " + dec(it->second));
        names[name] = ln;
    };
    for (auto& l : split_on(std::string(raw.begin(), raw.end()), "\n")) {
        line++;
        if (l.empty() || l[0] == '#') continue;
        auto f = split_on(l, "\t");
        if (f[0] == "section") {
            if (f.size() != 3) fail(line, "a section record has 3 fields");
            else section_kinds.insert(f[1]);
        } else if (f[0] == "name") {
            n_names++;
            if (f.size() != 7) {
                fail(line, "a name record has 7 fields");
                continue;
            }
            int64_t addr = py_int0(f[1]), size = py_int0(f[3]);
            const std::string &name = f[2], &kind = f[4], &cite = f[5], &official = f[6];
            check_name(line, name);
            if (addr <= last) fail(line, name + ": address " + f[1] + (addr == last ? " repeats the one before" : " is not in ascending order"));
            last = addr;
            regs[addr] = {size, name};
            if (!kinds.count(kind)) fail(line, name + ": unknown kind " + kind);
            used_kinds.insert(kind);
            if (size < 1) fail(line, name + ": size " + f[3]);
            if (strip(official).empty()) fail(line, name + ": no official spelling");
            if (kind == "io") {
                if (!std::regex_match(cite, io_cite)) fail(line, name + ": citation '" + cite + "' is not 'IO900H.H l.N'");
                if (addr >= 0x80 || (size != 1 && size != 2)) fail(line, name + ": an io register is 1 or 2 bytes below 0x80");
            } else {
                std::string why = snk_cite_problem(snk, cite, false);
                if (!why.empty()) fail(line, name + ": citation '" + cite + "': " + why);
            }
            if (kind == "io" || kind == "sysram" || (kind == "k2ge" && official.find(' ') == std::string::npos)) {
                if (name != normalize(official)) fail(line, name + ": the official '" + official + "' normalizes to " + normalize(official));
            } else if (kind == "k2ge" && name != from_words(official)) {
                fail(line, name + ": the SNK register name '" + official + "' gives " + from_words(official));
            }
            if (kind == "vector") {
                if (!starts_with(name, "VEC_")) fail(line, name + ": a vector's name starts with VEC_");
                if (size != 4) fail(line, name + ": a vector is 4 bytes, not " + f[3]);
                if (addr < 0x6FB8 || addr > 0x6FFC || addr % 4) fail(line, name + ": not a vector slot (4-aligned, 0x6FB8..0x6FFC)");
                if (addr == 0x6FEC) fail(line, name + ": 0x6FEC is reserved (SNK SysPro, SYSTEM INFORMATION)");
            }
        } else if (f[0] == "bit") {
            n_bits++;
            if (f.size() != 6) {
                fail(line, "a bit record has 6 fields");
                continue;
            }
            check_name(line, f[2]);
            std::string why = snk_cite_problem(snk, f[4], true);
            if (!why.empty()) fail(line, f[2] + ": citation '" + f[4] + "': " + why);
            if (strip(f[5]).empty()) fail(line, f[2] + ": no official spelling");
            bits.push_back({line, py_int0(f[1]), py_int0(f[3]), f[2], f[5]});
        } else {
            fail(line, "unknown record " + f[0]);
        }
    }
    for (auto& b : bits) {
        auto r = regs.find(b.reg);
        if (r == regs.end()) {
            fail(b.line, b.name + ": register 0x" + hexs(b.reg) + " has no name record");
            continue;
        }
        if (b.bit < 0 || b.bit >= 8 * r->second.size) fail(b.line, b.name + ": bit " + dec(b.bit) + " is outside " + r->second.name);
        std::string pre = r->second.name + "_", off = normalize(b.official), word = split_ws(b.official).empty() ? "" : normalize(split_ws(b.official)[0]);
        if (b.name != off && b.name != pre + off && b.name != pre + word)
            fail(b.line, b.name + ": a bit is SNK's name (" + off + ") or " + pre + "OFFICIAL (" + pre + off + " or " + pre + word + ")");
    }
    for (auto& k : used_kinds)
        if (!section_kinds.count(k)) fail(0, "kind " + k + " has no section record");
    // the embedded table is this file (cmake/embed.cmake)
    if (hw_tsv() != std::string(raw.begin(), raw.end())) fail(0, "the embedded copy differs from the file (rebuild)");
    if (size_t(n_names) != hw_names().size() || size_t(n_bits) != hw_bits().size()) fail(0, "the parsed tables have a different number of entries");
    for (auto& b : bad) std::cerr << b << "\n";
    std::cout << "hwnames: " << n_names << " names, " << n_bits << " bits, " << bad.size() << " problems\n";
    return bad.empty() ? 0 : 1;
}
