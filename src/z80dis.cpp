// Z80 tracer, listing and source writer (see z80dis.hpp).
#include "z80dis.hpp"

#include <map>
#include <set>
#include <vector>

#include "trace.hpp"   // read_hints
#include "util.hpp"
#include "z80.hpp"

namespace unitron::z80 {

namespace {

struct Line {
    int64_t addr = 0;
    Bytes bytes;
    std::string label, text, comment;
};

struct Model {
    std::vector<Line> lines;
    DisStats st;
    bool end_used = false;   // something refers to the end of the image (END_LABEL)
};

std::string default_name(int64_t a) { return "L" + hexs(a, 4); }

const char* const END_LABEL = "DriverEnd";
constexpr int64_t END_REACH = 16;   // END_LABEL+0 .. END_LABEL+15

Model build(const Bytes& img, const DisOptions& opt) {
    const int64_t size = int64_t(img.size());
    auto in = [&](int64_t a) { return a >= 0 && a < size; };
    MemFn mem = [&](int64_t a) { return in(a) ? int(img[size_t(a)]) : -1; };
    Hints hints;
    if (!opt.hints.empty()) hints = read_hints(opt.hints);
    std::map<int64_t, std::string> names;   // chosen names (vectors, hints)
    std::vector<int64_t> entries;
    // the Z80's fixed entry points: reset (UM0080 p.7), NMI and mode 1 interrupt (p.19)
    const std::pair<int64_t, const char*> vec[] = {{0x0000, "Reset"}, {0x0038, "Int38"}, {0x0066, "Nmi"}};
    for (auto& v : vec)
        if (in(v.first)) {
            entries.push_back(v.first);
            names[v.first] = v.second;
        }
    for (auto& c : hints.code) {
        entries.push_back(c.first);
        if (c.second != "code_" + hexs(c.first, 6)) names[c.first] = c.second;
    }
    if (!opt.executed.empty()) {
        for (int64_t a : read_executed(opt.executed)) entries.push_back(a);
    }
    // ---- trace
    std::map<int64_t, Insn> code;
    std::vector<int64_t> owner(size_t(size), -1);
    std::set<int64_t> targets;   // addresses that need a label
    std::vector<int64_t> work(entries.rbegin(), entries.rend());
    while (!work.empty()) {
        int64_t a = work.back();
        work.pop_back();
        if (!in(a) || code.count(a) || owner[size_t(a)] >= 0 || hints.is_data(a)) continue;
        Insn ins = decode(mem, a);
        if (ins.size == 0) continue;
        bool clash = false;
        for (int k = 0; k < ins.size; k++)
            if (!in(a + k) || owner[size_t(a + k)] >= 0 || hints.is_data(a + k)) clash = true;
        if (clash) continue;   // the bytes already belong to another instruction, or are data
        code[a] = ins;
        for (int k = 0; k < ins.size; k++) owner[size_t(a + k)] = a;
        const Form* f = ins.f();
        if (!f) continue;   // not a documented instruction: the path stops here
        Flow fl = f->flow;
        if (fl != Flow::Jump && fl != Flow::Ret && fl != Flow::JumpInd) work.push_back(a + ins.size);
        if (ins.has_target()) {
            work.push_back(ins.target);
            targets.insert(ins.target);
            if (fl == Flow::Rst && !names.count(ins.target)) names[ins.target] = "Rst" + hexs(ins.target, 2);   // UM0080 p.292
        }
    }
    // memory operands that point into the image get labels too (self-modifying code, tables)
    for (auto& kv : code) {
        const Insn& ins = kv.second;
        const Form* f = ins.f();
        if (!f) continue;
        for (auto& p : f->ops)
            if (p.k == P::Mem && in(ins.nn)) targets.insert(ins.nn);
    }
    // ---- labels: a target inside an instruction is written as that instruction's label + k
    std::map<int64_t, std::string> label;
    auto label_at = [&](int64_t a) -> std::string& {
        auto it = label.find(a);
        if (it != label.end()) return it->second;
        auto nm = names.find(a);
        return label[a] = nm != names.end() ? nm->second : default_name(a);
    };
    for (auto& kv : names)
        if (code.count(kv.first)) label_at(kv.first);
    for (int64_t t : targets)
        if (in(t)) label_at(owner[size_t(t)] >= 0 ? owner[size_t(t)] : t);
    // The end of the image and the few bytes after it: the drivers keep their data right after the
    // code and hard-code those addresses (Z80_PLAN.md 4.3). Written as END_LABEL+k, they follow the
    // code when it grows or shrinks. A jump or call there stays a number.
    bool end_used = false;
    auto end_ref = [&](int64_t a) -> std::string {
        if (a < size || a >= size + END_REACH) return "";
        end_used = true;
        return a == size ? std::string(END_LABEL) : std::string(END_LABEL) + "+" + std::to_string(a - size);
    };
    NameFn name = [&](int64_t a, char kind) -> std::string {
        if (kind == 'm' || kind == 'i') {
            std::string e = end_ref(a);
            if (!e.empty()) return e;
        }
        if (kind == 'i' || !in(a)) return "";   // other immediates stay numbers
        int64_t base = owner[size_t(a)] >= 0 ? owner[size_t(a)] : a;
        auto it = label.find(base);
        if (it == label.end()) return "";
        return a == base ? it->second : it->second + "+" + std::to_string(a - base);
    };
    // ---- lines
    Model m;
    auto hexb = [](uint8_t b) { return hexz(b, 2); };
    auto push = [&](int64_t a, Bytes b, std::string text, std::string comment = "") {
        Line l;
        l.addr = a;
        l.bytes = std::move(b);
        l.text = std::move(text);
        l.comment = std::move(comment);
        auto it = label.find(a);
        if (it != label.end()) l.label = it->second;
        m.lines.push_back(std::move(l));
    };
    int64_t a = 0;
    while (a < size) {
        auto c = code.find(a);
        if (c != code.end()) {
            const Insn& ins = c->second;
            const Form* f = ins.f();
            if (f && !f->alt) {
                push(a, ins.raw, text(ins, &name));
                m.st.instructions++;
            } else {
                // UM0080 lists no such instruction (or the assembler would pick the shorter code)
                std::string why;
                if (f) {
                    Insn plain = ins;
                    std::string t = text(plain, &name);
                    size_t tab = t.find('\t');
                    if (tab != std::string::npos) t[tab] = ' ';
                    why = t + " (the ED " + hexs(ins.raw[1], 2) + " encoding; UM0080 p.45)";
                } else {
                    why = "not a documented Z80 instruction";
                }
                for (int k = 0; k < ins.size; k++) push(a + k, {ins.raw[size_t(k)]}, "DEFB\t" + hexb(ins.raw[size_t(k)]), k ? "" : why);
                m.st.db_fallback += ins.size;
            }
            a += ins.size;
            continue;
        }
        // data up to the next code or label
        int64_t e = a + 1;
        while (e < size && !code.count(e) && !label.count(e)) e++;
        m.st.data_bytes += e - a;
        // a data word that holds the image's size is a pointer to its end: DEFW END_LABEL
        auto end_word = [&](int64_t p) { return p + 1 < e && (int64_t(img[size_t(p)]) | int64_t(img[size_t(p + 1)]) << 8) == size; };
        while (a < e) {
            if (end_word(a)) {
                end_used = true;
                push(a, {img[size_t(a)], img[size_t(a + 1)]}, "DEFW\t" + std::string(END_LABEL));
                a += 2;
                continue;
            }
            int64_t z = a;
            while (z < e && img[size_t(z)] == 0 && !end_word(z)) z++;
            if (z - a >= 8) {   // a run of zeros: DEFS (unitron fills DEFS with zeros, docs/z80.md)
                push(a, Bytes(img.begin() + a, img.begin() + z), "DEFS\t" + hexz(z - a, z - a > 0xFF ? 4 : 2));
                a = z;
                continue;
            }
            int64_t p = a;
            while (p < e && p - a < 63 && img[size_t(p)] >= 0x20 && img[size_t(p)] < 0x7F && !end_word(p)) p++;
            if (p - a >= 8) {   // printable text: DEFM (at most 63 characters, ZM p.11)
                std::string s = "'";
                for (int64_t k = a; k < p; k++) s += img[size_t(k)] == '\'' ? std::string("''") : std::string(1, char(img[size_t(k)]));
                push(a, Bytes(img.begin() + a, img.begin() + p), "DEFM\t" + s + "'");
                a = p;
                continue;
            }
            push(a, {img[size_t(a)]}, "DEFB\t" + hexb(img[size_t(a)]));
            a++;
        }
    }
    m.end_used = end_used;
    return m;
}

}  // namespace

std::string listing(const Bytes& img, const DisOptions& opt, DisStats* st) {
    Model m = build(img, opt);
    if (st) *st = m.st;
    std::string out;
    for (auto& l : m.lines) {
        if (!l.label.empty()) out += l.label + ":\n";
        std::string raw;
        for (size_t k = 0; k < l.bytes.size() && k < 4; k++) raw += (k ? " " : "") + hexs(l.bytes[k], 2);
        if (l.bytes.size() > 4) raw += " ..";
        while (raw.size() < 15) raw += ' ';
        std::string t = l.text;
        size_t tab = t.find('\t');
        if (tab != std::string::npos) t[tab] = ' ';
        out += hexs(l.addr, 4) + "  " + raw + t + (l.comment.empty() ? "" : "  ; " + l.comment) + "\n";
    }
    if (m.end_used) out += std::string(END_LABEL) + ":\n" + hexs(int64_t(img.size()), 4) + "  " + std::string(15, ' ') + "; the end of the image\n";
    return out;
}

std::string source(const Bytes& img, const DisOptions& opt, const std::string& name, DisStats* st) {
    Model m = build(img, opt);
    if (st) *st = m.st;
    std::string out = "; " + name + ": a Z80 program of " + std::to_string(img.size()) + " bytes, written by unitron z80 dis.\n" +
                      "; Zilog syntax (unitron docs/z80.md); unitron z80 asm assembles it back to the same bytes.\n" +
                      "\tORG\t0000H\n";
    for (auto& l : m.lines) {
        if (!l.label.empty()) out += l.label + ":\n";
        out += "\t" + l.text + (l.comment.empty() ? "" : "\t; " + l.comment) + "\n";
    }
    if (m.end_used) out += std::string(END_LABEL) + ":\n";
    return out + "\tEND\n";
}

}  // namespace unitron::z80
