// Small helpers that reproduce the Python reference's string, number and path behavior.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace unitron {

using Bytes = std::vector<uint8_t>;

// Python str.isspace() for ASCII (incl. \x1c-\x1f); bytes >= 0x80 are not space.
bool py_space(unsigned char c);
// Python str.isalnum()/isalpha(): bytes >= 0x80 count as letters (part of a non-ASCII letter).
bool py_alnum(unsigned char c);
bool py_alpha(unsigned char c);
inline bool py_digit(unsigned char c) { return c >= '0' && c <= '9'; }

std::string strip(const std::string& s);
std::string lstrip(const std::string& s);
std::string rstrip(const std::string& s);
std::string rstrip_chars(const std::string& s, const std::string& chars);
std::vector<std::string> split_ws(const std::string& s, int maxsplit = -1);   // str.split()
std::vector<std::string> split_on(const std::string& s, const std::string& sep);  // str.split(sep)
std::string join(const std::vector<std::string>& v, const std::string& sep);
std::string upper(std::string s);
std::string lower(std::string s);
bool starts_with(const std::string& s, const std::string& p);
bool ends_with(const std::string& s, const std::string& p);
std::string replace_all(std::string s, const std::string& a, const std::string& b);

// Python f"{v:X}" / f"{v:0{w}X}" (sign first, then digits) and f"{v:d}".
std::string hexs(int64_t v, int width = 0);
std::string hexl(int64_t v, int width = 0);   // lowercase
std::string dec(int64_t v);
// "0x%02X" style for a byte
std::string hexbyte(unsigned v);

// Python repr() of a str (input UTF-8; invalid bytes are surrogate escapes, as in the reference).
std::string py_repr(const std::string& s);

// Python int(s, 0) (CLI numbers); throws std::invalid_argument.
int64_t py_int0(const std::string& s);
// Python int(s) base 10 (argparse type=int); throws std::invalid_argument.
int64_t py_int10(const std::string& s);

// Files
Bytes read_bytes(const std::string& path);          // throws std::runtime_error
bool read_bytes_opt(const std::string& path, Bytes& out);
void write_bytes(const std::string& path, const Bytes& b);
void write_text(const std::string& path, const std::string& s);
bool is_file(const std::string& path);
void make_dirs(const std::string& path);
// open(path).read() with universal newlines: \r\n and \r become \n
std::string universal_newlines(const std::string& s);
// str.splitlines()
std::vector<std::string> splitlines(const std::string& s);

// pathlib: str(Path(p)) normalization, joining, parent, stem
std::string path_norm(const std::string& p);
std::string path_join(const std::string& a, const std::string& b);
std::string path_parent(const std::string& p);
std::string path_stem(const std::string& p);
std::string path_name(const std::string& p);
bool path_absolute(const std::string& p);

// UTF-8 decode to code points (invalid bytes -> 0xDC80+b, like surrogateescape) and back.
std::vector<uint32_t> utf8_decode(const std::string& s);
std::string utf8_encode(const std::vector<uint32_t>& cps);

}  // namespace unitron
