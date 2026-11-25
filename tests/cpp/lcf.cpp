// Link command file (.lcf) parser and placement tests: small inline files, written to a temp file.
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "lcf.hpp"
#include "util.hpp"

using namespace unitron;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        failures++;
    }
}

template <class A, class B>
void eq(const A& got, const B& want, const std::string& what) {
    if (!(got == static_cast<A>(want))) {
        std::cerr << "FAIL: " << what << ": got " << got << ", want " << want << "\n";
        failures++;
    }
}

std::string tmp_path() {
    static int n = 0;
    return (std::filesystem::temp_directory_path() / ("unitron_lcf_test_" + std::to_string(n++) + ".lcf")).u8string();
}

Lcf load(const std::string& text) {
    std::string p = tmp_path();
    write_text(p, text);
    Lcf l = Lcf::parse_file(p);
    std::remove(p.c_str());
    return l;
}

// Parse text, expect a std::runtime_error whose message contains want (and ":LINE:").
void parse_error(const std::string& text, const std::string& want, int line) {
    std::string p = tmp_path();
    write_text(p, text);
    try {
        Lcf::parse_file(p);
        check(false, "expected parse error containing '" + want + "'");
    } catch (std::runtime_error& e) {
        std::string m = e.what();
        check(m.find(want) != std::string::npos, "parse error '" + m + "' should contain '" + want + "'");
        check(m.find(p + ":" + std::to_string(line) + ": ") == 0, "parse error '" + m + "' should be at line " + std::to_string(line));
    }
    std::remove(p.c_str());
}

bool has(const std::vector<std::string>& v, const std::string& part) {
    for (auto& s : v)
        if (s.find(part) != std::string::npos) return true;
    return false;
}

void dump(const LcfResult& r) {
    for (auto& e : r.errors) std::cerr << "  error: " << e << "\n";
    for (auto& w : r.warnings) std::cerr << "  warning: " << w << "\n";
}

LcfInput in(size_t id, const std::string& name, const std::string& file, const std::string& kind, int64_t size,
            int64_t align = 1, int width = 24) {
    return LcfInput{id, name, file, kind, size, align, width};
}

int64_t sym(const LcfResult& r, const std::string& name) {
    int64_t v = -1;
    for (auto& s : r.symbols)
        if (s.first == name) v = s.second;
    return v;
}

// A cartridge layout in the style of the NGPC SDK's (own text): RAM data, ROM code and constants
// chained with addr()+sizeof(), initialized data loaded in ROM and run in RAM.
void test_cart() {
    Lcf l = load(R"(/* cartridge */
memory {
    ram (RW) : org=0x4000, len=0x2000   // work RAM
    rom (RX) : origin = 0x200000 length = 0x200000
}
sections {
    lib_ram addr=0x4000 : { *(f_libarea) }
    user_ram addr=0x5000 : { *(f_area) }
    head addr=0x200000 : { *(f_head) }
    code addr=addr(head)+sizeof(head) : { b.rel(f_code) *(f_code) }
    rodata addr=addr(code)+sizeof(code) : { *(f_const) }
    roinit org=org(rodata)+sizeof(rodata)
           addr=org(user_ram)+sizeof(user_ram) : { *(f_data) }
    __DataRAM = addr(roinit);
    __DataROM = org(roinit);
    __DataLen = sizeof(roinit);
}
)");
    std::vector<LcfInput> v = {
        in(0, "f_area", "src/a.asm", "data", 0x10, 2),  in(1, "f_code", "src/a.asm", "code", 0x101),
        in(2, "f_area", "src/b.asm", "data", 3),        in(3, "f_code", "src/b.asm", "code", 0x20, 2),
        in(4, "f_head", "src/a.asm", "romdata", 0x40),  in(5, "f_const", "src/a.asm", "romdata", 5),
        in(6, "f_data", "src/a.asm", "data", 4, 2),     in(7, "f_extra", "src/b.asm", "romdata", 7),
    };
    LcfResult r = l.place(v, {});
    dump(r);
    eq(r.errors.size(), 0, "cart: errors");
    eq(r.placed[0].addr, 0x5000, "cart: a f_area");
    eq(r.placed[2].addr, 0x5010, "cart: b f_area joined after a");
    eq(r.placed[4].addr, 0x200000, "cart: head");
    eq(r.placed[3].addr, 0x200040, "cart: b.rel(f_code) first");
    eq(r.placed[1].addr, 0x200060, "cart: then a f_code");
    eq(r.placed[5].addr, 0x200161, "cart: rodata after code");
    // roinit: load 0x200166 in ROM, run at 0x5013 in RAM; the data (align 2) at run 0x5014
    eq(r.placed[6].addr, 0x5014, "cart: f_data run address");
    eq(r.placed[6].load, 0x200167, "cart: f_data load address");
    eq(r.placed[4].load, 0x200000, "cart: load = addr without org");
    eq(sym(r, "__DataRAM"), 0x5013, "cart: __DataRAM");
    eq(sym(r, "__DataROM"), 0x200166, "cart: __DataROM");
    eq(sym(r, "__DataLen"), 5, "cart: __DataLen");
    // f_extra is not named: own output section after the last allocation (ROM, where '.' is)
    eq(r.placed[7].addr, 0x20016B, "cart: unnamed section linked after the others");
    check(has(r.warnings, "f_extra is not named"), "cart: warning for unnamed section");
    eq(r.outputs.size(), 7u, "cart: outputs");
    check(r.outputs.back().automatic, "cart: automatic output");
    eq(r.outputs[5].memory, std::string("ram"), "cart: roinit runs in ram");
}

// No memory and no sections part: input order, same names joined (TR 8.3.1); abs ranges avoided.
void test_default() {
    Lcf l = load("// nothing but a symbol\nTOP = 0x10 + 2 * 3;\n");
    std::vector<LcfInput> v = {in(0, "a", "x.asm", "code", 0x10), in(1, "b", "x.asm", "data", 4, 4),
                               in(2, "a", "y.asm", "code", 3, 2)};
    LcfResult r = l.place(v, {{0, 0x100}, {0x110, 0x120}});
    dump(r);
    eq(r.errors.size(), 0, "default: errors");
    // a = x's a + y's a (0x13 bytes) does not fit in 0x100..0x110: the joined group goes to 0x120
    eq(r.placed[0].addr, 0x120, "default: a after the abs ranges");
    eq(r.placed[2].addr, 0x130, "default: y's a joined after x's");
    eq(r.placed[1].addr, 0x134, "default: b next, aligned 4");
    eq(sym(r, "TOP"), 0x16, "default: TOP");
    eq(r.warnings.size(), 0, "default: no warnings without sections part");
}

void test_expr_and_dot() {
    Lcf l = load(R"(SECTIONS {
    X = 0y101 + 017 + 10;          /* 5 + 15 + 10 */
    P = 1 + 2 * 3 == 7;
    S = 1 << 2 + 1;
    N = -1;
    C = ~0x0F & 0xFF | 1;
    L = 3 > 2 && 0 || 2 <= 2;
    D = (7 - 1) / 4;
    W = 0x123456789;               /* part beyond 32 bits ignored */
    . = 0x3000;
    one : { *(s1) }
    . += 0x10;
    two Align = 0x100 : { *(s2) }
    Q = align(0x40);
    Q += 1;
    three ADDR = Q + 0x1F : { *(s3) }
    four : { *(s4) }
}
)");
    std::vector<LcfInput> v = {in(0, "s1", "m.asm", "data", 0x21), in(1, "s2", "m.asm", "data", 2),
                               in(2, "s3", "m.asm", "data", 1), in(3, "s4", "m.asm", "data", 1)};
    LcfResult r = l.place(v, {});
    dump(r);
    eq(r.errors.size(), 0, "expr: errors");
    eq(sym(r, "X"), 30, "expr: number bases");
    eq(sym(r, "P"), 1, "expr: precedence * + ==");
    eq(sym(r, "S"), 8, "expr: shift below +");
    eq(sym(r, "N"), 0xFFFFFFFF, "expr: unsigned 32-bit");
    eq(sym(r, "C"), 0xF1, "expr: ~ & |");
    eq(sym(r, "L"), 1, "expr: && ||");
    eq(sym(r, "D"), 1, "expr: / ()");
    eq(sym(r, "W"), 0x23456789, "expr: truncation");
    eq(r.placed[0].addr, 0x3000, "dot: one at .");
    eq(r.placed[1].addr, 0x3100, "dot: two after += and align");
    eq(sym(r, "Q"), 0x3141, "dot: align() then +=");
    eq(r.placed[2].addr, 0x3160, "dot: three");
    eq(r.placed[3].addr, 0x3161, "dot: four after three");
}

void test_errors() {
    Lcf l = load(R"(memory {
    ram(RW) : org=0x1000, len=0x1000
    rom(RX) : org=0x200000, len=0x10000
}
sections {
    a addr=0x1000 : { *(a) }
    b addr=0x1008 : { *(b) }
    c addr=0x300000 : { *(c) }
    d addr=0x1F00 : { *(d) }
    e addr=addr(nosuch) : { *(e) }
    f addr=0x200000 : { *(f) }
    g addr=0x1800 : { *(g) }
    h addr=addr(later) : { *(h) }
    later addr=0x1900 : { }
    . = 0x10;
}
)");
    std::vector<LcfInput> v = {in(0, "a", "m.asm", "data", 0x10), in(1, "b", "m.asm", "data", 2),
                               in(2, "c", "m.asm", "code", 2),    in(3, "d", "m.asm", "data", 0x200),
                               in(4, "e", "m.asm", "data", 1),    in(5, "f", "m.asm", "data", 1),
                               in(6, "g", "m.asm", "data", 1, 1, 8), in(7, "h", "m.asm", "data", 1)};
    LcfResult r = l.place(v, {});
    dump(r);
    auto line = [&](int n, const std::string& part) {
        bool ok = false;
        for (auto& e : r.errors)
            if (e.find(":" + std::to_string(n) + ": ") != std::string::npos && e.find(part) != std::string::npos) ok = true;
        check(ok, "errors: line " + std::to_string(n) + " should report '" + part + "'");
    };
    line(7, "\"b\" overlap");
    line(8, "enters unconfigured memory at 0x300000");
    line(9, "won't fit into memory ram");
    line(10, "no output section named \"nosuch\"");
    line(11, "attributes mismatch");
    line(12, "section g at 0x1800 does not fit its small displacement");
    line(13, "\"later\" not yet allocated");
    line(15, "location counter can't point lower address");
    eq(r.errors.size(), 8, "errors: count");
    check(!r.placed.count(4), "errors: e not placed");
}

void test_syntax() {
    parse_error("memory {\n  ram(RW) : org=0x1000 len=0x100\n}\nsections {\n  a addr= : { *(a) }\n}\n", "expected an expression", 5);
    parse_error("sections {\n  a addr=0x10 { *(a) }\n}\n", "expected org=, addr=", 2);
    parse_error("memory {\n ram(RQ) : org=0, len=1\n}\n", "can't set attributes", 2);
    parse_error("memory {\n a : org=0, len=0x100\n b : org=0x80, len=0x100\n}\n", "MEMORY segment overlap", 3);
    parse_error("/* open\n\n", "unterminated comment", 1);
    parse_error("X = 09;\n", "invalid number", 1);
    parse_error("sections {\n a : { *(a) }\n a : { *(b) }\n}\n", "multiply defined output section", 3);
    parse_error(". = 0x100;\n", "section definition part", 1);
    parse_error("sections {\n a : { *(a) } > nowhere\n}\n", "nowhere is not defined", 2);
    parse_error("X = 1\nY = 2;\n", "expected ';'", 2);
}

void test_predefined_and_attrs() {
    Lcf l = load(R"(MEMORY {
    DATA.S : org=0x10, len=0xF0
    data.m : org=0x10, len=0x3FF0
    code.l (RX) : org=0x200000, len=0x10000
    extra (RW) : org=0x8000, len=0x100
}
Sections {
    big : { *(big) } > extra
    over ORG=0x200000 (OVERLAY) : { *(ov) }
    dummy addr=0x900000 (DSECT) : { *(dm) }
    ab : { *(ab) }
}
)");
    std::vector<LcfInput> v = {in(0, "sm", "m.asm", "data", 0x20, 1, 8),  in(1, "md", "m.asm", "data", 0x10, 2, 16),
                               in(2, "cd", "m.asm", "code", 0x30, 1, 24), in(3, "big", "m.asm", "data", 0x40),
                               in(4, "ov", "m.asm", "code", 4),           in(5, "dm", "m.asm", "data", 8),
                               in(6, "ab", "m.asm", "data", 2)};
    LcfResult r = l.place(v, {{0x200000, 0x200010}});
    dump(r);
    eq(r.errors.size(), 0, "predef: errors");
    eq(r.placed[3].addr, 0x8000, "predef: > extra");
    eq(r.placed[4].addr, 0x200000, "predef: OVERLAY may overlap");
    eq(r.placed[5].addr, 0x900000, "predef: DSECT outside memory");
    // ab (data, large, not named) has no data.l: falls back to data.m/data.s; '.' is not in them
    eq(r.placed[6].addr, 0x10, "predef: ab in data.s start");
    eq(r.placed[0].addr, 0x12, "predef: small data in data.s");
    eq(r.placed[1].addr, 0x32, "predef: medium data after it");
    eq(r.placed[2].addr, 0x200010, "predef: code.l avoids the absolute range");
    eq(r.outputs[1].attr, std::string("OVERLAY"), "predef: attr");
}

void test_org_neq_addr_and_misc() {
    Lcf l = load(R"(memory { ram : org=0, len=0x10000  rom (RX) : org=0x200000, len=0x8000 }
sections {
    init org=0x200100 addr=0x6000 len=0x20 : { *(d1 d2) } = 0xFFFF
    lone org=0x200200 : { *(k) }
    tail align(0x10) : { other.rel }
    small : { *(z) }
    ovl ORG=0x200000 align=4 : { *(q) }
}
)");
    std::vector<LcfInput> v = {in(0, "d2", "a.asm", "data", 3),  in(1, "d1", "a.asm", "data", 2),
                               in(2, "d1", "b.asm", "data", 1),  in(3, "k", "a.asm", "romdata", 1),
                               in(4, "t1", "lib/other.asm", "code", 5), in(5, "t2", "lib/other.asm", "code", 1),
                               in(6, "z", "a.asm", "data", 1, 1, 8)};
    LcfResult r = l.place(v, {});
    dump(r);
    eq(r.placed[1].addr, 0x6000, "org!=addr: d1 run");
    eq(r.placed[1].load, 0x200100, "org!=addr: d1 load");
    eq(r.placed[2].addr, 0x6002, "org!=addr: second d1 joined");
    eq(r.placed[0].addr, 0x6003, "org!=addr: d2 after all d1");
    eq(r.placed[0].load, 0x200103, "org!=addr: d2 load");
    eq(r.outputs[0].size, 0x20, "len= minimum size");
    check(r.outputs[0].has_pad && r.outputs[0].pad == 0xFFFF, "padding parsed");
    eq(r.placed[3].addr, 0x200200, "org only: addr = org");
    eq(r.placed[4].addr, 0x200210, "FILE without list: all its sections, after '.' aligned");
    eq(r.placed[5].addr, 0x200215, "FILE: second section");
    eq(r.placed[6].addr, 0, "small data at ram start");
    check(has(r.errors, "can't both be given"), "org and align error");
    eq(r.errors.size(), 1, "org!=addr: only the org+align error");
}

}  // namespace

int test_lcf(const std::string& /*tr_dir_unused*/) {
    failures = 0;
    test_cart();
    test_default();
    test_expr_and_dot();
    test_errors();
    test_syntax();
    test_predefined_and_attrs();
    test_org_neq_addr_and_misc();
    if (failures) {
        std::cerr << "lcf: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "lcf: ok\n";
    return 0;
}
