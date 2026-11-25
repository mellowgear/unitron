// The per-feature CLI cases (tests/asm/, tests/link/, ...; tests/README.md describes the layout).
//   unitron_tests cases TESTS_DIR UNITRON_EXE FOLDER[/NAME]... [--write]
// For every case of each FOLDER (or the one case FOLDER/NAME): copy its input files into an empty
// directory, run its unitron commands there, and compare stdout with NAME.out, stderr with
// NAME.err, the exit code with NAME.exit (or the default: 1 when NAME.err has a line with
// "error", else 0), and every file the commands create or change with NAME.out.<path>.
// --write rewrites the expected files of the cases that ran (and removes stale ones).
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "testutil.hpp"
#include "util.hpp"

using namespace unitron;
namespace fs = std::filesystem;

namespace {

// The command each folder runs when a case has no NAME.cmd; NAME.flags is appended to its last
// line. {name} is the case name, {*.EXT} every top-level file of the work directory with that
// extension, sorted by name. A folder without a default needs NAME.cmd in every case.
const std::map<std::string, std::vector<std::string>> DEFAULTS = {
    {"asm", {"asm {name}.asm -o out.bin"}},
    {"link", {"asm {*.asm} -o out.bin"}},
    {"lcf", {"asm {*.asm} --lcf {*.lcf} -o out.bin"}},
    {"mpp", {"mac {name}.asm"}},
    {"dis", {"asm {name}.asm -o out.ngp --no-mpp", "dis out.ngp"}},
    {"text", {"text {name}.charmap {name}.txt"}},
    {"bin2db", {"bin2db {name}.bin"}},
    {"z80", {"z80 asm {name}.z80 -o out.bin"}},
};

// A metadata file of case NAME is NAME.<one of these>, or NAME.out.<anything> (an output).
const std::set<std::string> META = {"flags", "cmd", "out", "err", "exit"};

struct Case {
    std::string folder, name;
    fs::path dir;                  // the folder
    std::vector<fs::path> inputs;  // NAME.<ext> files
    bool has_dir = false;          // NAME/ exists
};

std::string text_of(const Bytes& b) { return std::string(b.begin(), b.end()); }

// split a command line: white space separates, '...' and "..." quote
std::vector<std::string> shsplit(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in = false;
    char q = 0;
    for (char c : s) {
        if (q) {
            if (c == q) q = 0;
            else cur += c;
            continue;
        }
        if (c == '"' || c == '\'') {
            q = c;
            in = true;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            if (in) out.push_back(cur);
            cur.clear();
            in = false;
        } else {
            cur += c;
            in = true;
        }
    }
    if (in) out.push_back(cur);
    return out;
}

// the non-empty, non-comment lines of a .cmd/.flags file
std::vector<std::string> command_lines(const fs::path& p) {
    std::vector<std::string> out;
    for (auto& line : split_on(text_of(read_bytes(p.u8string())), "\n")) {
        std::string s = strip(line);
        if (!s.empty() && s[0] != '#') out.push_back(s);
    }
    return out;
}

// name (before the first '.') and the rest
std::pair<std::string, std::string> split_name(const std::string& file) {
    auto dot = file.find('.');
    if (dot == std::string::npos) return {file, ""};
    return {file.substr(0, dot), file.substr(dot + 1)};
}

bool is_meta(const std::string& rest) { return META.count(rest) || starts_with(rest, "out."); }

std::vector<Case> find_cases(const fs::path& tests, const std::string& folder) {
    std::map<std::string, Case> m;
    fs::path dir = tests / fs::u8path(folder);
    if (!fs::is_directory(dir)) throw std::runtime_error("no folder " + dir.u8string());
    for (auto& e : fs::directory_iterator(dir)) {
        std::string file = e.path().filename().u8string();
        auto [name, rest] = split_name(file);
        if (name.empty() || file == "README.md") continue;
        if (e.is_directory()) {
            if (!rest.empty()) throw std::runtime_error(folder + "/" + file + ": a case folder's name has no '.'");
            m[name].has_dir = true;
        } else if (!rest.empty() && !is_meta(rest)) {
            m[name].inputs.push_back(e.path());
        } else {
            m[name];   // metadata alone does not make a case; checked below
        }
    }
    std::vector<Case> out;
    for (auto& kv : m) {
        Case c = kv.second;
        c.folder = folder;
        c.name = kv.first;
        c.dir = dir;
        if (c.inputs.empty() && !c.has_dir) {
            if (fs::exists(dir / fs::u8path(c.name + ".cmd"))) {   // a case may be only a command (CLI checks)
                out.push_back(c);
                continue;
            }
            throw std::runtime_error(folder + "/" + c.name + ": expected files but no input, folder or .cmd");
        }
        std::sort(c.inputs.begin(), c.inputs.end());
        out.push_back(c);
    }
    return out;
}

std::map<std::string, Bytes> snapshot(const fs::path& d) {
    std::map<std::string, Bytes> out;
    for (auto& e : fs::recursive_directory_iterator(d)) {
        if (!e.is_regular_file()) continue;
        out[fs::relative(e.path(), d).generic_u8string()] = read_bytes(e.path().u8string());
    }
    return out;
}

// the expected-file name of a produced file: out.bin -> NAME.out.bin, out/main.asm ->
// NAME.out.main.asm; a file whose name doesn't start with "out" goes under NAME.out.
std::string expected_name(const std::string& name, const std::string& rel) {
    std::string p = replace_all(rel, "/", ".");
    return name + "." + (starts_with(p, "out.") ? p : "out." + p);
}

int default_exit(const std::string& err) {
    for (auto& line : split_on(err, "\n"))
        if (line.find("error") != std::string::npos) return 1;
    return 0;
}

// expand {name} and {*.EXT}
std::vector<std::string> expand(const std::vector<std::string>& args, const std::string& name, const fs::path& work) {
    std::vector<std::string> out;
    for (auto& a : args) {
        if (starts_with(a, "{*.") && ends_with(a, "}")) {
            std::string ext = a.substr(2, a.size() - 3);
            std::vector<std::string> files;
            for (auto& e : fs::directory_iterator(work))
                if (e.is_regular_file() && e.path().extension().u8string() == ext) files.push_back(e.path().filename().u8string());
            std::sort(files.begin(), files.end());
            out.insert(out.end(), files.begin(), files.end());
        } else {
            out.push_back(replace_all(a, "{name}", name));
        }
    }
    return out;
}

std::string crlf_to_lf(const Bytes& b) {
    std::string s = text_of(b);
#ifdef _WIN32
    s = replace_all(s, "\r\n", "\n");   // the console streams are text mode on Windows
#endif
    return s;
}

// where two contents first differ, in a line or two
std::string first_difference(const std::string& want, const std::string& got) {
    bool text = want.find('\0') == std::string::npos && got.find('\0') == std::string::npos;
    if (text) {
        auto w = split_on(want, "\n"), g = split_on(got, "\n");
        for (size_t i = 0; i < std::max(w.size(), g.size()); i++) {
            std::string a = i < w.size() ? w[i] : "(end)", b = i < g.size() ? g[i] : "(end)";
            if (a != b) return "line " + dec(int64_t(i) + 1) + "\n      want: " + a + "\n      got:  " + b;
        }
        return "(same lines)";
    }
    size_t i = 0;
    while (i < want.size() && i < got.size() && want[i] == got[i]) i++;
    return "byte 0x" + hexl(int64_t(i)) + " (want " + dec(int64_t(want.size())) + " bytes, got " + dec(int64_t(got.size())) + ")";
}

struct Result {
    std::string out, err;
    int code = 0;
    std::map<std::string, Bytes> files;   // expected name -> content
    std::string problem;                  // a broken case (not a mismatch)
};

Result run_case(const Case& c, const std::string& exe, const fs::path& scratch) {
    Result r;
    fs::path work = fresh_dir("case");
    if (c.has_dir) fs::copy(c.dir / fs::u8path(c.name), work, fs::copy_options::recursive);
    for (auto& p : c.inputs) fs::copy_file(p, work / p.filename());
    fs::path cmdf = c.dir / fs::u8path(c.name + ".cmd"), flagf = c.dir / fs::u8path(c.name + ".flags");
    std::vector<std::vector<std::string>> steps;
    if (fs::exists(cmdf)) {
        if (fs::exists(flagf)) r.problem = "has both .cmd and .flags";
        for (auto& line : command_lines(cmdf)) steps.push_back(shsplit(line));
    } else {
        auto d = DEFAULTS.find(c.folder);
        if (d == DEFAULTS.end()) r.problem = "no .cmd (this folder has no default command)";
        else
            for (auto& line : d->second) steps.push_back(shsplit(line));
        if (fs::exists(flagf) && !steps.empty())
            for (auto& line : command_lines(flagf))
                for (auto& a : shsplit(line)) steps.back().push_back(a);
    }
    if (steps.empty() && r.problem.empty()) r.problem = "no command";
    auto before = snapshot(work);
    for (size_t i = 0; i < steps.size() && r.problem.empty(); i++) {
        Run x = run_in(work, exe, expand(steps[i], c.name, work), scratch);
        r.out += crlf_to_lf(x.out);
        r.err += crlf_to_lf(x.err);
        r.code = x.code;
        if (i + 1 < steps.size() && x.code != 0)
            r.problem = "step " + dec(int64_t(i) + 1) + " (" + join(steps[i], " ") + ") exited " + dec(x.code) + ":\n" + text_of(x.err);
    }
    for (auto& kv : snapshot(work)) {
        auto b = before.find(kv.first);
        if (b == before.end() || b->second != kv.second) r.files[expected_name(c.name, kv.first)] = kv.second;
    }
    fs::remove_all(work);
    return r;
}

// the expected files of a case now on disk: NAME.out.* outputs
std::set<std::string> expected_outputs(const Case& c) {
    std::set<std::string> s;
    for (auto& e : fs::directory_iterator(c.dir)) {
        std::string f = e.path().filename().u8string();
        if (e.is_regular_file() && starts_with(f, c.name + ".out.")) s.insert(f);
    }
    return s;
}

bool read_opt(const fs::path& p, std::string& out) {
    Bytes b;
    if (!read_bytes_opt(p.u8string(), b)) return false;
    out = text_of(b);
    return true;
}

// compare; returns the problems (empty = pass)
std::vector<std::string> compare(const Case& c, const Result& r) {
    std::vector<std::string> bad;
    if (!r.problem.empty()) return {r.problem};
    std::string want_out, want_err, want_exit;
    read_opt(c.dir / fs::u8path(c.name + ".out"), want_out);
    read_opt(c.dir / fs::u8path(c.name + ".err"), want_err);
    int code = default_exit(want_err);
    if (read_opt(c.dir / fs::u8path(c.name + ".exit"), want_exit)) code = int(py_int10(strip(want_exit)));
    if (r.code != code) bad.push_back("exit code " + dec(r.code) + ", want " + dec(code));
    if (r.out != want_out) bad.push_back("stdout differs from .out at " + first_difference(want_out, r.out));
    if (r.err != want_err) bad.push_back("stderr differs from .err at " + first_difference(want_err, r.err));
    auto have = expected_outputs(c);
    for (auto& kv : r.files) {
        std::string want;
        if (!read_opt(c.dir / fs::u8path(kv.first), want)) bad.push_back("wrote a file with no " + kv.first);
        else if (want != text_of(kv.second)) bad.push_back(kv.first + " differs at " + first_difference(want, text_of(kv.second)));
        have.erase(kv.first);
    }
    for (auto& f : have) bad.push_back("did not write the file of " + f);
    return bad;
}

void write_expected(const Case& c, const Result& r) {
    auto put = [&](const std::string& file, const std::string& content, bool keep_empty) {
        fs::path p = c.dir / fs::u8path(file);
        if (content.empty() && !keep_empty) fs::remove(p);
        else write_bytes(p.u8string(), Bytes(content.begin(), content.end()));
    };
    put(c.name + ".out", r.out, false);
    put(c.name + ".err", r.err, false);
    put(c.name + ".exit", r.code == default_exit(r.err) ? "" : dec(r.code) + "\n", false);
    auto stale = expected_outputs(c);
    for (auto& kv : r.files) {
        put(kv.first, text_of(kv.second), true);
        stale.erase(kv.first);
    }
    for (auto& f : stale) fs::remove(c.dir / fs::u8path(f));
}

}  // namespace

int test_cases(const std::string& tests_dir, const std::string& exe0, const std::vector<std::string>& which, bool write) {
    std::string exe = fs::absolute(fs::u8path(exe0)).u8string();
    fs::path tests = fs::u8path(tests_dir);
    fs::path scratch = fresh_dir("scratch");
    int total = 0, failed = 0;
    for (auto& w : which) {
        auto slash = w.find('/');
        std::string folder = w.substr(0, slash), only = slash == std::string::npos ? "" : w.substr(slash + 1);
        int n = 0, nf = 0;
        for (auto& c : find_cases(tests, folder)) {
            if (!only.empty() && c.name != only) continue;
            n++;
            Result r = run_case(c, exe, scratch);
            if (write && r.problem.empty()) {
                write_expected(c, r);
                continue;
            }
            auto bad = compare(c, r);
            if (!bad.empty()) {
                nf++;
                std::cerr << "FAIL " << folder << "/" << c.name << "\n";
                for (auto& b : bad) std::cerr << "    " << b << "\n";
            }
        }
        if (!only.empty() && n == 0) {
            std::cerr << "no case " << w << "\n";
            nf++;
        }
        std::cout << folder << ": " << n << " cases, " << (write ? "expected files written" : dec(nf) + " failed") << "\n";
        total += n;
        failed += nf;
    }
    fs::remove_all(scratch);
    if (which.size() > 1) std::cout << "total: " << total << " cases, " << failed << " failed\n";
    return failed ? 1 : 0;
}
