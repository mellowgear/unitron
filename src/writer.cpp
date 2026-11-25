#include "writer.hpp"

#include <algorithm>
#include <map>

#include "hwnames.hpp"
#include "line.hpp"
#include "regs.hpp"

namespace unitron {

namespace {

const int DB_PER_LINE = 16;

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

WriteStats write_source(const Rom& rom, const InsnMap& insns, const Labels& labels, const std::string& outdir,
                        const std::string& module) {
    make_dirs(outdir);
    const Bytes& data = rom.data;
    int64_t end = CART_BASE + int64_t(data.size());
    std::map<int64_t, std::string> hw;
    for (auto& h : hw_names()) hw[h.addr] = h.name;
    std::map<std::string, int64_t> used_hw;

    // instruction starts that will actually be emitted (overlapping decodes are dropped)
    std::vector<bool> emitted(data.size(), false);
    for (int64_t pc = CART_BASE; pc < end;) {
        const Insn* ins = insns.get(pc);
        if (ins) {
            emitted[size_t(pc - CART_BASE)] = true;
            pc += ins->size;
        } else {
            pc++;
        }
    }
    auto is_emitted = [&](int64_t a) { return a >= CART_BASE && a < end && emitted[size_t(a - CART_BASE)]; };
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

    auto sym = [&](const Op& o) -> std::string {
        if (!o.ref) return o.text;
        if (o.kind == 'c' || o.text.empty() || o.text[0] != '(') {
            auto it = names.find(o.addr);
            if (it != names.end() && !it->second.empty()) return it->second;
            return o.text;
        }
        auto it = hw.find(o.addr);
        if (it != hw.end()) {
            used_hw[it->second] = o.addr;
            return "(" + it->second + ")";
        }
        return o.text;
    };

    RomHeader h = rom.header();
    std::vector<std::string> lines = {"; " + py_repr(h.title) + ": asm900 source written by `unitron dis` from the ROM.",
                                      "; Build: unitron asm main.asm -o out.ngp --origin 0x" + hexs(CART_BASE) + " --size 0x" +
                                          hexs(int64_t(data.size())),
                                      "$maximum",
                                      "\tmodule\t" + module,
                                      "$include \"hw.inc\"",
                                      ""};
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
            auto nm = names.find(pc);
            if (nm != names.end()) lines.push_back(nm->second + ":");
            std::string why = ins->illegal;
            WidthVar var;
            if (why.empty() && !spell(*ins, pc, var)) why = "no asm900 spelling for this encoding";
            if (!why.empty()) {
                lines.push_back(db_line(ins->raw.data(), ins->raw.size()) + "\t; " + replace_all(ins->text(), "\t", " ") + " (" +
                                why + ")");
                st.db_fallback++;
            } else {
                if (!var.empty()) st.width_marked++;
                std::vector<std::string> ops;
                for (auto& o : ins->ops) ops.push_back(sym(o));
                ops = apply_width(ops, var);
                std::string t = ops.empty() ? lower(ins->mnem) : lower(ins->mnem) + "\t" + join(ops, ",");
                if (!ins->comment.empty()) t += "\t; " + ins->comment;
                lines.push_back("\t" + t);
            }
            st.instructions++;
            pc += ins->size;
            continue;
        }
        int64_t q = pc;
        while (q < end && !is_emitted(q)) q++;
        lines.push_back("\nd_" + hexs(pc, 6) + "\tsection\tromdata abs=0x" + hexs(pc, 6));
        auto nm = names.find(pc);
        if (nm != names.end()) lines.push_back(nm->second + ":");
        st.sections++;
        in_code = 0;
        const uint8_t* chunk = data.data() + (pc - CART_BASE);
        size_t len = size_t(q - pc);
        for (size_t i = 0; i < len; i += DB_PER_LINE) lines.push_back(db_line(chunk + i, std::min<size_t>(DB_PER_LINE, len - i)));
        st.data_bytes += int64_t(len);
        pc = q;
    }
    lines.push_back("");
    lines.push_back("\tend");
    lines.push_back("");
    write_text(path_join(outdir, "main.asm"), join(lines, "\n"));
    std::map<std::string, std::pair<std::string, std::string>> cite;
    for (auto& n : hw_names()) cite[n.name] = {n.cite, n.official};
    std::vector<std::pair<std::string, int64_t>> used(used_hw.begin(), used_hw.end());
    std::stable_sort(used.begin(), used.end(), [](auto& a, auto& b) { return a.second < b.second; });
    std::string inc = "; hw.inc: the NGPC hardware names this source uses (from unitron/data/ngpc_hw.inc).\n";
    for (auto& u : used) {
        auto& c = cite[u.first];
        inc += u.first + "\tequ\t0x" + hexs(u.second) + "\t; " + c.first + " (" + c.second + ")\n";
    }
    write_text(path_join(outdir, "hw.inc"), inc);
    return st;
}

WriteStats disassemble_rom(const Rom& rom, const std::string& outdir, const std::string& executed, bool do_sweep,
                           const std::string& hints_path) {
    InsnMap insns;
    Labels labels;
    DiscoverStats ds;
    Hints hints;
    if (!hints_path.empty()) hints = read_hints(hints_path);
    discover(rom, executed, do_sweep, insns, labels, ds, hints_path.empty() ? nullptr : &hints);
    WriteStats st = write_source(rom, insns, labels, outdir);
    st.discover = ds;
    return st;
}

}  // namespace unitron
