#include "writer.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "assembler.hpp"   // is_reserved
#include "hwnames.hpp"
#include "line.hpp"
#include "regs.hpp"

namespace unitron {

namespace {

const int DB_PER_LINE = 16;
const int64_t DFB_MIN = 16;
const int64_t K2GE_LO = 0x8000, K2GE_HI = 0xBFFF;   // SNK K2GETechRef, 4-2. MEMORY MAP   // a run of one byte this long or longer is written as dfb

bool is_hexnum(const std::string& s, size_t from, size_t to) {   // 0x[0-9A-F]+ | \d+ exactly on [from,to)
    if (to - from > 2 && s.compare(from, 2, "0x") == 0) {
        for (size_t i = from + 2; i < to; i++)
            if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F'))) return false;
        return true;
    }
    if (to == from) return false;
    for (size_t i = from; i < to; i++)
        if (!py_digit(s[i])) return false;
    return true;
}

// re.search(r"[+-](0x[0-9A-F]+|\d+)$", inner)
bool ends_with_disp(const std::string& s) {
    for (size_t k = 0; k < s.size(); k++)
        if ((s[k] == '+' || s[k] == '-') && is_hexnum(s, k + 1, s.size())) return true;
    return false;
}

// Ways to add an asm900 `:n` width to one or two operands.
std::vector<WidthVar> width_variants(const std::vector<Op>& ops) {
    std::vector<std::pair<size_t, int>> single;
    for (size_t i = 0; i < ops.size(); i++) {
        const std::string& o = ops[i].text;
        if (o.find(':') != std::string::npos) continue;
        if (!o.empty() && o.front() == '(' && o.back() == ')') {
            std::string inner = o.substr(1, o.size() - 2);
            if (ops[i].ref) {
                single.push_back({i, 16});
                single.push_back({i, 24});
            } else if (ends_with_disp(inner)) {
                single.push_back({i, 16});
            }
        } else if (is_hexnum(o, o.size() && o[0] == '-' ? 1 : 0, o.size()) || ops[i].ref) {
            std::vector<int> sizes;
            for (auto& x : ops) {
                auto it = REG().find(x.text);
                if (it != REG().end()) sizes.push_back(it->second.size * 8);
            }
            std::vector<int> order;
            if (!sizes.empty()) order.push_back(sizes[0]);
            for (int n : {8, 16, 24, 32})
                if (sizes.empty() || n != sizes[0]) order.push_back(n);
            for (int n : order) single.push_back({i, n});
        }
    }
    std::vector<WidthVar> out;
    for (auto& s : single) out.push_back({s});
    for (size_t a = 0; a < single.size(); a++)
        for (size_t b = a + 1; b < single.size(); b++)
            if (single[a].first != single[b].first) out.push_back({single[a], single[b]});
    return out;
}

std::vector<std::string> apply_width(std::vector<std::string> ops, const WidthVar& var) {
    for (auto& v : var) {
        std::string& o = ops[v.first];
        if (!o.empty() && o[0] == '(') o = o.substr(0, o.size() - 1) + ":" + dec(v.second) + ")";
        else o = o + ":" + dec(v.second);
    }
    return ops;
}

std::string db_line(const uint8_t* b, size_t n) {
    std::string s = "\tdb\t";
    for (size_t i = 0; i < n; i++) {
        if (i) s += ",";
        s += "0x" + hexs(b[i], 2);
    }
    return s;
}

}  // namespace

bool spell(const Insn& ins, int64_t pc, WidthVar& out) {
    out.clear();
    Bytes b;
    if (assemble_or_none(ins.text(), pc, b) && b == ins.raw) return true;
    std::string m = lower(ins.mnem);
    std::vector<std::string> texts;
    for (auto& o : ins.ops) texts.push_back(o.text);
    for (auto& var : width_variants(ins.ops)) {
        std::string t = m + "\t" + join(apply_width(texts, var), ",");
        if (assemble_or_none(t, pc, b) && b == ins.raw) {
            out = var;
            return true;
        }
    }
    return false;
}

namespace {

// The cartridge header's fields (SNK SysPro, SOFTWARE CASSETTE RECOGNITION HEADER INFORMATION
// FORMAT): offset, length, comment. Offset 0x1C (the startup address) is written as `dl`.
struct HeaderField {
    int64_t off, len;
    const char* what;
};
const HeaderField HEADER[] = {
    {0x00, 28, "recognition code"},
    {0x1C, 4, "startup address"},
    {0x20, 2, "ID code (BCD)"},
    {0x22, 1, "ID sub-code (version)"},
    {0x23, 1, "compatible system (0x00 monochrome, 0x10 color)"},
    {0x24, 12, "title"},
    {0x30, 16, "reserved (0)"},
};
const int64_t HEADER_SIZE = 0x40;
const char* const HEADER_CITE = "SNK SysPro, SOFTWARE CASSETTE RECOGNITION HEADER INFORMATION FORMAT";

// bytes that can be written inside "..." and read back unchanged
bool plain_string(const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (b[i] < 0x20 || b[i] > 0x7E || b[i] == '"' || b[i] == '\'' || b[i] == '\\' || b[i] == ';') return false;
    return n > 0;
}

uint32_t le32(const uint8_t* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }

// hardware names an address immediate may be written as: not the I/O registers (0x00-0xFF,
// which collide with small counts), see docs/language.md, "Addresses used as values"
bool hw_value_kind(const std::string& k) { return k == "sysram" || k == "vector" || k == "k2ge" || k == "mem"; }

}  // namespace

WriteStats write_source(const Rom& rom, const InsnMap& insns, const Labels& labels, const std::string& outdir,
                        const SourceOptions& opt, NameStats* ns) {
    const Hints* hints = opt.hints;
    const std::string& module = opt.module;
    make_dirs(outdir);
    const Bytes& data = rom.data;
    int64_t end = CART_BASE + int64_t(data.size());
    int64_t chip_end = std::min(end, CART_BASE + 0x200000);   // addresses a value can name
    std::map<int64_t, std::string> hw, hw_value;
    for (auto& h : hw_names()) {
        hw[h.addr] = h.name;
        if (hw_value_kind(h.kind)) hw_value[h.addr] = h.name;
    }
    std::map<std::string, int64_t> used_hw;

    // instruction starts that will actually be emitted (overlapping decodes are dropped), and the
    // bytes they cover
    std::vector<bool> emitted(data.size(), false), covered(data.size(), false);
    for (int64_t pc = CART_BASE; pc < end;) {
        const Insn* ins = insns.get(pc);
        if (ins) {
            emitted[size_t(pc - CART_BASE)] = true;
            for (int64_t b = pc; b < pc + ins->size && b < end; b++) covered[size_t(b - CART_BASE)] = true;
            pc += ins->size;
        } else {
            pc++;
        }
    }
    auto is_emitted = [&](int64_t a) { return a >= CART_BASE && a < end && emitted[size_t(a - CART_BASE)]; };
    auto is_covered = [&](int64_t a) { return a >= CART_BASE && a < end && covered[size_t(a - CART_BASE)]; };
    // the header is written field by field when none of it is code
    bool header = int64_t(data.size()) >= HEADER_SIZE;
    for (int64_t a = CART_BASE; header && a < CART_BASE + HEADER_SIZE; a++)
        if (is_covered(a)) header = false;
    auto header_field = [&](int64_t a) -> const HeaderField* {
        if (!header || a < CART_BASE || a >= CART_BASE + HEADER_SIZE) return nullptr;
        for (auto& f : HEADER)
            if (CART_BASE + f.off == a) return &f;
        return nullptr;
    };
    // the first bytes of the pointers hints' words, where all four bytes are data
    auto pointer_word = [&](int64_t a) {
        if (!hints || !hints->is_pointer(a) || a + 4 > end) return false;
        for (int64_t b = a; b < a + 4; b++)
            if (is_covered(b) || (header && b < CART_BASE + HEADER_SIZE)) return false;
        return true;
    };
    // where a data label can go: a data byte that doesn't split a header field or a dl word
    auto data_label_ok = [&](int64_t a) {
        if (a < CART_BASE || a >= chip_end || is_covered(a)) return false;
        if (header && a < CART_BASE + HEADER_SIZE) return header_field(a) != nullptr;
        for (int64_t b = a - 3; b < a; b++)
            if (pointer_word(b)) return false;
        return true;
    };

    std::map<int64_t, std::string> names;
    for (auto& kv : labels)
        if (is_emitted(kv.first)) names[kv.first] = kv.second;
    // Every jump or call target that is an instruction start in this source gets a placeholder
    // label if it has none (sub_ for a call target, else loc_, as the trace names them), so no
    // branch into the code is written as a bare number. Calls first: a target both called and
    // jumped to is sub_. A target inside data or inside an instruction keeps its number.
    for (int calls = 1; calls >= 0; calls--)
        for (int64_t pc = CART_BASE; pc < end; pc++) {
            if (!is_emitted(pc)) continue;
            const Insn& i = insns.at(pc);
            if (i.has_target && i.is_call == bool(calls) && is_emitted(i.target) && !names.count(i.target))
                names[i.target] = std::string(calls ? "sub" : "loc") + "_" + hexs(i.target, 6);
        }

    // A names file's names: a ROM address that is an instruction start or a data byte here is
    // labelled with it (the first name replaces any placeholder; more names at one address are
    // extra labels); any other address is an equ in names.inc, written when the source uses it.
    std::set<int64_t> user_named;
    std::map<int64_t, std::vector<std::string>> extra;   // more labels at one address
    std::map<int64_t, std::string> ram;                  // address -> its first name
    std::map<std::string, int64_t> used_ram;
    NameStats nst;
    if (opt.names) {
        nst.skipped = opt.names->skipped;
        for (auto& e : opt.names->entries) {
            int64_t a = e.first;
            if (a >= CART_BASE && a < end) {
                if (!(a < chip_end && (is_emitted(a) || data_label_ok(a)))) {
                    nst.unplaced++;
                    continue;
                }
                if (user_named.insert(a).second) names[a] = e.second;
                else extra[a].push_back(e.second);
                nst.labels++;
            } else {
                ram.emplace(a, e.second);
                nst.equs++;
            }
        }
    }
    if (ns) *ns = nst;

    // Addresses used as values (docs/language.md, "Addresses used as values"): an immediate of an
    // instruction with a 32-bit register operand, an address operand (lda, ldar, a memory
    // operand), a pointers hint's word, the header's startup address. One that is an instruction
    // start or a data byte here gets a label: code_ or dat_ if it has none yet.
    auto is_num = [](const std::string& t) { return is_hexnum(t, 0, t.size()); };
    auto has_reg32 = [](const Insn& i) {
        for (auto& o : i.ops) {
            auto it = REG().find(o.text);
            if (it != REG().end() && it->second.size == 4) return true;
        }
        return false;
    };
    // the operand's value if it is an address used as a value or a memory operand in the ROM
    auto value_of = [&](const Insn& i, const Op& o, int64_t& v) {
        if (o.ref) {
            if (o.kind == 'c') return false;   // branch targets: labelled above
            v = o.addr;
            return true;
        }
        if (!is_num(o.text) || !has_reg32(i)) return false;
        v = py_int0(o.text);
        return true;
    };
    auto want = [&](int64_t v) {
        if (names.count(v)) return;
        if (is_emitted(v) && v < chip_end) names[v] = "code_" + hexs(v, 6);
        else if (data_label_ok(v)) names[v] = "dat_" + hexs(v, 6);
    };
    for (int64_t pc = CART_BASE; pc < end; pc++) {
        if (!is_emitted(pc)) continue;
        const Insn& i = insns.at(pc);
        for (auto& o : i.ops) {
            int64_t v;
            if (value_of(i, o, v)) want(v);
        }
    }
    if (header) want(int64_t(le32(data.data() + 0x1C)));
    for (int64_t a = CART_BASE; hints && a < end; a++)
        if (pointer_word(a)) want(int64_t(le32(data.data() + (a - CART_BASE))));

    // a value as a ROM label, or a hardware name when `hw_ok`, or ""
    auto value_name = [&](int64_t v, bool hw_ok) -> std::string {
        auto it = names.find(v);
        if (it != names.end() && v >= CART_BASE && v < chip_end) return it->second;
        if (hw_ok && v >= 0x100) {   // a names file's other names, not over small numbers
            auto r = ram.find(v);
            if (r != ram.end()) {
                used_ram[r->second] = v;
                return r->second;
            }
        }
        if (hw_ok) {
            auto h = hw_value.find(v);
            if (h != hw_value.end()) {
                used_hw[h->second] = v;
                return h->second;
            }
        }
        return "";
    };
    auto sym = [&](const Insn& i, size_t k) -> std::string {
        const Op& o = i.ops[k];
        if (!o.ref) {
            int64_t v;
            if (!value_of(i, o, v)) return o.text;
            // a hardware name only for `ld XRR,n`, the form that loads a pointer
            std::string n = value_name(v, i.mnem == "LD" && k == 1);
            return n.empty() ? o.text : n;
        }
        if (o.kind == 'c' || o.text.empty() || o.text[0] != '(') {
            auto it = names.find(o.addr);
            if (it != names.end() && !it->second.empty()) return it->second;
            return o.text;
        }
        auto r = ram.find(o.addr);
        if (r != ram.end()) {
            used_ram[r->second] = o.addr;
            return "(" + r->second + ")";
        }
        auto it = hw.find(o.addr);
        if (it != hw.end()) {
            used_hw[it->second] = o.addr;
            return "(" + it->second + ")";
        }
        std::string n = value_name(o.addr, false);
        return n.empty() ? o.text : "(" + n + ")";
    };
    auto dl_text = [&](int64_t v) {
        std::string n = value_name(v, true);
        return n.empty() ? "0x" + hexs(v, 8) : n;
    };

    // Local labels: a loc_ placeholder between a routine's label and the next one becomes
    // ROUTINE.N, numbered from 1 in address order. A routine's label is any label without a '.'
    // that is not a loc_ placeholder; a run of code with none keeps its loc_ labels.
    {
        std::string g;
        int n = 0;
        int64_t next = -1;   // where the current run of code continues
        for (int64_t pc = CART_BASE; pc < end; pc++) {
            if (!is_emitted(pc)) continue;
            if (pc != next) g.clear();   // data in between: a new run
            next = pc + insns.at(pc).size;
            auto it = names.find(pc);
            if (it == names.end()) continue;
            const std::string& nm = it->second;
            if (starts_with(nm, "loc_") && !user_named.count(pc)) {
                if (!g.empty()) it->second = g + "." + dec(++n);
            } else if (nm.find('.') == std::string::npos) {
                g = nm;
                n = 0;
            }
        }
    }

    // `; @ADDR` comments
    auto tag = [&](std::string line, int64_t a) {
        if (!opt.addr_comments) return line;
        std::string at = "@" + hexs(a, 6);
        size_t c = line.find("\t; ");
        if (c == std::string::npos) return line + "\t; " + at;
        return line.insert(c + 3, at + " ");
    };

    RomHeader h = rom.header();
    std::vector<std::string> lines = {"; " + py_repr(h.title) + ": asm900 source written by `unitron dis` from the ROM.",
                                      "; Build: unitron asm main.asm -o out.ngp --origin 0x" + hexs(CART_BASE) + " --size 0x" +
                                          hexs(int64_t(data.size())),
                                      "$maximum",
                                      "\tmodule\t" + module,
                                      "$include \"hw.inc\""};
    if (opt.names) lines.push_back("$include \"names.inc\"");
    lines.push_back("");
    auto label_lines = [&](int64_t a) {
        auto nm = names.find(a);
        if (nm != names.end()) lines.push_back(nm->second + ":");
        auto ex = extra.find(a);
        if (ex != extra.end())
            for (auto& x : ex->second) lines.push_back(x + ":");
    };
    WriteStats st;
    int in_code = -1;   // None
    for (int64_t pc = CART_BASE; pc < end;) {
        const Insn* ins = is_emitted(pc) ? insns.get(pc) : nullptr;
        if (ins) {
            if (in_code != 1) {
                lines.push_back("\nc_" + hexs(pc, 6) + "\tsection\tcode abs=0x" + hexs(pc, 6));
                st.sections++;
                in_code = 1;
            }
            label_lines(pc);
            std::string why = ins->illegal;
            WidthVar var;
            if (why.empty() && !spell(*ins, pc, var)) why = "no asm900 spelling for this encoding";
            if (!why.empty()) {
                lines.push_back(tag(db_line(ins->raw.data(), ins->raw.size()) + "\t; " + replace_all(ins->text(), "\t", " ") + " (" +
                                    why + ")", pc));
                st.db_fallback++;
            } else {
                if (!var.empty()) st.width_marked++;
                std::vector<std::string> ops;
                for (size_t k = 0; k < ins->ops.size(); k++) ops.push_back(sym(*ins, k));
                ops = apply_width(ops, var);
                std::string t = ops.empty() ? lower(ins->mnem) : lower(ins->mnem) + "\t" + join(ops, ",");
                if (!ins->comment.empty()) t += "\t; " + ins->comment;
                lines.push_back(tag("\t" + t, pc));
            }
            st.instructions++;
            pc += ins->size;
            continue;
        }
        int64_t q = pc;
        while (q < end && !is_emitted(q)) q++;
        lines.push_back("\nd_" + hexs(pc, 6) + "\tsection\tromdata abs=0x" + hexs(pc, 6));
        st.sections++;
        in_code = 0;
        st.data_bytes += q - pc;
        // up to the next label, header field, dl word or the end of the block
        auto stop = [&](int64_t a) { return names.count(a) || header_field(a) || pointer_word(a); };
        auto bytes = [&](int64_t a) { return data.data() + (a - CART_BASE); };
        while (pc < q) {
            const HeaderField* f = header_field(pc);
            if (f && f->off == 0) lines.push_back("; the cartridge header (" + std::string(HEADER_CITE) + ")");
            label_lines(pc);
            if (f) {
                const uint8_t* b = bytes(pc);
                std::string t;
                if (f->off == 0x1C) t = "\tdl\t" + dl_text(le32(b));
                else if (f->off == 0x20) t = "\tdw\t0x" + hexs(int64_t(b[0]) | int64_t(b[1]) << 8, 4);
                else if ((f->off == 0 || f->off == 0x24) && plain_string(b, size_t(f->len)))
                    t = "\tdb\t\"" + std::string(b, b + f->len) + "\"";
                if (!t.empty()) {
                    lines.push_back(tag(t + "\t; " + f->what, pc));
                } else {
                    for (int64_t i = 0; i < f->len; i += DB_PER_LINE)
                        lines.push_back(tag(db_line(b + i, size_t(std::min<int64_t>(DB_PER_LINE, f->len - i))) + (i ? "" : "\t; " + std::string(f->what)), pc + i));
                }
                pc += f->len;
                continue;
            }
            if (pointer_word(pc)) {
                int64_t at = pc;
                std::vector<std::string> w;
                do {
                    w.push_back(dl_text(le32(bytes(pc))));
                    pc += 4;
                } while (w.size() < 4 && pc < q && pointer_word(pc) && !names.count(pc));
                lines.push_back(tag("\tdl\t" + join(w, ","), at));
                continue;
            }
            // a run of one byte value, at least DFB_MIN long: dfb n,v
            auto run = [&](int64_t a, int64_t cap) {
                int64_t r = 1;
                while (r < cap && a + r < q && !stop(a + r) && data[size_t(a + r - CART_BASE)] == data[size_t(a - CART_BASE)]) r++;
                return r;
            };
            int64_t r = run(pc, INT64_MAX);
            if (r >= DFB_MIN) {
                lines.push_back(tag("\tdfb\t" + dec(r) + ",0x" + hexs(data[size_t(pc - CART_BASE)], 2), pc));
                pc += r;
                continue;
            }
            int64_t e = pc + 1;
            while (e < q && e - pc < DB_PER_LINE && !stop(e) && run(e, DFB_MIN) < DFB_MIN) e++;
            lines.push_back(tag(db_line(bytes(pc), size_t(e - pc)), pc));
            pc = e;
        }
    }
    lines.push_back("");
    lines.push_back("\tend");
    lines.push_back("");
    write_text(path_join(outdir, "main.asm"), join(lines, "\n"));
    std::map<std::string, std::pair<std::string, std::string>> cite;
    for (auto& n : hw_names()) cite[n.name] = {n.cite, n.official};
    std::vector<std::pair<std::string, int64_t>> used(used_hw.begin(), used_hw.end());
    std::stable_sort(used.begin(), used.end(), [](auto& a, auto& b) { return a.second < b.second; });
    // the names this source uses, in three files by part of the machine (all three always written)
    std::map<std::string, std::string> kind;
    for (auto& n : hw_names()) kind[n.name] = n.kind;
    struct Group {
        const char* file;
        const char* what;
        std::string text;
    };
    Group groups[] = {
        {"cpu.inc", "the CPU's on-chip I/O (Toshiba IO900H.H, TMP95C061F)", ""},
        {"system.inc", "system work RAM, interrupt vectors, Z80 RAM (SNK SysPro, SysWork)", ""},
        {"k2ge.inc", "the K2GE video chip, 0x8000-0xBFFF (SNK K2GETechRef, 4-2. MEMORY MAP)", ""},
    };
    for (auto& u : used) {
        auto& c = cite[u.first];
        int g = kind[u.first] == "io" ? 0 : (u.second >= K2GE_LO && u.second <= K2GE_HI) ? 2 : 1;
        groups[g].text += u.first + "\tequ\t0x" + hexs(u.second) + "\t; " + c.first + " (" + c.second + ")\n";
    }
    std::string inc = "; hw.inc: the NGPC hardware names this source uses (from unitron's data/ngpc_hw.inc), by part.\n";
    for (auto& g : groups) {
        inc += "$include \"" + std::string(g.file) + "\"\n";
        write_text(path_join(outdir, g.file), "; " + std::string(g.file) + ": " + g.what + ".\n" + g.text);
    }
    write_text(path_join(outdir, "hw.inc"), inc);
    if (opt.names) {
        std::vector<std::pair<std::string, int64_t>> ur(used_ram.begin(), used_ram.end());
        std::stable_sort(ur.begin(), ur.end(), [](auto& a, auto& b) { return a.second < b.second; });
        std::string ni = "; names.inc: the names from the names file that this source uses outside the ROM.\n";
        for (auto& u : ur) ni += u.first + "\tequ\t0x" + hexs(u.second) + "\n";
        write_text(path_join(outdir, "names.inc"), ni);
    }
    return st;
}

Names read_names(const std::string& path) {
    Bytes raw = read_bytes(path);
    std::set<std::string> hwn;
    for (auto& h : hw_names()) hwn.insert(h.name);
    for (auto& b : hw_bits()) hwn.insert(b.name);
    Names out;
    std::map<std::string, int64_t> seen;
    int ln = 0;
    for (auto& line0 : splitlines(std::string(raw.begin(), raw.end()))) {
        ln++;
        std::string line = line0.substr(0, std::min(line0.find('#'), line0.find(';')));
        auto w = split_ws(line);
        if (w.empty()) continue;
        std::string at = path + ":" + std::to_string(ln) + ": ";
        if (w.size() != 2) throw std::runtime_error(at + "expected `ADDR NAME`");
        int64_t a;
        try {
            std::string x = lower(w[0]);
            if (starts_with(x, "0x")) x = x.substr(2);
            size_t used = 0;
            a = int64_t(std::stoll(x, &used, 16));
            if (used != x.size()) throw std::invalid_argument(x);
        } catch (std::exception&) {
            throw std::runtime_error(at + "bad hex address " + py_repr(w[0]));
        }
        const std::string& n = w[1];
        bool ok = !n.empty() && (std::isalpha((unsigned char)n[0]) || n[0] == '_');
        for (char c : n)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.')) ok = false;
        if (!ok) throw std::runtime_error(at + "bad name " + py_repr(n));
        if (is_reserved(n)) throw std::runtime_error(at + py_repr(n) + " is an asm900 reserved word");
        if (hwn.count(n)) {
            out.skipped++;
            continue;
        }
        auto s = seen.find(n);
        if (s != seen.end()) {
            if (s->second != a) throw std::runtime_error(at + py_repr(n) + " is already 0x" + hexs(s->second));
            continue;
        }
        seen[n] = a;
        out.entries.push_back({a, n});
    }
    return out;
}

WriteStats disassemble_rom(const Rom& rom, const std::string& outdir, const DisOptions& o, NameStats* ns) {
    InsnMap insns;
    Labels labels;
    DiscoverStats ds;
    Hints hints;
    Names names;
    if (!o.hints.empty()) hints = read_hints(o.hints);
    if (!o.names.empty()) names = read_names(o.names);
    const Hints* hp = o.hints.empty() ? nullptr : &hints;
    discover(rom, o.executed, o.sweep, insns, labels, ds, hp);
    SourceOptions so;
    so.hints = hp;
    so.names = o.names.empty() ? nullptr : &names;
    so.addr_comments = o.addr_comments;
    WriteStats st = write_source(rom, insns, labels, outdir, so, ns);
    st.discover = ds;
    return st;
}

}  // namespace unitron
