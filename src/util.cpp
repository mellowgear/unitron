#include "util.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace unitron {

bool py_space(unsigned char c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F);
}
bool py_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }
bool py_alnum(unsigned char c) { return py_alpha(c) || py_digit(c); }

std::string lstrip(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && py_space(s[i])) i++;
    return s.substr(i);
}
std::string rstrip(const std::string& s) {
    size_t n = s.size();
    while (n && py_space(s[n - 1])) n--;
    return s.substr(0, n);
}
std::string strip(const std::string& s) { return lstrip(rstrip(s)); }
std::string rstrip_chars(const std::string& s, const std::string& chars) {
    size_t n = s.size();
    while (n && chars.find(s[n - 1]) != std::string::npos) n--;
    return s.substr(0, n);
}

std::vector<std::string> split_ws(const std::string& s, int maxsplit) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    while (true) {
        while (i < n && py_space(s[i])) i++;
        if (i >= n) break;
        if (maxsplit >= 0 && (int)out.size() == maxsplit) {
            out.push_back(rstrip(s.substr(i)));
            break;
        }
        size_t j = i;
        while (j < n && !py_space(s[j])) j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::vector<std::string> split_on(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    size_t i = 0;
    while (true) {
        size_t j = s.find(sep, i);
        if (j == std::string::npos) {
            out.push_back(s.substr(i));
            return out;
        }
        out.push_back(s.substr(i, j - i));
        i = j + sep.size();
    }
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::string upper(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 32);
    return s;
}
std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
std::string replace_all(std::string s, const std::string& a, const std::string& b) {
    if (a.empty()) return s;
    size_t i = 0;
    while ((i = s.find(a, i)) != std::string::npos) {
        s.replace(i, a.size(), b);
        i += b.size();
    }
    return s;
}

static std::string hex_digits(uint64_t v, bool up) {
    const char* d = up ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do {
        s.insert(s.begin(), d[v & 15]);
        v >>= 4;
    } while (v);
    return s;
}
static std::string pad_signed(bool neg, std::string digits, int width) {
    int w = width - (neg ? 1 : 0);
    while ((int)digits.size() < w) digits.insert(digits.begin(), '0');
    return neg ? "-" + digits : digits;
}
std::string hexs(int64_t v, int width) {
    bool neg = v < 0;
    uint64_t m = neg ? uint64_t(0) - uint64_t(v) : uint64_t(v);
    return pad_signed(neg, hex_digits(m, true), width);
}
std::string hexl(int64_t v, int width) {
    bool neg = v < 0;
    uint64_t m = neg ? uint64_t(0) - uint64_t(v) : uint64_t(v);
    return pad_signed(neg, hex_digits(m, false), width);
}
std::string dec(int64_t v) { return std::to_string(v); }
std::string hexbyte(unsigned v) { return "0x" + hexs(v, 2); }

std::vector<uint32_t> utf8_decode(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = s[i];
        if (c < 0x80) {
            out.push_back(c);
            i++;
            continue;
        }
        int len = c >= 0xF0 && c <= 0xF4 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c < 0xE0 ? 2 : 0;
        if (c >= 0xF5) len = 0;
        uint32_t cp = len == 2 ? c & 0x1F : len == 3 ? c & 0x0F : c & 0x07;
        bool ok = len > 0 && i + len <= n;
        for (int k = 1; ok && k < len; k++) {
            unsigned char d = s[i + k];
            if ((d & 0xC0) != 0x80) ok = false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (ok && len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp < 0xE000))) ok = false;
        if (ok && len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ok = false;
        if (!ok) {
            out.push_back(0xDC00 + c);
            i++;
            continue;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string utf8_encode(const std::vector<uint32_t>& cps) {
    std::string s;
    for (uint32_t c : cps) {
        if (c >= 0xDC80 && c <= 0xDCFF) {
            s += char(c - 0xDC00);
        } else if (c < 0x80) {
            s += char(c);
        } else if (c < 0x800) {
            s += char(0xC0 | (c >> 6));
            s += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            s += char(0xE0 | (c >> 12));
            s += char(0x80 | ((c >> 6) & 0x3F));
            s += char(0x80 | (c & 0x3F));
        } else {
            s += char(0xF0 | (c >> 18));
            s += char(0x80 | ((c >> 12) & 0x3F));
            s += char(0x80 | ((c >> 6) & 0x3F));
            s += char(0x80 | (c & 0x3F));
        }
    }
    return s;
}

static bool printable(uint32_t c) {
    if (c < 0x20 || c == 0x7F) return false;
    if (c < 0x7F) return true;
    if (c <= 0xA0 || c == 0xAD) return false;
    if (c >= 0xD800 && c < 0xE000) return false;
    if ((c >= 0x2000 && c <= 0x200F) || (c >= 0x2028 && c <= 0x202F) || (c >= 0x205F && c <= 0x206F) ||
        c == 0x3000 || c == 0xFEFF || (c >= 0xFFF9 && c <= 0xFFFB) || c == 0x1680 || c == 0x180E)
        return false;
    if (c >= 0xE000 && c <= 0xF8FF) return false;
    return true;
}

std::string py_repr(const std::string& s) {
    auto cps = utf8_decode(s);
    bool sq = false, dq = false;
    for (auto c : cps) {
        sq |= c == '\'';
        dq |= c == '"';
    }
    char q = (sq && !dq) ? '"' : '\'';
    std::string out(1, q);
    for (auto c : cps) {
        if (c == (uint32_t)q || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (printable(c)) {
            out += utf8_encode({c});
        } else if (c < 0x100) {
            out += "\\x" + hexl(c, 2);
        } else if (c < 0x10000) {
            out += "\\u" + hexl(c, 4);
        } else {
            out += "\\U" + hexl(c, 8);
        }
    }
    out += q;
    return out;
}

static int64_t parse_digits(const std::string& d, int base, bool allow_leading_zero) {
    if (d.empty() || d.front() == '_' || d.back() == '_') throw std::invalid_argument(d);
    if (!allow_leading_zero && d.size() > 1 && d[0] == '0') {
        for (char c : d)
            if (c != '0' && c != '_') throw std::invalid_argument(d);
    }
    uint64_t v = 0;
    char prev = 0;
    for (char c : d) {
        if (c == '_') {
            if (prev == '_') throw std::invalid_argument(d);
            prev = c;
            continue;
        }
        prev = c;
        int x = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'z') ? c - 'a' + 10 : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : 99;
        if (x >= base) throw std::invalid_argument(d);
        v = v * base + x;
    }
    return (int64_t)v;
}

int64_t py_int0(const std::string& s0) {
    std::string s = strip(s0);
    bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
        neg = s[0] == '-';
        s = s.substr(1);
    }
    int64_t v;
    std::string l = lower(s);
    if (starts_with(l, "0x")) v = parse_digits(s.substr(2 + (s.size() > 2 && s[2] == '_')), 16, true);
    else if (starts_with(l, "0o")) v = parse_digits(s.substr(2 + (s.size() > 2 && s[2] == '_')), 8, true);
    else if (starts_with(l, "0b")) v = parse_digits(s.substr(2 + (s.size() > 2 && s[2] == '_')), 2, true);
    else v = parse_digits(s, 10, false);
    return neg ? -v : v;
}

int64_t py_int10(const std::string& s0) {
    std::string s = strip(s0);
    bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
        neg = s[0] == '-';
        s = s.substr(1);
    }
    int64_t v = parse_digits(s, 10, true);
    return neg ? -v : v;
}

static const std::map<std::string, std::string>* g_overlay = nullptr;
void set_read_overlay(const std::map<std::string, std::string>* overlay) { g_overlay = overlay; }
std::string canonical_path(const std::string& path) {
    std::error_code ec;
    auto p = fs::weakly_canonical(fs::u8path(path), ec);
    return ec ? path : p.u8string();
}

bool read_bytes_opt(const std::string& path, Bytes& out) {
    if (g_overlay && !g_overlay->empty()) {
        auto it = g_overlay->find(canonical_path(path));
        if (it != g_overlay->end()) {
            out.assign(it->second.begin(), it->second.end());
            return true;
        }
    }
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(path), ec)) return false;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    out.assign(s.begin(), s.end());
    return true;
}
Bytes read_bytes(const std::string& path) {
    Bytes b;
    if (!read_bytes_opt(path, b)) throw std::runtime_error("can't open " + py_repr(path));
    return b;
}
void write_bytes(const std::string& path, const Bytes& b) {
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("can't write " + py_repr(path));
    f.write(reinterpret_cast<const char*>(b.data()), (std::streamsize)b.size());
    if (!f) throw std::runtime_error("can't write " + py_repr(path));
}
void write_text(const std::string& path, const std::string& s) { write_bytes(path, Bytes(s.begin(), s.end())); }
bool is_file(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec);
}
void make_dirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path), ec);
    if (ec && !fs::is_directory(fs::u8path(path))) throw std::runtime_error("can't create " + py_repr(path));
}

std::string universal_newlines(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\r') {
            out += '\n';
            if (i + 1 < s.size() && s[i + 1] == '\n') i++;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::vector<std::string> splitlines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0, i = 0, n = s.size();
    while (i < n) {
        unsigned char c = s[i];
        size_t len = 0;
        if (c == '\r') len = (i + 1 < n && s[i + 1] == '\n') ? 2 : 1;
        else if (c == '\n' || c == 0x0B || c == 0x0C || c == 0x1C || c == 0x1D || c == 0x1E) len = 1;
        else if (c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0x85) len = 2;
        else if (c == 0xE2 && i + 2 < n && (unsigned char)s[i + 1] == 0x80 &&
                 ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9)) len = 3;
        if (len) {
            out.push_back(s.substr(start, i - start));
            i += len;
            start = i;
        } else {
            i++;
        }
    }
    if (start < n) out.push_back(s.substr(start));
    return out;
}

static bool is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

bool path_absolute(const std::string& p) {
#ifdef _WIN32
    if (p.size() >= 3 && p[1] == ':' && is_sep(p[2])) return true;
    return p.size() >= 2 && is_sep(p[0]) && is_sep(p[1]);
#else
    return !p.empty() && p[0] == '/';
#endif
}

std::string path_norm(const std::string& p) {
    std::string root;
    size_t i = 0;
#ifdef _WIN32
    if (p.size() >= 2 && p[1] == ':') {
        root = p.substr(0, 2);
        i = 2;
    }
#endif
    if (i < p.size() && is_sep(p[i])) {
        root += '/';
        while (i < p.size() && is_sep(p[i])) i++;
    }
    std::vector<std::string> parts;
    std::string cur;
    for (; i <= p.size(); i++) {
        if (i == p.size() || is_sep(p[i])) {
            if (!cur.empty() && cur != ".") parts.push_back(cur);
            cur.clear();
        } else {
            cur += p[i];
        }
    }
    std::string out = root + join(parts, "/");
    return out.empty() ? "." : out;
}

std::string path_join(const std::string& a, const std::string& b) {
    if (path_absolute(b)) return path_norm(b);
    return path_norm(a + "/" + b);
}

std::string path_parent(const std::string& p0) {
    std::string p = path_norm(p0);
    size_t k = p.rfind('/');
    if (k == std::string::npos) return ".";
    if (k == 0) return "/";
    return p.substr(0, k);
}

std::string path_name(const std::string& p0) {
    std::string p = path_norm(p0);
    if (p == "." || p == "/") return "";
    size_t k = p.rfind('/');
    return k == std::string::npos ? p : p.substr(k + 1);
}

std::string path_stem(const std::string& p) {
    std::string n = path_name(p);
    size_t k = n.rfind('.');
    if (k == std::string::npos || k == 0 || k == n.size() - 1) return n;
    return n.substr(0, k);
}

}  // namespace unitron
