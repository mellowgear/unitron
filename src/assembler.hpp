// asm900 assembler and linker in one step: modules -> sections -> a memory image.
// Language: Toshiba TLCS-900 Assembler Reference (tde122-01_96, "TR") chapters 3-5; see
// docs/language.md. Each source file is one module; relocatable sections are placed after all
// modules are sized (docs/language.md "Linking"), absolute sections stay where `abs=` puts them.
// Errors carry Toshiba's numbers where one fits (TR p.96-99).
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "charmap.hpp"
#include "encode.hpp"
#include "mpp.hpp"
#include "where.hpp"

namespace unitron {

// The reserved words of asm900 by kind (upper case), for editors: mnemonics (without B/W/L), registers,
// conditions, control registers, directives, $ controls (without $), other keywords.
std::vector<std::pair<std::string, std::vector<std::string>>> asm900_keywords();

struct AsmError : std::runtime_error {
    int code;
    AsmError(int c, const std::string& m) : std::runtime_error(m), code(c) {}
};

std::string split_comment(const std::string& line);
bool is_reserved(const std::string& name);

struct Module;

// a data value: a number, a string, or an expression for the final pass
struct DVal {
    enum K { Int, Str, Ex } k = Int;
    int64_t v = 0;
    bool big = false;
    std::string decimal;
    Bytes s;
    ExprP e;
};

struct Item {
    enum Kind { Label, Org, Align, Ins, Bin, Data, Fill, Space } kind;
    Where where;
    ExprP ex;              // org target, fill/space count
    int64_t align = 0;
    std::string base;      // instruction
    int sfx = 0;
    std::vector<Operand> ops;
    int w = 0;             // data/fill/space unit
    std::vector<DVal> vals;
    DVal fillv;
    int64_t off = 0, size = 0;
    Bytes data;
    int64_t seq = 0;       // statement number in the module (forward references)
};

struct Section {
    Module* module = nullptr;
    std::string name, kind, attr;   // attr "" = none
    bool has_abs = false;
    int64_t abs = 0;
    int64_t align = 1;
    Where where;
    std::vector<std::unique_ptr<Item>> items;
    int64_t base = 0, size = 0;
    bool has_init = false;    // initialized data (db/dw/df*) in a data section
    Where init_where;
    int64_t load_delta = 0;   // image address - run address (an .lcf org= that differs from addr=)
    bool noload = false;      // an .lcf NOLOAD/DSECT output section: no bytes in the image
    bool overlay = false;     // an .lcf OVERLAY/COPY output section: may overlap others (TR 8.3.5)
    int width() const;
};

struct Sym {
    enum K { Equ, Label, Extern } kind = Equ;
    Where where;
    Section* sec = nullptr;
    Item* item = nullptr;
    ExprP expr;
    int width = 0;
    bool busy = false;
    int64_t seq = 0;       // statement number of the definition (forward references)
};

// insertion-ordered map (Python dict)
template <class V>
struct OMap {
    std::vector<std::pair<std::string, V>> items;
    std::unordered_map<std::string, size_t> index;
    V* get(const std::string& k) {
        auto it = index.find(k);
        return it == index.end() ? nullptr : &items[it->second].second;
    }
    void set(const std::string& k, const V& v) {
        auto it = index.find(k);
        if (it != index.end()) {
            items[it->second].second = v;
            return;
        }
        index[k] = items.size();
        items.push_back({k, v});
    }
    bool setdefault(const std::string& k, const V& v) {
        if (index.count(k)) return false;
        set(k, v);
        return true;
    }
};

struct Module {
    std::string name;
    Where where;
    OMap<Sym> syms;
    std::vector<std::unique_ptr<Section>> sections;
    std::map<std::string, Section*> secmap;
    OMap<Where> pub;
    bool ended = false, maximum = false, named = false;
    int includes = 0;                              // $include count (asm900: 99 per unit)
    bool warned_depth = false, warned_count = false;
};

struct Chunk {
    int64_t addr;
    Bytes b;
    Section* sec;
};

class Assembler {
public:
    Assembler(const std::vector<std::string>& include_dirs = {}, bool strict = false, bool mpp = true,
              const Defines& defines = {}, bool optimize = true);
    void add_module(const std::string& path);
    std::vector<Chunk> assemble(bool link = true, const std::map<std::string, int64_t>& placements = {},
                                int64_t data_base = 0x4000, int64_t code_base = 0x200000, int max_passes = 24);
    std::vector<std::pair<std::string, int64_t>> symbols();   // name -> value, dict order
    // Every symbol defined in the sources, for editors: name, equ or label, where, its section
    // (labels), and its value when it has one after assemble().
    struct SymDef {
        std::string name;
        bool label = false;
        Where where;
        std::string section;
        bool has_value = false;
        int64_t value = 0;
    };
    std::vector<SymDef> symbol_defs();
    // Place relocatable sections with a Toshiba link command file instead of --place/first fit
    // (docs/lcf.md); its assign statements define symbols that externs can name.
    void set_lcf(const std::string& path) { lcf_path_ = path; }
    Diags d;

    // `asm -M`: list every file the assembly reads (the sources, #include and $include files), in
    // the order read, each once. missing_ok (-MG): an include file that can't be found is listed
    // as written and skipped, and `missing` is set. scan_only: add_module reads only what decides
    // the includes (the preprocessor, $ control lines, `end`), so nothing else is checked.
    void track_deps(bool missing_ok, bool scan_only) {
        track_ = true;
        missing_ok_ = missing_ok;
        scan_ = scan_only;
    }
    std::vector<std::string> deps;
    bool missing = false;

private:
    bool track_ = false, missing_ok_ = false, scan_ = false;
    std::set<std::string> dep_seen_;
    void add_dep(const std::string& p) {
        if (track_ && dep_seen_.insert(p).second) deps.push_back(p);
    }
    std::vector<std::string> include_dirs_;
    bool strict_, use_mpp_, optimize_;
    Defines defines_;
    std::vector<std::unique_ptr<Module>> modules_;
    bool final_ = false, link_ = false, publics_ready_ = false;
    std::map<std::string, std::pair<Module*, Where>> publics_;
    std::string lcf_path_;
    std::map<std::string, int64_t> lcf_syms_;
    bool lcf_failed_ = false;
    void place_lcf();
    Section* cur_sec_ = nullptr;
    bool first_ = true;
    int64_t seq_ = 0;     // statements so far (Item::seq, Sym::seq)

    // Text with a character map (`$charmap "file"`): the map's commands (text, line, page, done, ...)
    // assemble as bytes. `slot WIDTH PAD` pads each string (a `text` line up to the next `text`,
    // label, other statement or the end of the file) with PAD to WIDTH bytes; `slot 0` ends it.
    std::unique_ptr<Charmap> charmap_;
    struct Slot {
        size_t width = 0, bytes = 0;
        int pad = 0;
        bool in_str = false;
        Where where;
    } slot_;
    void slot_close();
    void text_statement(const std::string& op, const std::string& line, const Where& where);

    std::vector<SrcLine> lines_of(const std::string& path);
    std::string find_include(const std::string& name, const std::string& cur);
    void run(Module& mod, const std::vector<SrcLine>& lines, const std::string& path, const std::vector<Loc>& chain);
    void statement(Module& mod, const std::string& text, const Where& where, const std::string& path);
    void control(Module& mod, const std::string& name, const std::string& field, const Where& where);
    Section& need_section();
    void define(Module& mod, const std::string& name, const Sym& sym);
    int64_t abs_now(Module& mod, const ExprP& e);
    void section(Module& mod, const std::string& name, const std::string& field, const Where& where);
    void data(const std::string& u, const std::string& field, const Where& where);

    VW lookup(Module& mod, const std::string& name, bool strict = false);
    VW func(Module& mod, const std::string& f, const std::string& name);
    bool layout();
    int64_t count(Module& mod, Section& sec, Item& it);
    VW value(Module& mod, Section& sec, Item& it, const ExprP& e);
    VW ev_strict(Module& mod, Section& sec, Item& it, const ExprP& e);
    int64_t ins_size(Module& mod, Section& sec, Item& it);
    Bytes encode_item(Module& mod, Section& sec, Item& it, bool final);
    void resolve_publics();
    void place(const std::map<std::string, int64_t>& placements, int64_t data_base, int64_t code_base);
    std::vector<Chunk> emit();
    bool item_bytes(Module& mod, Section& sec, Item& it, Bytes& out);
    Bytes pack(int w, const std::vector<DVal>& vals, const Where& where);
};

// Raw image from chunks. Default origin/size: the span of code/romdata bytes.
struct Image {
    Bytes data;
    int64_t origin = 0, size = 0;
};
Image build_image(const std::vector<Chunk>& chunks, const int64_t* origin, const int64_t* size, int fill, Diags* d);

}  // namespace unitron
