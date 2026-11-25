// Helpers shared by the tests that run the unitron binary (golden.cpp, cart.cpp).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "bytes.hpp"

namespace unitron {

struct Run {
    int code;
    Bytes out, err;
};

// Run EXE ARGS... in DIR; stdout and stderr go through files in SCRATCH.
Run run_in(const std::filesystem::path& dir, const std::string& exe, const std::vector<std::string>& args,
           const std::filesystem::path& scratch);

// A new, empty directory under the system's temporary directory.
std::filesystem::path fresh_dir(const std::string& tag);

}  // namespace unitron
