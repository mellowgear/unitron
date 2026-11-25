// NGPC ROM code discovery: recursive trace from the entry point, plus an optional gap sweep.
// The `executed` input (optional) is a plain list of hex code addresses from any source (a log,
// a coverage file); unitron itself never runs code.
#pragma once
#include <list>
#include <map>
#include <string>
#include <unordered_map>

#include "decode.hpp"
#include "rom.hpp"

namespace unitron {

// Instructions by address, in insertion order (a Python dict).
class InsnMap {
public:
    bool has(int64_t a) const { return index_.count(a) > 0; }
    Insn& at(int64_t a) { return index_.at(a)->second; }
    const Insn& at(int64_t a) const { return index_.at(a)->second; }
    const Insn* get(int64_t a) const {
        auto it = index_.find(a);
        return it == index_.end() ? nullptr : &it->second->second;
    }
    void set(int64_t a, const Insn& i);          // update in place or append
    Insn pop(int64_t a);
    size_t size() const { return order_.size(); }
    bool empty() const { return order_.empty(); }
    using List = std::list<std::pair<int64_t, Insn>>;
    List::const_iterator begin() const { return order_.begin(); }
    List::const_iterator end() const { return order_.end(); }
    List::iterator begin() { return order_.begin(); }
    List::iterator end() { return order_.end(); }

private:
    List order_;
    std::unordered_map<int64_t, List::iterator> index_;
};

struct Hints;
using Labels = std::map<int64_t, std::string>;
using Regs = std::map<std::string, int64_t>;

// {vector address: NAME} for the interrupt vector slots
const std::map<int64_t, std::string>& VECTORS();

// Track RW3 loads; comment `swi 1` with the BIOS vector number it selects (SNK SysCall).
void annotate_bios(Insn& ins, Regs& regs);
// Comment, in address order, the `swi 1` that have no comment yet (swept code) and the calls of
// a BIOS-call subroutine, with the RW3 that straight-line code before them loads.
void annotate_bios_linear(InsnMap& insns, const Labels& labels);
// Recursive-descent disassembly into insns/labels.
void trace(const Rom& rom, const std::vector<std::pair<int64_t, std::string>>& entries, InsnMap& insns, Labels& labels);
// Fill gaps in the code region with code that decodes as plausible functions; returns the count added.
int64_t sweep(const Rom& rom, InsnMap& insns, Labels& labels, int rounds = 30, const Hints* hints = nullptr);

// A hints file pins what the heuristic sweep may not decide on its own (one record per line;
// '#' or ';' starts a comment; addresses in hex):
//   code ADDR [NAME]    an extra trace entry point (like --executed), labeled NAME or code_ADDR
//   data START END      bytes START..END-1 are data: the sweep never puts code there
//   pointers START END  a table of 4-byte addresses (END-START a multiple of 4): data, written
//                       as `dl`, and every address in the ROM is a trace entry (code_ADDR)
struct Hints {
    std::vector<std::pair<int64_t, std::string>> code;
    std::vector<std::pair<int64_t, int64_t>> data, pointers;
    bool is_data(int64_t a) const {
        for (auto* v : {&data, &pointers})
            for (auto& r : *v)
                if (r.first <= a && a < r.second) return true;
        return false;
    }
    bool is_pointer(int64_t a) const {   // a is the first byte of a word in a pointers range
        for (auto& r : pointers)
            if (r.first <= a && a < r.second && (a - r.first) % 4 == 0) return true;
        return false;
    }
};
Hints read_hints(const std::string& path);   // throws std::runtime_error("FILE:LINE: ...")
// An --executed file: hex addresses (0x optional) separated by white space. A bad one is an
// error "FILE:LINE: bad hex address 'X'", as in a hints file.
std::vector<int64_t> read_executed(const std::string& path);

struct DiscoverStats {
    int64_t static_ = 0, dynamic = -1, sweep = -1;   // -1: not run
};
// Static trace + optional executed-address list + optional gap sweep.
void discover(const Rom& rom, const std::string& executed_path, bool do_sweep, InsnMap& insns, Labels& labels,
              DiscoverStats& st, const Hints* hints = nullptr);

}  // namespace unitron
