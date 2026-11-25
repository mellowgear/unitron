// Text with a character map -> asm900 `db` lines. Port of the original Python unitron/data/charmap.py. (removed; in git history before this commit)
// All strings are UTF-8; matching and lengths work on code points as in Python.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bytes.hpp"

namespace unitron {

class CharmapError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Path(path).read_text(encoding="utf-8"): strict UTF-8 (a bad byte throws std::runtime_error
// with Python's UnicodeDecodeError text), \r\n and lone \r become \n. A missing file throws
// std::runtime_error("[Errno 2] No such file or directory: '<path>'").
std::string read_text_utf8(const std::string& path);

class Charmap {
public:
    // Reads and parses the charmap file; path appears in error messages as given.
    explicit Charmap(const std::string& path);
    // Parse charmap text already read (as read_text_utf8 returns it).
    static Charmap from_text(const std::string& text, const std::string& path);

    Bytes encode(const std::string& s) const;                       // throws CharmapError
    std::optional<Bytes> command(const std::string& line) const;    // nullopt: not a command

    // Parsed tables (code points as UTF-8): chars in insertion order, keys longest first.
    std::vector<std::pair<std::string, int>> chars;
    std::map<std::string, std::pair<int, bool>> cmds;   // name -> (prefix byte or -1, takes string)
    std::vector<std::string> keys;

private:
    Charmap() = default;
    void parse(const std::string& text, const std::string& path);
    std::vector<std::u32string> keys32_;
    std::map<std::u32string, int> char_code_;
};

// An empty source means None (no header line, "text" in error locations).
std::string text_to_db(const Charmap& charmap, const std::string& text, const std::string& source = "");

}  // namespace unitron
