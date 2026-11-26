// unitron play: a headless NGPC (the NgpCraft core, fetched by cmake/deps.cmake) for mapping a game by
// playing it. States carry their parent and the whole input log from power-on, so any state can
// be replayed exactly. docs/play.md has the commands and file formats.
#pragma once
#include <cstdint>
#include <map>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

#include "bytes.hpp"

struct ngpc_machine;   // the core's handle (ngpc_t in ngpc_core.h)

namespace unitron::play {

// The joypad byte the BIOS hands the game (SysWork 0x6F82 is filled from it): bit masks.
enum : uint8_t { PAD_U = 0x01, PAD_D = 0x02, PAD_L = 0x04, PAD_R = 0x08, PAD_A = 0x10, PAD_B = 0x20, PAD_O = 0x40 };

enum class Lang { Japanese = 0, English = 1 };

struct Fault {
    uint64_t frame;
    int status;
    uint32_t pc;
};

// One input run: hold `mask` for `frames` frames. The log is a list of these, merged when equal.
using InputLog = std::vector<std::pair<uint8_t, uint32_t>>;

// What play saw the code do with the ROM, merged over runs (DIR/trace.bin, format in docs/play.md).
// Indexed by ROM offset (address - 0x200000).
struct Trace {
    std::string rom_sha1;
    uint64_t frames = 0;                    // frames traced
    std::vector<uint32_t> reader;           // per ROM byte: first PC that read it as data (0 = never)
    std::vector<uint16_t> nread;            // per ROM byte: data reads (saturating)
    // indirect jumps/calls (jp/call with no fixed target): site PC -> {target -> times taken}
    std::map<uint32_t, std::map<uint32_t, uint32_t>> indirect;
    // 4-byte ROM reads whose value was a ROM address: address read -> value (pointer candidates)
    std::map<uint32_t, uint32_t> pointer_reads;
    // work RAM (0x4000-0x6FFF) accesses: key (address << 32 | PC) -> count
    std::unordered_map<uint64_t, uint32_t> ram_reads, ram_writes;

    void init(const Bytes& rom);            // empty trace for this ROM
    void merge(const Trace& o);
    void save(const std::string& path) const;
    // Load path into *this; false if there is no file. Throws if it is for another ROM.
    bool load(const std::string& path, const Bytes& rom);
};

class Machine {
public:
    // Power on with the built-in HLE BIOS, handing straight to the
    // cartridge: language set first, silicon timing, hand-off reset.
    Machine(const Bytes& rom, Lang lang);
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    void step(uint8_t mask, uint32_t frames = 1);   // hold mask for n frames
    // Record into t while playing (nullptr = off). Traced play is the same game, frame for frame:
    // only what the core reports per instruction is kept.
    void set_trace(Trace* t) { trace_ = t; }
    void press(uint8_t mask, uint32_t hold = 5, uint32_t release = 8) { step(mask, hold); step(0, release); }
    void seq(const std::string& tokens);            // see parse_seq

    uint64_t frame() const;                         // frames since power-on (sum of the log)
    const InputLog& log() const { return log_; }
    const std::vector<Fault>& faults() const { return faults_; }
    Lang lang() const { return lang_; }

    Bytes read(uint32_t addr, uint32_t n) const;
    void write(uint32_t addr, const Bytes& data);
    std::vector<uint16_t> framebuffer() const;      // 160 x 152, 12-bit 0BGR
    Bytes coverage() const;                         // 1 bit per ROM byte executed (instruction starts)

    // States: save to / load from a file (format in play.cpp). load() replaces this machine's state.
    void save(const std::string& path, const std::string& name, const std::string& parent) const;
    static std::string state_parent(const std::string& path);

    friend Machine* load_state(const Bytes& rom, const std::string& path);

private:
    ngpc_machine* h_ = nullptr;
    Bytes rom_;
    Lang lang_;
    InputLog log_;
    std::vector<Fault> faults_;
    Trace* trace_ = nullptr;
    struct Decoded {
        uint8_t raw[8];
        uint8_t len;
        uint8_t size;
        bool indirect;
    };
    std::map<uint32_t, Decoded> decoded_;   // decode cache for the trace, by PC
    int traced_frame_pc(uint32_t& stop_pc); // one frame in records mode; returns the stop status
};

// Load a state file into a new machine (powered on, then the saved state put back).
Machine* load_state(const Bytes& rom, const std::string& path);

// The input token language:
//   A B O U D L R   press (hold 5 frames, release 8); letters combine: UA = up+A together
//   wN              wait N frames, nothing held
//   aN bN oN uN dN lN rN   hold that button N frames (no release)
// Returns (mask, frames) runs. Throws std::runtime_error on a bad token.
InputLog parse_seq(const std::string& tokens);

// A RAM condition for `play run --until`: ADDR[.w|.l]OP VALUE, OP one of == != < > <= >=, ADDR in
// hex, VALUE decimal or 0x hex; .w/.l read 2/4 bytes little-endian (default 1). Example: 4186==0.
struct Cond {
    uint32_t addr = 0;
    int width = 1;
    std::string op;
    int64_t value = 0;
    std::string text;
    bool test(const Machine& m) const;
};
Cond parse_cond(const std::string& s);   // throws std::runtime_error

// Play `each` until one of conds holds or `max_frames` more frames have passed. Returns the
// condition that held, or nullptr.
const Cond* play_until(Machine& m, const std::vector<Cond>& conds, const InputLog& each, uint64_t max_frames);

// Hints for `unitron dis --hints` from a trace and the coverage: `code` for indirect targets,
// `pointers` for 4-byte tables whose entries were read and are executed code, `data` for ROM read
// as data and never executed. Returns the text; if executed is non-null it also gets every
// executed instruction address (for `dis --executed`), one per line.
std::string make_hints(const Bytes& rom, const Trace& t, const Bytes& coverage, std::string* executed = nullptr);

// A short report on a trace: what it covers, the indirect sites and their targets.
std::string trace_report(const Bytes& rom, const Trace& t, const Bytes& coverage);

// Who read and wrote work RAM lo..hi (inclusive), per address: PCs with counts. `syms` (address ->
// name, e.g. from `unitron asm --sym`) shows each PC as name+offset of the nearest symbol below it.
std::string ram_report(const Trace& t, uint32_t lo, uint32_t hi, const std::map<uint32_t, std::string>& syms);

// What is left to name: every work RAM address the trace saw read or written, against `names`
// (address -> name). Named = a name at that address; NAME+n = up to 16 bytes after a name (a field
// or an element, probably); the rest, in runs of consecutive addresses, with the routines that use
// them (`syms` as for ram_report). Ends with a summary line.
std::string unnamed_report(const Trace& t, const std::map<uint32_t, std::string>& names, const std::map<uint32_t, std::string>& syms);

// Read names for unnamed_report / --sym: `NAME equ VALUE` lines (asm900, e.g. a wram.inc) and
// `ADDR NAME` lines (unitron asm --sym), both in one reader; ';' and '#' start comments.
std::map<uint32_t, std::string> read_names(const std::string& text);

// The two scroll planes as text: for each plane, the 20 x 19 cells on screen (its scroll applied),
// each as the 3-digit hex tile number (bits 0-8 of the map entry). A game's own script can map its
// font's tile numbers to characters to read the screen without an image.
std::string tiles_text(const Machine& m);

// Screenshot: the framebuffer as an indexed PNG, scaled by an integer factor.
Bytes screenshot_png(const Machine& m, int scale = 2);

// A contact sheet of PNG screenshots (as written by screenshot_png), `cols` wide.
Bytes montage_png(const std::vector<Bytes>& pngs, int cols = 4);

}  // namespace unitron::play
