#include "trace.hpp"

#include <algorithm>
#include <deque>
#include <set>
#include <unordered_set>

#include "hwnames.hpp"

namespace unitron {

void InsnMap::set(int64_t a, const Insn& i) {
    auto it = index_.find(a);
    if (it != index_.end()) {
        it->second->second = i;
        return;
    }
    order_.push_back({a, i});
    index_[a] = std::prev(order_.end());
}

Insn InsnMap::pop(int64_t a) {
    auto it = index_.at(a);
    Insn i = std::move(it->second);
    order_.erase(it);
    index_.erase(a);
    return i;
}

const std::map<int64_t, std::string>& VECTORS() {
    static const std::map<int64_t, std::string> v = [] {
        std::map<int64_t, std::string> m;
        for (auto& h : hw_names())
            if (h.kind == "vector") m[h.addr] = h.name;
        return m;
    }();
    return v;
}

void annotate_bios(Insn& ins, Regs& regs) {
    if (ins.mnem == "LD" && ins.ops.size() == 2 && ins.ops[0].text == "RW3") {
        try {
            regs["RW3"] = py_int0(ins.ops[1].text);
        } catch (std::exception&) {
            regs.erase("RW3");
        }
    } else if (ins.mnem == "SWI" && ins.ops.size() == 1 && ins.ops[0].text == "1") {
        auto it = regs.find("RW3");
        ins.comment = it != regs.end() ? "BIOS call 0x" + hexs(it->second, 2) : "BIOS call (RW3 not known here)";
    }
}

static MemFn memfn(const Rom& rom) {
    return [&rom](int64_t a) { return rom(a); };
}

void trace(const Rom& rom, const std::vector<std::pair<int64_t, std::string>>& entries, InsnMap& insns, Labels& labels) {
    std::deque<int64_t> work;
    for (auto& e : entries) {
        labels.emplace(e.first, e.second);
        work.push_back(e.first);
    }
    MemFn mem = memfn(rom);
    while (!work.empty()) {
        int64_t pc = work.front();
        work.pop_front();
        Regs regs;   // last 32-bit immediate per register (for vector installs)
        while (rom.contains(pc) && !insns.has(pc)) {
            insns.set(pc, decode(mem, pc));
            Insn& ins = insns.at(pc);
            if (ins.has_imm32) regs[ins.dst_reg] = ins.imm32;
            annotate_bios(ins, regs);
            // "LD (vector),XRR" after "LD XRR,handler" installs an IRQ handler
            if (ins.mnem == "LD" && ins.ops.size() == 2 && ins.ops[0].ref) {
                int64_t v = ins.ops[0].addr;
                auto vi = VECTORS().find(v);
                auto ri = regs.find(ins.ops[1].text);
                if (vi != VECTORS().end() && ri != regs.end() && rom.contains(ri->second)) {
                    int64_t h = ri->second;
                    labels.emplace(h, "irq_" + lower(vi->second.substr(4)));
                    work.push_back(h);
                }
            }
            if (ins.has_target && rom.contains(ins.target)) {
                std::string pre = ins.is_call ? "sub" : "loc";
                labels.emplace(ins.target, pre + "_" + hexs(ins.target, 6));
                work.push_back(ins.target);
            }
            if (ins.invalid || ins.ends_block) break;
            pc += ins.size;
        }
    }
}

namespace {

bool bad_insn(const Insn& ins) {
    if (ins.invalid || !ins.illegal.empty()) return true;
    if (ins.mnem == "NORMAL" || ins.mnem == "MAX" || ins.mnem == "HALT" || ins.mnem == "LDX") return true;
    if (ins.mnem == "SWI" && !(ins.ops.size() == 1 && ins.ops[0].text == "1")) return true;
    for (auto& o : ins.ops)
        if (o.text.find("r?") != std::string::npos) return true;   // undefined register code
    return false;
}

using Comp = std::vector<std::pair<int64_t, Insn>>;   // insertion-ordered

// Trace from seed into a scratch component, stopping at known code; false if any of it looks like data.
bool component(const Rom& rom, int64_t seed, const InsnMap& insns, int64_t lo, int64_t hi, Comp& out, const Hints* hints) {
    out.clear();
    std::unordered_map<int64_t, size_t> idx;
    std::deque<int64_t> work{seed};
    MemFn mem = memfn(rom);
    while (!work.empty()) {
        int64_t pc = work.front();
        work.pop_front();
        int nops = 0;
        bool broke = false;
        while (lo <= pc && pc < hi && !insns.has(pc) && !idx.count(pc)) {
            Insn ins = decode(mem, pc);
            if (bad_insn(ins)) return false;
            if (hints)
                for (int64_t b = pc; b < pc + ins.size; b++)
                    if (hints->is_data(b)) return false;
            nops = ins.mnem == "NOP" ? nops + 1 : 0;
            if (nops >= 2) return false;
            idx[pc] = out.size();
            out.push_back({pc, ins});
            const Insn& in = out.back().second;
            if (in.has_target) {
                if (!(lo <= in.target && in.target < hi)) return false;
                if (!in.is_call) work.push_back(in.target);   // callees are validated on their own
            }
            if (in.ends_block) {
                broke = true;
                break;
            }
            pc += in.size;
        }
        if (!broke && !(lo <= pc && pc < hi)) return false;
    }
    // no overlaps with known code or within the component
    std::unordered_set<int64_t> cov;
    for (auto& ai : out)
        for (int64_t b = ai.first; b < ai.first + ai.second.size; b++) {
            if (cov.count(b)) return false;
            cov.insert(b);
        }
    for (auto& ai : out)
        for (int64_t b = ai.first; b < ai.first + ai.second.size; b++)
            if (b != ai.first && insns.has(b)) return false;
    return true;
}

}  // namespace

int64_t sweep(const Rom& rom, InsnMap& insns, Labels& labels, int rounds, const Hints* hints) {
    if (insns.empty()) return 0;
    // [lo, hi): from the first to the last traced instruction. Inside it the sweep works as it
    // always has. [wlo, whi): the whole image that can hold code (after the 0x40-byte header, on
    // the first 2 MB chip, which is what write_source writes as code). Outside [lo, hi) only
    // stronger evidence starts a component (see `anchored` and the seed loop below).
    int64_t lo = INT64_MAX, hi = INT64_MIN;
    for (auto& ai : insns) {
        lo = std::min(lo, ai.first);
        hi = std::max(hi, ai.first + ai.second.size);
    }
    const Bytes& d = rom.data;
    int64_t wlo = std::min(lo, CART_BASE + std::min<int64_t>(0x40, int64_t(d.size())));
    int64_t whi = std::max(hi, CART_BASE + std::min<int64_t>(int64_t(d.size()), 0x200000));
    size_t n0 = insns.size();
    std::unordered_set<int64_t> ptrs;
    std::unordered_map<int64_t, std::vector<int64_t>> far_ptrs;   // value outside [lo, hi) -> its ROM offsets
    auto word_at = [&](int64_t off) -> int64_t {
        if (off < 0 || off + 3 >= int64_t(d.size())) return -1;
        size_t o = size_t(off);
        return int64_t(d[o]) | int64_t(d[o + 1]) << 8 | int64_t(d[o + 2]) << 16 | int64_t(d[o + 3]) << 24;
    };
    for (size_t off = 0; off + 3 < d.size(); off++) {
        int64_t v = word_at(int64_t(off));
        if (lo <= v && v < hi) ptrs.insert(v);
        else if (wlo <= v && v < whi) far_ptrs[v].push_back(int64_t(off));
    }
    std::unordered_map<int64_t, int64_t> weak;               // addr -> component id (pointer-seeded code)
    std::map<int64_t, std::vector<int64_t>> comps;           // component id -> addrs
    std::unordered_map<int64_t, int64_t> cov;
    for (auto& ai : insns)
        for (int64_t b = ai.first; b < ai.first + ai.second.size; b++) cov[b] = ai.first;

    auto place = [&](const Comp& comp, int64_t sa, bool is_weak) {
        std::unordered_set<int64_t> in_comp;
        for (auto& ai : comp) in_comp.insert(ai.first);
        for (auto& ai : comp) insns.set(ai.first, ai.second);
        for (auto& ai : comp) {
            const Insn& i = ai.second;
            for (int64_t b = ai.first; b < ai.first + i.size; b++) cov[b] = ai.first;
            if (i.has_target && in_comp.count(i.target))
                labels.emplace(i.target, std::string(i.is_call ? "sub" : "loc") + "_" + hexs(i.target, 6));
        }
        if (is_weak) {
            std::vector<int64_t> addrs;
            for (auto& ai : comp) {
                addrs.push_back(ai.first);
                weak[ai.first] = sa;
            }
            std::sort(addrs.begin(), addrs.end());
            comps[sa] = addrs;
        }
    };
    auto evict = [&](int64_t cid) {
        std::vector<int64_t> addrs = comps.at(cid);
        comps.erase(cid);
        for (int64_t a : addrs) {
            Insn i = insns.pop(a);
            weak.erase(a);
            for (int64_t b = a; b < a + i.size; b++) {
                auto c = cov.find(b);
                if (c != cov.end() && c->second == a) cov.erase(c);
            }
            auto l = labels.find(a);
            if (l != labels.end() && (starts_with(l->second, "swp_") || starts_with(l->second, "loc_") || starts_with(l->second, "sub_")))
                labels.erase(l);
        }
    };

    // A pointer outside [lo, hi) is a seed only when it sits in a table: a run of 4-byte words, all
    // in [wlo, whi), at least one of which is the address of a labelled instruction the trace or the
    // sweep has already found. The word itself must not be inside known code.
    auto anchored = [&](int64_t v) {
        auto fp = far_ptrs.find(v);
        if (fp == far_ptrs.end()) return false;
        for (int64_t off : fp->second) {
            if (cov.count(off < 0x200000 ? CART_BASE + off : CART_HI_BASE + off - 0x200000)) continue;
            for (int dir : {-4, 4})
                for (int64_t o = off + dir;; o += dir) {
                    int64_t w = word_at(o);
                    if (!(wlo <= w && w < whi)) break;
                    if (insns.has(w) && labels.count(w)) return true;
                }
        }
        return false;
    };

    for (int phase = 0; phase < 2; phase++) {
        for (int round = 0; round < rounds; round++) {
            std::unordered_map<int64_t, int64_t> callers;
            for (auto& ai : insns)
                if (ai.second.is_call && ai.second.has_target) callers[ai.second.target]++;
            std::unordered_set<int64_t> heads;
            for (auto& ai : insns)
                if (ai.second.ends_block) heads.insert(ai.first + ai.second.size);
            std::set<int64_t> pool;
            for (auto& c : callers) pool.insert(c.first);
            for (auto h : heads) pool.insert(h);
            std::unordered_set<int64_t> far;   // anchored pointers outside [lo, hi)
            if (phase) {
                for (auto p : ptrs) pool.insert(p);
                for (auto& fp : far_ptrs)
                    if (!insns.has(fp.first) && anchored(fp.first)) {
                        far.insert(fp.first);
                        pool.insert(fp.first);
                    }
            }
            std::vector<int64_t> seeds(pool.begin(), pool.end());
            auto key = [&](int64_t x) {
                int cls = callers.count(x) ? 0 : heads.count(x) ? 1 : 2;
                int64_t nc = callers.count(x) ? callers.at(x) : 0;
                return std::make_tuple(cls, -nc, x);
            };
            std::sort(seeds.begin(), seeds.end(), [&](int64_t a, int64_t b) { return key(a) < key(b); });
            int accepted = 0;
            for (int64_t sa : seeds) {
                bool inside = lo <= sa && sa < hi;
                // outside [lo, hi): only call targets and anchored pointers, never block heads
                if (!(inside || (wlo <= sa && sa < whi && rom.contains(sa) && (callers.count(sa) || far.count(sa))))) continue;
                if (insns.has(sa)) continue;
                // a component seeded inside [lo, hi) must stay inside it, as before
                int64_t clo = inside ? lo : wlo, chi = inside ? hi : whi;
                if (hints && hints->is_data(sa)) continue;
                if (rom(sa) == 0xFF || (rom(sa) == 0 && rom(sa + 1) == 0)) continue;
                bool strong = callers.count(sa) || (inside && heads.count(sa));
                auto cs = cov.find(sa);
                if (cs != cov.end() && !(strong && weak.count(cs->second))) continue;
                Comp comp;
                bool ok = component(rom, sa, insns, clo, chi, comp, hints);
                if (!ok && strong) {
                    std::set<int64_t> hit;
                    for (int64_t b = sa; b < sa + 16; b++) {
                        auto w = weak.find(b);
                        if (w != weak.end()) hit.insert(w->second);
                    }
                    if (!hit.empty()) {
                        std::vector<std::pair<int64_t, Comp>> saved;
                        for (int64_t c : hit) {
                            Comp body;
                            for (int64_t a : comps.at(c)) body.push_back({a, insns.at(a)});
                            saved.push_back({c, body});
                        }
                        for (int64_t c : hit) evict(c);
                        ok = component(rom, sa, insns, clo, chi, comp, hints);
                        if (!ok) {
                            for (auto& cb : saved) place(cb.second, cb.first, true);
                            continue;
                        }
                    }
                }
                if (!ok || comp.empty()) continue;
                std::set<int64_t> clash;
                for (auto& ai : comp)
                    for (int64_t b = ai.first; b < ai.first + ai.second.size; b++) {
                        auto c = cov.find(b);
                        if (c != cov.end()) clash.insert(c->second);
                    }
                if (!clash.empty()) {
                    std::set<int64_t> owners;
                    bool none = false;
                    for (int64_t c : clash) {
                        auto w = weak.find(c);
                        if (w == weak.end()) none = true;
                        else owners.insert(w->second);
                    }
                    if (strong && !none) {
                        for (int64_t c : owners)
                            if (comps.count(c)) evict(c);
                    } else {
                        continue;
                    }
                }
                place(comp, sa, !strong);
                labels.emplace(sa, std::string(callers.count(sa) ? "sub" : "swp") + "_" + hexs(sa, 6));
                accepted++;
            }
            if (!accepted) break;
        }
    }
    return int64_t(insns.size()) - int64_t(n0);
}

Hints read_hints(const std::string& path) {
    Bytes raw = read_bytes(path);
    Hints h;
    int ln = 0;
    for (auto& line0 : splitlines(std::string(raw.begin(), raw.end()))) {
        ln++;
        std::string line = line0.substr(0, std::min(line0.find('#'), line0.find(';')));
        auto w = split_ws(line);
        if (w.empty()) continue;
        auto num = [&](const std::string& t) {
            try {
                std::string x = lower(t);
                if (starts_with(x, "0x")) x = x.substr(2);
                size_t used = 0;
                int64_t v = int64_t(std::stoll(x, &used, 16));
                if (used != x.size()) throw std::invalid_argument(t);
                return v;
            } catch (std::exception&) {
                throw std::runtime_error(path + ":" + std::to_string(ln) + ": bad hex address " + py_repr(t));
            }
        };
        if (w[0] == "code" && (w.size() == 2 || w.size() == 3)) {
            int64_t a = num(w[1]);
            h.code.push_back({a, w.size() == 3 ? w[2] : "code_" + hexs(a, 6)});
        } else if (w[0] == "data" && w.size() == 3) {
            int64_t a = num(w[1]), b = num(w[2]);
            if (b <= a) throw std::runtime_error(path + ":" + std::to_string(ln) + ": data range ends before it starts");
            h.data.push_back({a, b});
        } else {
            throw std::runtime_error(path + ":" + std::to_string(ln) + ": expected `code ADDR [NAME]` or `data START END`");
        }
    }
    return h;
}

void discover(const Rom& rom, const std::string& executed_path, bool do_sweep, InsnMap& insns, Labels& labels,
              DiscoverStats& st, const Hints* hints) {
    RomHeader h = rom.header();
    trace(rom, {{h.entry, "start"}}, insns, labels);
    st.static_ = int64_t(insns.size());
    if (!executed_path.empty()) {
        Bytes raw = read_bytes(executed_path);
        std::vector<int64_t> ex;
        for (auto& w : split_ws(std::string(raw.begin(), raw.end()))) {
            std::string t = lower(w);
            if (starts_with(t, "0x")) t = t.substr(2);
            ex.push_back(int64_t(std::stoll(t, nullptr, 16)));
        }
        std::sort(ex.begin(), ex.end());
        std::vector<std::pair<int64_t, std::string>> entries;
        for (int64_t x : ex)
            if (!insns.has(x)) entries.push_back({x, "dyn_" + hexs(x, 6)});
        trace(rom, entries, insns, labels);
        st.dynamic = int64_t(insns.size()) - st.static_;
    }
    if (hints && !hints->code.empty()) {
        std::vector<std::pair<int64_t, std::string>> entries;
        for (auto& c : hints->code)
            if (!insns.has(c.first)) entries.push_back(c);
        trace(rom, entries, insns, labels);
    }
    if (do_sweep) st.sweep = sweep(rom, insns, labels, 30, hints);
    // drop dyn_/swp_ labels that straight-line code already reaches
    std::vector<int64_t> drop;
    for (auto& kv : labels)
        if (starts_with(kv.second, "dyn_") || starts_with(kv.second, "swp_")) drop.push_back(kv.first);
    for (int64_t x : drop) {
        for (int64_t p = x - 1; p > x - 12; p--) {
            const Insn* pi = insns.get(p);
            if (!pi) continue;
            if (p + pi->size == x && !pi->ends_block) labels.erase(x);
            break;
        }
    }
}

}  // namespace unitron
