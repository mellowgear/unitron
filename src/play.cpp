// unitron play: see play.hpp.
#include "play.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "decode.hpp"
#include "embedded.hpp"
#include "hash.hpp"
#include "ngpc_core.h"
#include "png.hpp"
#include "util.hpp"

namespace unitron::play {

namespace {

constexpr uint32_t JOY = 0x00B0;           // where the HLE BIOS takes the pad byte from
constexpr uint32_t MEMLEN = 0xC000;        // 0x000000-0x00BFFF: I/O, work RAM, Z80 RAM, video
constexpr uint32_t ROMBASE = 0x200000;
constexpr uint32_t FRAME_BUDGET = 200000;  // instructions per frame: a runaway backstop

bool status_ok(int s) {
    return s == NGPC_OK || s == NGPC_HALTED || s == NGPC_COUNT_REACHED || s == NGPC_BREAKPOINT;
}

// Games that check they booted from SNK's BIOS by finding a piece of its boot-time char RAM.
// The HLE BIOS can't leave SNK's bytes there, so they are copied from the game's own copy in
// the cartridge (NgpCraft core/bios_fingerprint.py; only facts are stored here, no data).
struct Fingerprint {
    const char* title;   // 16 bytes at 0x24, NUL-padded
    uint32_t src, len, dest;
};
const Fingerprint FINGERPRINTS[] = {
    {"METALSLUG2ND\0\0\0\0", 0x08DCC4, 64, 0x00A1C0},
};

void restore_fingerprint(Machine& m, const Bytes& rom) {
    if (rom.size() < 0x40) return;
    for (const auto& fp : FINGERPRINTS) {
        if (std::memcmp(&rom[0x24], fp.title, 16) != 0 || rom.size() < fp.src + fp.len) continue;
        Bytes want(rom.begin() + fp.src, rom.begin() + fp.src + fp.len);
        Bytes chr = m.read(0xA000, 0x2000);
        if (std::search(chr.begin(), chr.end(), want.begin(), want.end()) != chr.end()) return;
        m.write(fp.dest, want);
        return;
    }
}

void put_u32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t get_u32(const Bytes& b, size_t& at) {
    if (at + 4 > b.size()) throw std::runtime_error("state file: truncated");
    uint32_t v = b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<uint32_t>(b[at + 3]) << 24;
    at += 4;
    return v;
}
void put_blob(Bytes& b, const void* p, size_t n) {
    put_u32(b, static_cast<uint32_t>(n));
    auto c = static_cast<const uint8_t*>(p);
    b.insert(b.end(), c, c + n);
}
Bytes get_blob(const Bytes& b, size_t& at) {
    uint32_t n = get_u32(b, at);
    if (at + n > b.size()) throw std::runtime_error("state file: truncated");
    Bytes r(b.begin() + at, b.begin() + at + n);
    at += n;
    return r;
}
template <class T>
T get_struct(const Bytes& b, size_t& at, const char* what) {
    Bytes r = get_blob(b, at);
    if (r.size() != sizeof(T))
        throw std::runtime_error(std::string("state file: ") + what + " block is from another core build");
    T t;
    std::memcpy(&t, r.data(), sizeof t);
    return t;
}

const char MAGIC[8] = {'U', 'N', 'P', 'L', 'A', 'Y', 0, 1};

}  // namespace

Machine::Machine(const Bytes& rom, Lang lang) : rom_(rom), lang_(lang) {
    h_ = ngpc_create();
    if (!h_) throw std::runtime_error("play: the emulator core could not start");
    if (ngpc_load_rom(h_, rom.data(), rom.size()) != 0) throw std::runtime_error("play: the core rejected the ROM");
    const std::string& bios = embedded_hle_bios();
    if (ngpc_load_bios(h_, reinterpret_cast<const uint8_t*>(bios.data()), bios.size()) != 0)
        throw std::runtime_error("play: the core rejected the built-in BIOS");
    ngpc_set_language(h_, static_cast<uint32_t>(lang));
    ngpc_reset(h_, NGPC_RESET_HANDOFF);
    ngpc_set_timing_silicon(h_, 10, 8);
    restore_fingerprint(*this, rom_);
    ngpc_set_coverage(h_, 1);
}

Machine::~Machine() {
    if (h_) ngpc_destroy(h_);
}

void Machine::step(uint8_t mask, uint32_t frames) {
    uint8_t pad = mask & 0x7F;
    for (uint32_t i = 0; i < frames; i++) {
        ngpc_write_mem(h_, JOY, &pad, 1);
        if (trace_) {
            uint32_t pc = 0;
            int st = traced_frame_pc(pc);
            if (!status_ok(st)) faults_.push_back({frame(), st, pc});
            trace_->frames++;
        } else {
            ngpc_summary_t s{};
            ngpc_run_frames(h_, 1, FRAME_BUDGET, &s);
            if (!status_ok(s.stop_status)) faults_.push_back({frame(), s.stop_status, s.stop_pc});
        }
        if (!log_.empty() && log_.back().first == mask) log_.back().second++;
        else log_.push_back({mask, 1});
    }
}

// One frame with per-instruction records. The slices are sized exactly as ngpc_run_frames sizes
// them (from the cycles left in the frame, so the run stops on the frame boundary), which keeps a
// traced run the same game as an untraced one.
int Machine::traced_frame_pc(uint32_t& stop_pc) {
    static constexpr uint32_t CYCLES_PER_FRAME = 515 * 199;   // NgpCraft machine.hpp
    static thread_local std::vector<ngpc_record_t> recs(4096);
    ngpc_summary_t s{};
    ngpc_run(h_, 0, nullptr, 0, &s);                          // where the raster is
    const uint32_t target = s.frame_count + 1;
    uint32_t budget = FRAME_BUDGET;
    Trace& t = *trace_;
    const uint32_t romsize = static_cast<uint32_t>(t.reader.size());
    while (budget > 0) {
        uint32_t done = s.scanline * 515 + s.timer_hblank_cycles;
        uint32_t left = done < CYCLES_PER_FRAME ? CYCLES_PER_FRAME - done : 1;
        uint32_t chunk = std::max<uint32_t>(left / 40, 1);
        chunk = std::min({chunk, 4096u, budget});
        ngpc_run(h_, chunk, recs.data(), chunk, &s);
        budget -= s.executed;
        for (uint32_t i = 0; i < s.emitted; i++) {
            const ngpc_record_t& r = recs[i];
            for (uint32_t k = 0; k < r.n_reads && k < NGPC_MAX_ACCESS; k++) {
                const ngpc_access_t& a = r.reads[k];
                for (uint32_t b = 0; b < a.size; b++) {
                    uint32_t off = a.address + b - ROMBASE;
                    if (a.address + b < ROMBASE || off >= romsize) continue;
                    if (!t.reader[off]) t.reader[off] = r.pc;
                    if (t.nread[off] < 0xFFFF) t.nread[off]++;
                }
                if (a.size == 4 && a.address >= ROMBASE && a.address - ROMBASE < romsize) {
                    uint32_t v = (a.data[0] | a.data[1] << 8 | a.data[2] << 16) & 0xFFFFFF;  // the CPU uses 24 bits
                    if (v >= ROMBASE && v - ROMBASE < romsize) t.pointer_reads.emplace(a.address, v);
                }
            }
            for (uint32_t k = 0; k < r.n_reads && k < NGPC_MAX_ACCESS; k++) {
                const ngpc_access_t& a = r.reads[k];
                if (a.address >= 0x4000 && a.address < 0x7000) t.ram_reads[uint64_t{a.address} << 32 | r.pc]++;
            }
            for (uint32_t k = 0; k < r.n_writes && k < NGPC_MAX_ACCESS; k++) {
                const ngpc_access_t& a = r.writes[k];
                if (a.address >= 0x4000 && a.address < 0x7000) t.ram_writes[uint64_t{a.address} << 32 | r.pc]++;
            }
            // indirect jump/call: decode each PC once (again if the bytes there changed)
            auto it = decoded_.find(r.pc);
            if (it == decoded_.end() || it->second.len != r.raw_len || std::memcmp(it->second.raw, r.raw, r.raw_len) != 0) {
                Decoded d{};
                d.len = std::min<uint8_t>(r.raw_len, 8);
                std::memcpy(d.raw, r.raw, d.len);
                Insn ins = decode_bytes(Bytes(r.raw, r.raw + d.len), r.pc);
                d.size = static_cast<uint8_t>(ins.size);
                d.indirect = !ins.invalid && (ins.is_call || ins.is_jump) && !ins.has_target && ins.mnem != "ret" &&
                             ins.mnem != "reti" && ins.mnem != "retd" && ins.mnem != "djnz";
                it = decoded_.insert_or_assign(r.pc, d).first;
            }
            if (it->second.indirect && r.next_pc != r.pc + it->second.size) t.indirect[r.pc][r.next_pc]++;
        }
        if (s.stop_status != NGPC_COUNT_REACHED) {
            stop_pc = s.stop_pc;
            return s.stop_status;
        }
        if (s.executed == 0 || s.frame_count >= target) break;
    }
    return NGPC_COUNT_REACHED;
}

void Machine::seq(const std::string& tokens) {
    for (auto& [mask, n] : parse_seq(tokens)) step(mask, n);
}

uint64_t Machine::frame() const {
    uint64_t f = 0;
    for (auto& r : log_) f += r.second;
    return f;
}

Bytes Machine::read(uint32_t addr, uint32_t n) const {
    Bytes b(n);
    ngpc_read_mem(h_, addr, b.data(), n);
    return b;
}

void Machine::write(uint32_t addr, const Bytes& data) {
    ngpc_write_mem(h_, addr, data.data(), static_cast<uint32_t>(data.size()));
}

std::vector<uint16_t> Machine::framebuffer() const {
    std::vector<uint16_t> fb(NGPC_SCREEN_W * NGPC_SCREEN_H);
    fb.resize(ngpc_get_framebuffer(h_, fb.data(), static_cast<uint32_t>(fb.size())));
    return fb;
}

Bytes Machine::coverage() const {
    uint32_t n = ngpc_get_coverage(h_, nullptr, 0);
    Bytes b(n);
    if (n) b.resize(ngpc_get_coverage(h_, b.data(), n));
    return b;
}

// State file: MAGIC, then blobs (u32 length + bytes): a JSON header (name, parent, ROM sha1,
// language, frame, input log, faults), the core's RTC, CPU, aux (sound CPU/chip, timers, IRQs)
// and link-cable blocks, memory 0x000000-0x00BFFF, then the cartridge flash bytes the game has
// changed (count, then address + blob each). Restored in the order the core needs: RTC, memory,
// CPU, aux (after memory: writing memory pokes 0x00BA), link, flash.
void Machine::save(const std::string& path, const std::string& name, const std::string& parent) const {
    nlohmann::json j;
    j["name"] = name;
    j["parent"] = parent;
    j["rom_sha1"] = sha1_hex(rom_);
    j["lang"] = lang_ == Lang::English ? "en" : "ja";
    j["frame"] = frame();
    nlohmann::json lg = nlohmann::json::array();
    for (auto& r : log_) lg.push_back({r.first, r.second});
    j["inputs"] = lg;
    nlohmann::json fl = nlohmann::json::array();
    for (auto& f : faults_) fl.push_back({f.frame, f.status, f.pc});
    j["faults"] = fl;
    std::string hdr = j.dump();

    Bytes out(MAGIC, MAGIC + 8);
    put_blob(out, hdr.data(), hdr.size());
    ngpc_rtc_t rtc;
    ngpc_get_rtc(h_, &rtc);
    put_blob(out, &rtc, sizeof rtc);
    ngpc_cpu_t cpu;
    ngpc_get_cpu(h_, &cpu);
    put_blob(out, &cpu, sizeof cpu);
    ngpc_aux_state_t aux;
    ngpc_get_aux_state(h_, &aux);
    put_blob(out, &aux, sizeof aux);
    ngpc_link_state_t link;
    ngpc_get_link_state(h_, &link);
    put_blob(out, &link, sizeof link);
    Bytes mem = read(0, MEMLEN);
    put_blob(out, mem.data(), mem.size());
    // flash: runs of changed bytes (gaps of up to 16 unchanged bytes joined)
    Bytes cur = read(ROMBASE, static_cast<uint32_t>(rom_.size()));
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    for (uint32_t i = 0; i < cur.size(); i++) {
        if (cur[i] == rom_[i]) continue;
        if (!runs.empty() && i - runs.back().second <= 16) runs.back().second = i;
        else runs.push_back({i, i});
    }
    put_u32(out, static_cast<uint32_t>(runs.size()));
    for (auto& [a, b] : runs) {
        put_u32(out, ROMBASE + a);
        put_blob(out, &cur[a], b - a + 1);
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("play: can't write " + path);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

namespace {
nlohmann::json read_header(const Bytes& b, size_t& at, const std::string& path) {
    if (b.size() < 8 || std::memcmp(b.data(), MAGIC, 8) != 0)
        throw std::runtime_error("play: " + path + " is not a unitron play state");
    at = 8;
    Bytes h = get_blob(b, at);
    return nlohmann::json::parse(std::string(h.begin(), h.end()));
}
}  // namespace

std::string Machine::state_parent(const std::string& path) {
    Bytes b = read_bytes(path);
    size_t at = 0;
    return read_header(b, at, path).value("parent", "");
}

Machine* load_state(const Bytes& rom, const std::string& path) {
    Bytes b = read_bytes(path);
    size_t at = 0;
    nlohmann::json j = read_header(b, at, path);
    if (j.value("rom_sha1", "") != sha1_hex(rom))
        throw std::runtime_error("play: " + path + " was saved from a different ROM");
    Lang lang = j.value("lang", "en") == "ja" ? Lang::Japanese : Lang::English;
    auto* m = new Machine(rom, lang);
    try {
        auto rtc = get_struct<ngpc_rtc_t>(b, at, "RTC");
        auto cpu = get_struct<ngpc_cpu_t>(b, at, "CPU");
        auto aux = get_struct<ngpc_aux_state_t>(b, at, "aux");
        auto link = get_struct<ngpc_link_state_t>(b, at, "link");
        Bytes mem = get_blob(b, at);
        ngpc_set_rtc(m->h_, &rtc);
        m->write(0, mem);
        ngpc_set_cpu(m->h_, &cpu);
        if (ngpc_set_aux_state(m->h_, &aux) != 0) throw std::runtime_error("play: aux block is from another core build");
        if (ngpc_set_link_state(m->h_, &link) != 0) throw std::runtime_error("play: link block is from another core build");
        uint32_t nruns = get_u32(b, at);
        for (uint32_t i = 0; i < nruns; i++) {
            uint32_t a = get_u32(b, at);
            m->write(a, get_blob(b, at));
        }
        for (auto& r : j.at("inputs")) m->log_.push_back({r[0].get<uint8_t>(), r[1].get<uint32_t>()});
        for (auto& f : j.at("faults")) m->faults_.push_back({f[0].get<uint64_t>(), f[1].get<int>(), f[2].get<uint32_t>()});
    } catch (...) {
        delete m;
        throw;
    }
    return m;
}

// ---- Trace

void Trace::init(const Bytes& rom) {
    rom_sha1 = sha1_hex(rom);
    frames = 0;
    reader.assign(rom.size(), 0);
    nread.assign(rom.size(), 0);
    indirect.clear();
    pointer_reads.clear();
    ram_reads.clear();
    ram_writes.clear();
}

void Trace::merge(const Trace& o) {
    if (o.rom_sha1 != rom_sha1 || o.reader.size() != reader.size()) throw std::runtime_error("trace: merging traces of different ROMs");
    frames += o.frames;
    for (size_t i = 0; i < reader.size(); i++) {
        if (!reader[i]) reader[i] = o.reader[i];
        nread[i] = static_cast<uint16_t>(std::min<uint32_t>(0xFFFFu, nread[i] + o.nread[i]));
    }
    for (auto& [site, ts] : o.indirect)
        for (auto& [to, n] : ts) indirect[site][to] += n;
    for (auto& kv : o.pointer_reads) pointer_reads.insert(kv);
    for (auto& [k, n] : o.ram_reads) ram_reads[k] += n;
    for (auto& [k, n] : o.ram_writes) ram_writes[k] += n;
}

// trace.bin: "UNTRACE" 1, then u32 blobs: JSON header {rom_sha1, rom_size, frames}, reader
// (u32 per ROM byte), nread (u16 per ROM byte), indirect (u32 site, u32 count, then count x
// (u32 target, u32 times)), pointer_reads (u32 address, u32 value pairs).
// Version 2 adds ram_reads and ram_writes: (u32 address, u32 PC, u32 count) triples.
static const char TMAGIC[8] = {'U', 'N', 'T', 'R', 'A', 'C', 'E', 2};

void Trace::save(const std::string& path) const {
    nlohmann::json j{{"rom_sha1", rom_sha1}, {"rom_size", reader.size()}, {"frames", frames}};
    std::string h = j.dump();
    Bytes out(TMAGIC, TMAGIC + 8);
    put_blob(out, h.data(), h.size());
    Bytes b;
    for (uint32_t v : reader) put_u32(b, v);
    put_blob(out, b.data(), b.size());
    b.clear();
    for (uint16_t v : nread) b.push_back(static_cast<uint8_t>(v)), b.push_back(static_cast<uint8_t>(v >> 8));
    put_blob(out, b.data(), b.size());
    b.clear();
    for (auto& [site, ts] : indirect) {
        put_u32(b, site);
        put_u32(b, static_cast<uint32_t>(ts.size()));
        for (auto& [to, n] : ts) put_u32(b, to), put_u32(b, n);
    }
    put_blob(out, b.data(), b.size());
    b.clear();
    for (auto& [a, v] : pointer_reads) put_u32(b, a), put_u32(b, v);
    put_blob(out, b.data(), b.size());
    for (auto* m : {&ram_reads, &ram_writes}) {
        std::map<uint64_t, uint32_t> sorted(m->begin(), m->end());   // stable files
        b.clear();
        for (auto& [k, n] : sorted) put_u32(b, static_cast<uint32_t>(k >> 32)), put_u32(b, static_cast<uint32_t>(k)), put_u32(b, n);
        put_blob(out, b.data(), b.size());
    }
    write_bytes(path, out);
}

bool Trace::load(const std::string& path, const Bytes& rom) {
    Bytes b;
    if (!read_bytes_opt(path, b)) return false;
    if (b.size() < 8 || std::memcmp(b.data(), TMAGIC, 8) != 0) throw std::runtime_error("play: " + path + " is not a unitron play trace");
    size_t at = 8;
    Bytes h = get_blob(b, at);
    auto j = nlohmann::json::parse(std::string(h.begin(), h.end()));
    if (j.value("rom_sha1", "") != sha1_hex(rom)) throw std::runtime_error("play: " + path + " is the trace of a different ROM");
    init(rom);
    frames = j.value("frames", uint64_t{0});
    Bytes r = get_blob(b, at), n = get_blob(b, at), ind = get_blob(b, at), pr = get_blob(b, at);
    if (r.size() != rom.size() * 4 || n.size() != rom.size() * 2) throw std::runtime_error("play: " + path + ": bad sizes");
    for (size_t i = 0; i < rom.size(); i++) {
        reader[i] = r[4 * i] | r[4 * i + 1] << 8 | r[4 * i + 2] << 16 | static_cast<uint32_t>(r[4 * i + 3]) << 24;
        nread[i] = static_cast<uint16_t>(n[2 * i] | n[2 * i + 1] << 8);
    }
    size_t k = 0;
    while (k < ind.size()) {
        uint32_t site = get_u32(ind, k), c = get_u32(ind, k);
        for (uint32_t q = 0; q < c; q++) {
            uint32_t to = get_u32(ind, k);
            indirect[site][to] = get_u32(ind, k);
        }
    }
    k = 0;
    while (k < pr.size()) {
        uint32_t a = get_u32(pr, k);
        pointer_reads[a] = get_u32(pr, k);
    }
    for (auto* m : {&ram_reads, &ram_writes}) {
        Bytes x = get_blob(b, at);
        k = 0;
        while (k < x.size()) {
            uint64_t a = get_u32(x, k), pc = get_u32(x, k);
            (*m)[a << 32 | pc] = get_u32(x, k);
        }
    }
    return true;
}

namespace {
// Executed instruction starts (from coverage) and the bytes each one covers.
struct Executed {
    std::vector<uint32_t> starts;   // ROM addresses
    std::vector<bool> code;         // per ROM byte: inside an executed instruction
};
Executed executed_code(const Bytes& rom, const Bytes& coverage) {
    Executed e;
    e.code.assign(rom.size(), false);
    auto mem = [&](int64_t a) -> int { return a >= ROMBASE && a - ROMBASE < static_cast<int64_t>(rom.size()) ? rom[a - ROMBASE] : -1; };
    for (size_t i = 0; i < coverage.size(); i++)
        for (int b = 0; b < 8; b++)
            if (coverage[i] >> b & 1) {
                uint32_t off = static_cast<uint32_t>(i * 8 + b);
                if (off >= rom.size()) continue;
                e.starts.push_back(ROMBASE + off);
                Insn ins = decode(mem, ROMBASE + off);
                for (int q = 0; q < std::max(ins.size, 1) && off + q < rom.size(); q++) e.code[off + q] = true;
            }
    return e;
}
std::string h6(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%06X", v);
    return b;
}
}  // namespace

std::string make_hints(const Bytes& rom, const Trace& t, const Bytes& coverage, std::string* executed) {
    Executed e = executed_code(rom, coverage);
    std::set<uint32_t> starts(e.starts.begin(), e.starts.end());
    auto in_rom = [&](uint32_t a) { return a >= ROMBASE && a - ROMBASE < rom.size(); };
    std::string out = "# hints from unitron play (" + std::to_string(t.frames) + " frames traced)\n";

    // code: indirect targets in the ROM that executed
    std::map<uint32_t, std::vector<uint32_t>> targets;   // target -> sites
    for (auto& [site, ts] : t.indirect)
        for (auto& kv : ts)
            if (in_rom(kv.first) && starts.count(kv.first)) targets[kv.first].push_back(site);
    out += "\n# code reached by indirect jumps and calls\n";
    for (auto& [to, sites] : targets) {
        out += "code " + h6(to) + "\t; from";
        for (size_t i = 0; i < sites.size() && i < 4; i++) out += " " + h6(sites[i]);
        if (sites.size() > 4) out += " (+" + std::to_string(sites.size() - 4) + ")";
        out += "\n";
    }

    // pointers: runs (stride 4) of 4-byte reads whose values are executed code
    out += "\n# tables of code addresses (read as 4-byte values that are executed code)\n";
    std::vector<uint32_t> ptrs;
    for (auto& [a, v] : t.pointer_reads)
        if (starts.count(v) && !e.code[a - ROMBASE]) ptrs.push_back(a);
    for (size_t i = 0; i < ptrs.size();) {
        size_t j = i;
        while (j + 1 < ptrs.size() && ptrs[j + 1] == ptrs[j] + 4) j++;
        out += "pointers " + h6(ptrs[i]) + " " + h6(ptrs[j] + 4) + "\t; " + std::to_string(j - i + 1) + " seen\n";
        i = j + 1;
    }

    // data: read as data, never executed (gaps of up to 16 unread, unexecuted bytes joined)
    out += "\n# read as data and never executed\n";
    size_t n = rom.size(), i = 0;
    while (i < n) {
        if (!t.nread[i] || e.code[i]) {
            i++;
            continue;
        }
        size_t j = i + 1, last = i;
        while (j < n && !e.code[j] && j - last <= 16) {
            if (t.nread[j]) last = j;
            j++;
        }
        out += "data " + h6(static_cast<uint32_t>(ROMBASE + i)) + " " + h6(static_cast<uint32_t>(ROMBASE + last + 1)) + "\n";
        i = last + 1;
    }
    if (executed) {
        executed->clear();
        for (uint32_t a : e.starts) *executed += h6(a) + "\n";
    }
    return out;
}

std::string trace_report(const Bytes& rom, const Trace& t, const Bytes& coverage) {
    Executed e = executed_code(rom, coverage);
    size_t code = 0, data = 0, both = 0;
    for (size_t i = 0; i < rom.size(); i++) {
        code += e.code[i];
        data += t.nread[i] > 0;
        both += e.code[i] && t.nread[i] > 0;
    }
    size_t ntargets = 0;
    for (auto& kv : t.indirect) ntargets += kv.second.size();
    std::string out;
    out += "frames traced       " + std::to_string(t.frames) + "\n";
    out += "executed            " + std::to_string(e.starts.size()) + " instructions, " + std::to_string(code) + " bytes\n";
    out += "read as data        " + std::to_string(data) + " bytes (" + std::to_string(both) + " of them also executed)\n";
    out += "never touched       " + std::to_string(rom.size() - code - (data - both)) + " bytes\n";
    out += "indirect sites      " + std::to_string(t.indirect.size()) + " (" + std::to_string(ntargets) + " targets)\n";
    out += "pointer reads       " + std::to_string(t.pointer_reads.size()) + "\n\n";
    out += "indirect jumps and calls (site: targets, times taken)\n";
    for (auto& [site, ts] : t.indirect) {
        out += "  " + h6(site) + ":";
        size_t k = 0;
        for (auto& [to, c] : ts) {
            if (k++ == 8) {
                out += " ... (" + std::to_string(ts.size()) + " targets)";
                break;
            }
            out += " " + h6(to) + "x" + std::to_string(c);
        }
        out += "\n";
    }
    return out;
}

std::string ram_report(const Trace& t, uint32_t lo, uint32_t hi, const std::map<uint32_t, std::string>& syms) {
    auto where = [&](uint32_t pc) {
        std::string s = h6(pc);
        auto it = syms.upper_bound(pc);
        if (it != syms.begin()) {
            --it;
            s += " " + it->second;
            if (pc != it->first) {
                char b[16];
                std::snprintf(b, sizeof b, "+%X", pc - it->first);
                s += b;
            }
        }
        return s;
    };
    std::map<uint32_t, std::map<uint32_t, std::pair<uint32_t, uint32_t>>> by;   // addr -> pc -> (reads, writes)
    for (auto& [k, n] : t.ram_reads) {
        uint32_t a = static_cast<uint32_t>(k >> 32);
        if (a >= lo && a <= hi) by[a][static_cast<uint32_t>(k)].first += n;
    }
    for (auto& [k, n] : t.ram_writes) {
        uint32_t a = static_cast<uint32_t>(k >> 32);
        if (a >= lo && a <= hi) by[a][static_cast<uint32_t>(k)].second += n;
    }
    std::string out;
    for (auto& [a, pcs] : by) {
        char b[16];
        std::snprintf(b, sizeof b, "%04X", a);
        out += b;
        out += "\n";
        for (auto& [pc, rw] : pcs) {
            out += "  " + where(pc);
            if (rw.second) out += "  writes " + std::to_string(rw.second);
            if (rw.first) out += "  reads " + std::to_string(rw.first);
            out += "\n";
        }
    }
    if (out.empty()) out = "no reads or writes of that range in the trace\n";
    return out;
}

Cond parse_cond(const std::string& s) {
    static const char* ops[] = {"==", "!=", "<=", ">=", "<", ">"};
    for (const char* op : ops) {
        size_t at = s.find(op);
        if (at == std::string::npos) continue;
        Cond c;
        c.text = s;
        c.op = op;
        std::string lhs = s.substr(0, at), rhs = s.substr(at + std::strlen(op));
        size_t dot = lhs.find('.');
        if (dot != std::string::npos) {
            std::string w = lhs.substr(dot + 1);
            c.width = w == "w" ? 2 : w == "l" ? 4 : w == "b" ? 1 : 0;
            if (!c.width) break;
            lhs = lhs.substr(0, dot);
        }
        try {
            c.addr = static_cast<uint32_t>(std::stoul(lhs, nullptr, 16));
            c.value = std::stoll(rhs, nullptr, 0);
        } catch (...) {
            break;
        }
        return c;
    }
    throw std::runtime_error("play: bad condition '" + s + "' (ADDR[.w|.l]OP VALUE, e.g. 4186==0)");
}

bool Cond::test(const Machine& m) const {
    Bytes b = m.read(addr, static_cast<uint32_t>(width));
    int64_t v = 0;
    for (int i = width - 1; i >= 0; i--) v = v << 8 | b[i];
    if (op == "==") return v == value;
    if (op == "!=") return v != value;
    if (op == "<=") return v <= value;
    if (op == ">=") return v >= value;
    if (op == "<") return v < value;
    return v > value;
}

const Cond* play_until(Machine& m, const std::vector<Cond>& conds, const InputLog& each, uint64_t max_frames) {
    if (each.empty()) throw std::runtime_error("play: empty sequence to repeat");
    uint64_t limit = m.frame() + max_frames;
    for (;;) {
        for (auto& c : conds)
            if (c.test(m)) return &c;
        if (m.frame() >= limit) return nullptr;
        for (auto& [mask, n] : each) m.step(mask, n);
    }
}

std::map<uint32_t, std::string> read_names(const std::string& text) {
    std::map<uint32_t, std::string> out;
    std::istringstream in(text);
    std::string line;
    auto num = [](const std::string& s, bool hex_default, uint32_t& v) {
        try {
            size_t used = 0;
            unsigned long x = std::stoul(s, &used, hex_default && s.rfind("0x", 0) != 0 && s.rfind("0X", 0) != 0 ? 16 : 0);
            if (used != s.size()) return false;
            v = static_cast<uint32_t>(x);
            return true;
        } catch (...) {
            return false;
        }
    };
    while (std::getline(in, line)) {
        size_t c = line.find_first_of(";#");
        if (c != std::string::npos) line = line.substr(0, c);
        std::istringstream ls(line);
        std::vector<std::string> w;
        for (std::string x; ls >> x;) w.push_back(x);
        uint32_t v;
        if (w.size() >= 3 && (w[1] == "equ" || w[1] == "EQU") && num(w[2], false, v)) out[v] = w[0];
        else if (w.size() == 2 && num(w[0], true, v)) out[v] = w[1];
    }
    return out;
}

std::string unnamed_report(const Trace& t, const std::map<uint32_t, std::string>& names, const std::map<uint32_t, std::string>& syms) {
    struct Use {
        uint64_t reads = 0, writes = 0;
        std::map<std::string, uint64_t> who;   // routine -> accesses
    };
    auto routine = [&](uint32_t pc) {
        auto it = syms.upper_bound(pc);
        if (it == syms.begin()) return h6(pc);
        --it;
        return it->second;
    };
    std::map<uint32_t, Use> used;
    for (auto& [k, n] : t.ram_reads) {
        Use& u = used[static_cast<uint32_t>(k >> 32)];
        u.reads += n;
        u.who[routine(static_cast<uint32_t>(k))] += n;
    }
    for (auto& [k, n] : t.ram_writes) {
        Use& u = used[static_cast<uint32_t>(k >> 32)];
        u.writes += n;
        u.who[routine(static_cast<uint32_t>(k))] += n;
    }
    size_t n_named = 0, n_near = 0, n_un = 0;
    std::string near_out, un_out;
    auto who_text = [](const std::map<std::string, uint64_t>& who) {
        std::vector<std::pair<uint64_t, std::string>> v;
        for (auto& [r, c] : who) v.push_back({c, r});
        std::sort(v.rbegin(), v.rend());
        std::string s;
        for (size_t i = 0; i < v.size() && i < 4; i++) s += (i ? ", " : "") + v[i].second;
        if (v.size() > 4) s += ", +" + std::to_string(v.size() - 4);
        return s;
    };
    // unnamed runs: consecutive addresses, merged
    uint32_t run_lo = 0, run_hi = 0;
    Use run;
    bool in_run = false;
    auto flush = [&]() {
        if (!in_run) return;
        char b[64];
        if (run_lo == run_hi) std::snprintf(b, sizeof b, "  %04X       ", run_lo);
        else std::snprintf(b, sizeof b, "  %04X-%04X  ", run_lo, run_hi);
        un_out += b;
        un_out += "r " + std::to_string(run.reads) + " w " + std::to_string(run.writes) + "  " + who_text(run.who) + "\n";
        in_run = false;
    };
    for (auto& [a, u] : used) {
        auto it = names.upper_bound(a);
        bool named = false, near = false;
        std::string label;
        if (it != names.begin()) {
            --it;
            if (it->first == a) named = true;
            else if (a - it->first <= 16) {
                near = true;
                char b[16];
                std::snprintf(b, sizeof b, "+%X", a - it->first);
                label = it->second + b;
            }
        }
        if (named) {
            n_named++;
            flush();
            continue;
        }
        if (near) {
            n_near++;
            flush();
            char b[16];
            std::snprintf(b, sizeof b, "  %04X  ", a);
            near_out += b + label + "  r " + std::to_string(u.reads) + " w " + std::to_string(u.writes) + "  " + who_text(u.who) + "\n";
            continue;
        }
        n_un++;
        if (in_run && a == run_hi + 1) {
            run_hi = a;
            run.reads += u.reads;
            run.writes += u.writes;
            for (auto& [r, c] : u.who) run.who[r] += c;
        } else {
            flush();
            in_run = true;
            run_lo = run_hi = a;
            run = u;
        }
    }
    flush();
    std::string out;
    if (!un_out.empty()) out += "unnamed (runs of consecutive addresses; reads, writes, the routines using them)\n" + un_out;
    if (!near_out.empty()) out += "\nnear a name (up to 16 bytes after it: a field or an element?)\n" + near_out;
    out += "\n" + std::to_string(used.size()) + " work RAM addresses used: " + std::to_string(n_named) + " named, " +
           std::to_string(n_near) + " near a name, " + std::to_string(n_un) + " unnamed\n";
    return out;
}

InputLog parse_seq(const std::string& tokens) {
    static const std::map<char, uint8_t> keys = {{'U', PAD_U}, {'D', PAD_D}, {'L', PAD_L}, {'R', PAD_R},
                                                 {'A', PAD_A}, {'B', PAD_B}, {'O', PAD_O}, {'.', 0}};
    InputLog out;
    auto add = [&](uint8_t m, uint32_t n) {
        if (!n) return;
        if (!out.empty() && out.back().first == m) out.back().second += n;
        else out.push_back({m, n});
    };
    size_t i = 0;
    while (i < tokens.size()) {
        while (i < tokens.size() && std::isspace(static_cast<unsigned char>(tokens[i]))) i++;
        size_t j = i;
        while (j < tokens.size() && !std::isspace(static_cast<unsigned char>(tokens[j]))) j++;
        if (i == j) break;
        std::string t = tokens.substr(i, j - i);
        i = j;
        bool digits = t.size() > 1 && std::all_of(t.begin() + 1, t.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        if (digits && t[0] == 'w') {
            add(0, static_cast<uint32_t>(std::stoul(t.substr(1))));
        } else if (digits && std::string("udlrabo").find(t[0]) != std::string::npos) {
            add(keys.at(static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])))), static_cast<uint32_t>(std::stoul(t.substr(1))));
        } else {
            uint8_t m = 0;
            for (char c : t) {
                auto k = keys.find(c);
                if (k == keys.end()) throw std::runtime_error("play: bad input token '" + t + "' (A B O U D L R, wN, aN..rN)");
                m |= k->second;
            }
            add(m, 5);
            add(0, 8);
        }
    }
    return out;
}

namespace {
Rgb to_rgb(uint16_t c) {
    return {static_cast<uint8_t>((c & 0xF) * 17), static_cast<uint8_t>(((c >> 4) & 0xF) * 17),
            static_cast<uint8_t>(((c >> 8) & 0xF) * 17)};
}

// Indexed PNG from 12-bit colours. A screen almost never has more than 256 colours; if one
// does, each extra colour takes the nearest colour already in the palette.
Bytes write_12bit(uint32_t w, uint32_t h, const std::vector<uint16_t>& px) {
    std::vector<Rgb> pal;
    std::map<uint16_t, uint8_t> index;
    Bytes out(px.size());
    for (size_t i = 0; i < px.size(); i++) {
        uint16_t c = px[i] & 0xFFF;
        auto it = index.find(c);
        if (it == index.end()) {
            uint8_t k;
            if (pal.size() < 256) {
                k = static_cast<uint8_t>(pal.size());
                pal.push_back(to_rgb(c));
            } else {
                Rgb want = to_rgb(c);
                int best = 0, bd = 1 << 30;
                for (size_t q = 0; q < pal.size(); q++) {
                    int d = 0;
                    for (int ch = 0; ch < 3; ch++) d += (pal[q][ch] - want[ch]) * (pal[q][ch] - want[ch]);
                    if (d < bd) bd = d, best = static_cast<int>(q);
                }
                k = static_cast<uint8_t>(best);
            }
            it = index.emplace(c, k).first;
        }
        out[i] = it->second;
    }
    if (pal.empty()) pal.push_back({0, 0, 0});
    return png_write_indexed(w, h, out, pal);
}
}  // namespace

std::string tiles_text(const Machine& m) {
    std::string out;
    for (int plane = 1; plane <= 2; plane++) {
        uint32_t map = plane == 1 ? 0x9000 : 0x9800;
        Bytes so = m.read(plane == 1 ? 0x8032 : 0x8034, 2);   // S1SO.H/V or S2SO.H/V
        Bytes vram = m.read(map, 0x800);
        char b[64];
        std::snprintf(b, sizeof b, "plane %d (scroll %d,%d)\n", plane, so[0], so[1]);
        out += b;
        for (int row = 0; row < 19; row++) {
            for (int col = 0; col < 20; col++) {
                int ty = ((so[1] / 8) + row) & 31, tx = ((so[0] / 8) + col) & 31;
                uint16_t e = static_cast<uint16_t>(vram[(ty * 32 + tx) * 2] | vram[(ty * 32 + tx) * 2 + 1] << 8);
                std::snprintf(b, sizeof b, "%s%03X", col ? " " : "", e & 0x1FF);
                out += b;
            }
            out += "\n";
        }
    }
    return out;
}

Bytes screenshot_png(const Machine& m, int scale) {
    if (scale < 1) scale = 1;
    std::vector<uint16_t> fb = m.framebuffer();
    fb.resize(NGPC_SCREEN_W * NGPC_SCREEN_H, 0);
    uint32_t w = NGPC_SCREEN_W * scale, h = NGPC_SCREEN_H * scale;
    std::vector<uint16_t> big(static_cast<size_t>(w) * h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) big[y * w + x] = fb[(y / scale) * NGPC_SCREEN_W + x / scale];
    return write_12bit(w, h, big);
}

Bytes montage_png(const std::vector<Bytes>& pngs, int cols) {
    if (pngs.empty()) throw std::runtime_error("montage: no images");
    std::vector<PngImage> ims;
    uint32_t cw = 0, ch = 0;
    for (auto& p : pngs) {
        ims.push_back(png_read(p));
        if (ims.back().color_type != 3) throw std::runtime_error("montage: only palette PNGs (unitron play shots)");
        cw = std::max(cw, ims.back().width);
        ch = std::max(ch, ims.back().height);
    }
    uint32_t c = static_cast<uint32_t>(std::min<size_t>(static_cast<size_t>(std::max(cols, 1)), ims.size()));
    uint32_t rows = static_cast<uint32_t>((ims.size() + c - 1) / c);
    uint32_t W = cw * c, H = ch * rows;
    std::vector<uint16_t> px(static_cast<size_t>(W) * H, 0x666);   // grey between images
    for (size_t i = 0; i < ims.size(); i++) {
        auto& im = ims[i];
        uint32_t ox = static_cast<uint32_t>(i % c) * cw, oy = static_cast<uint32_t>(i / c) * ch;
        for (uint32_t y = 0; y < im.height; y++)
            for (uint32_t x = 0; x < im.width; x++) {
                const Rgb& r = im.palette[im.pixels[y * im.width + x]];
                px[(oy + y) * W + ox + x] = static_cast<uint16_t>((r[0] / 17) | (r[1] / 17) << 4 | (r[2] / 17) << 8);
            }
    }
    return write_12bit(W, H, px);
}

}  // namespace unitron::play
