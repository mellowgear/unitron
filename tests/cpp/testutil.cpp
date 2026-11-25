// run_in and fresh_dir (testutil.hpp), shared by the tests that run the unitron binary.
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "testutil.hpp"
#include "util.hpp"

using namespace unitron;
namespace fs = std::filesystem;

static std::string quote(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string o = "'";
    for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return o + "'";
#endif
}

Run unitron::run_in(const fs::path& dir, const std::string& exe, const std::vector<std::string>& args, const fs::path& scratch) {
    fs::path so = scratch / "stdout", se = scratch / "stderr";
    std::string cmd;
#ifdef _WIN32
    cmd = "cd /d " + quote(dir.u8string()) + " && " + quote(exe);
#else
    cmd = "cd " + quote(dir.u8string()) + " && " + quote(exe);
#endif
    for (auto& a : args) cmd += " " + quote(a);
    cmd += " > " + quote(so.u8string()) + " 2> " + quote(se.u8string());
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    int st = std::system(cmd.c_str());
    Run r;
#ifdef _WIN32
    r.code = st;
#else
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
#endif
    r.out = read_bytes(so.u8string());
    r.err = read_bytes(se.u8string());
    return r;
}

fs::path unitron::fresh_dir(const std::string& tag) {
    std::random_device rd;
    fs::path p = fs::temp_directory_path() / ("unitron_test_" + tag + "_" + std::to_string(rd()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}
