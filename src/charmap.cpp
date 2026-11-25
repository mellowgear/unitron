#include "charmap.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace unitron {
namespace {

using U32 = std::u32string;

// Strict UTF-8 decoding with CPython's UnicodeDecodeError message.
U32 decode_utf8(const std::string& s) {
    U32 out;
    out.reserve(s.size());
    const size_t n = s.size();
    auto byte = [&](size_t i) { return static_cast<uint8_t>(s[i]); };
    auto err = [&](size_t start, size_t end, const char* reason) {
        static const char* HEX = "0123456789abcdef";
        std::string m = "'utf-8' codec can't decode ";
        if (end - start == 1) {
            uint8_t b = byte(start);
            m += "byte 0x";
            m += HEX[b >> 4];
            m += HEX[b & 15];
            m += " in position " + std::to_string(start);
        } else {
            m += "bytes in position " + std::to_string(start) + "-" + std::to_string(end - 1);
        }
        throw std::runtime_error(m + ": " + reason);
    };
    size_t i = 0;
    while (i < n) {
        uint8_t c = byte(i);
        if (c < 0x80) {
            out += static_cast<char32_t>(c);
            i++;
            continue;
        }
        int more;
        uint8_t lo = 0x80, hi = 0xBF;
        uint32_t cp;
        if (c >= 0xC2 && c <= 0xDF) { more = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) {
            more = 2; cp = c & 0x0F;
            if (c == 0xE0) lo = 0xA0;
            if (c == 0xED) hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            more = 3; cp = c & 0x07;
            if (c == 0xF0) lo = 0x90;
            if (c == 0xF4) hi = 0x8F;
        } else {
            err(i, i + 1, "invalid start byte");
        }
        for (int k = 1; k <= more; k++) {
            if (i + k >= n) err(i, n, "unexpected end of data");
            uint8_t d = byte(i + k);
            uint8_t l = k == 1 ? lo : 0x80, h = k == 1 ? hi : 0xBF;
            if (d < l || d > h) err(i, i + k, "invalid continuation byte");
            cp = (cp << 6) | (d & 0x3F);
        }
        out += static_cast<char32_t>(cp);
        i += static_cast<size_t>(more) + 1;
    }
    return out;
}

std::string encode_utf8(const U32& s) {
    std::string o;
    o.reserve(s.size());
    for (char32_t c : s) {
        if (c < 0x80) {
            o += static_cast<char>(c);
        } else if (c < 0x800) {
            o += static_cast<char>(0xC0 | (c >> 6));
            o += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            o += static_cast<char>(0xE0 | (c >> 12));
            o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (c >> 18));
            o += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return o;
}

U32 u32(const char* a) {
    U32 s;
    while (*a) s += static_cast<char32_t>(static_cast<unsigned char>(*a++));
    return s;
}

// str.isspace() / re \s
bool is_space(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

bool in(char32_t c, char32_t a, char32_t b) { return c >= a && c <= b; }

// re \w (str.isalnum() or '_'): exact for ASCII and Latin-1; elsewhere every code point outside
// the common punctuation, symbol, combining-mark and space blocks below counts as a letter.
bool is_word(char32_t c) {
    if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    if (c < 0x100) {
        if (c < 0xA0) return false;
        if (c < 0xC0) return c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA ||
                             (c >= 0xBC && c <= 0xBE);
        return c != 0xD7 && c != 0xF7;
    }
    if (is_space(c)) return false;
    if (in(c, 0x300, 0x36F) || in(c, 0x2000, 0x206F) || in(c, 0x20A0, 0x20FF) || in(c, 0x2190, 0x23FF) ||
        in(c, 0x2500, 0x2775) || in(c, 0x2794, 0x27BF) || in(c, 0x3000, 0x3004) || in(c, 0x3008, 0x3020) ||
        c == 0x3030 || in(c, 0x303D, 0x303F) || in(c, 0x3099, 0x309C) || c == 0x30FB || in(c, 0xFF01, 0xFF0F) ||
        in(c, 0xFF1A, 0xFF20) || in(c, 0xFF3B, 0xFF40) || in(c, 0xFF5B, 0xFF65) || in(c, 0xD800, 0xDFFF) ||
        in(c, 0xFE00, 0xFE0F) || in(c, 0xFEFF, 0xFEFF) || in(c, 0x1F000, 0x1FAFF))
        return false;
    return true;
}

// str.isprintable() for one code point (Cc, Cf, Cs, Co, Zl, Zp, Zs other than space, and the
// noncharacters are not printable; other unassigned code points are treated as printable).
bool is_printable(char32_t c) {
    if (c < 0x20 || in(c, 0x7F, 0xA0) || c == 0xAD) return false;
    if (c < 0x100) return true;
    if (in(c, 0x600, 0x605) || c == 0x61C || c == 0x6DD || c == 0x70F || in(c, 0x890, 0x891) || c == 0x8E2 ||
        c == 0x1680 || c == 0x180E || in(c, 0x2000, 0x200F) || in(c, 0x2028, 0x202F) || in(c, 0x205F, 0x206F) ||
        c == 0x3000 || in(c, 0xD800, 0xF8FF) || in(c, 0xFDD0, 0xFDEF) || c == 0xFEFF || in(c, 0xFFF9, 0xFFFB) ||
        (c & 0xFFFE) == 0xFFFE || c == 0x110BD || c == 0x110CD || in(c, 0x13430, 0x1343F) ||
        in(c, 0x1BCA0, 0x1BCA3) || in(c, 0x1D173, 0x1D17A) || c == 0xE0001 || in(c, 0xE0020, 0xE007F) ||
        c >= 0xF0000)
        return false;
    return true;
}

// repr() of a one-character str
std::string repr1(char32_t c) {
    if (c == '\'') return "\"'\"";
    static const char* HEX = "0123456789abcdef";
    std::string b;
    if (c == '\\') b = "\\\\";
    else if (c == '\t') b = "\\t";
    else if (c == '\n') b = "\\n";
    else if (c == '\r') b = "\\r";
    else if (is_printable(c)) b = encode_utf8(U32(1, c));
    else {
        int digits = c < 0x100 ? 2 : c < 0x10000 ? 4 : 8;
        b = c < 0x100 ? "\\x" : c < 0x10000 ? "\\u" : "\\U";
        for (int k = digits - 1; k >= 0; k--) b += HEX[(c >> (4 * k)) & 15];
    }
    return "'" + b + "'";
}

// str.splitlines()
std::vector<U32> splitlines(const U32& s) {
    std::vector<U32> out;
    size_t start = 0, i = 0;
    while (i < s.size()) {
        char32_t c = s[i];
        bool brk = c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || in(c, 0x1C, 0x1E) || c == 0x85 ||
                   c == 0x2028 || c == 0x2029;
        if (!brk) {
            i++;
            continue;
        }
        out.push_back(s.substr(start, i - start));
        i += (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ? 2 : 1;
        start = i;
    }
    if (start < s.size()) out.push_back(s.substr(start));
    return out;
}

U32 rstrip(const U32& s) {
    size_t e = s.size();
    while (e > 0 && is_space(s[e - 1])) e--;
    return s.substr(0, e);
}

U32 strip(const U32& s) {
    size_t b = 0, e = s.size();
    while (b < e && is_space(s[b])) b++;
    while (e > b && is_space(s[e - 1])) e--;
    return s.substr(b, e - b);
}

bool is_hex(char32_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
int hexval(char32_t c) { return c <= '9' ? int(c - '0') : c <= 'F' ? int(c - 'A' + 10) : int(c - 'a' + 10); }

bool starts(const U32& s, size_t p, const U32& t) { return s.compare(p, t.size(), t) == 0 && p + t.size() <= s.size(); }

size_t skip_ws(const U32& s, size_t p) {
    while (p < s.size() && is_space(s[p])) p++;
    return p;
}

// `\s*(?:;.*)?$` from position p (re.match semantics: `.` stops at \n, `$` also before a final \n)
bool tail_ok(const U32& s, size_t p) {
    p = skip_ws(s, p);
    if (p == s.size()) return true;
    if (s[p] != ';') return false;
    size_t nl = s.find(U'\n', p);
    return nl == U32::npos || nl == s.size() - 1;
}

std::string hex2(unsigned v) {
    static const char* HEX = "0123456789ABCDEF";
    std::string r = "0x";
    r += HEX[(v >> 4) & 15];
    r += HEX[v & 15];
    return r;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("[Errno 2] No such file or directory: '" + path + "'");
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

std::string read_text_utf8(const std::string& path) {
    std::string raw = read_file(path);
    decode_utf8(raw);  // validate
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\r') {
            out += '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n') i++;
        } else {
            out += raw[i];
        }
    }
    return out;
}

Charmap::Charmap(const std::string& path) { parse(read_text_utf8(path), path); }

Charmap Charmap::from_text(const std::string& text, const std::string& path) {
    Charmap c;
    c.parse(text, path);
    return c;
}

void Charmap::parse(const std::string& text, const std::string& path) {
    std::vector<U32> lines = splitlines(decode_utf8(text));
    std::vector<U32> order;   // dict insertion order
    for (size_t ln = 0; ln < lines.size(); ln++) {
        const U32& raw = lines[ln];
        U32 s = strip(raw);
        if (s.empty() || s[0] == ';') continue;
        // char\s+"((?:[^"]|"")+)"\s+([0-9A-Fa-f]{2})\s*(?:;.*)?$
        if (starts(s, 0, u32("char"))) {
            size_t p = 4, q;
            if (p < s.size() && is_space(s[p])) {
                p = skip_ws(s, p);
                if (p < s.size() && s[p] == '"') {
                    size_t g = ++p;
                    q = g;
                    while (q < s.size()) {
                        if (s[q] != '"') q++;
                        else if (q + 1 < s.size() && s[q + 1] == '"') q += 2;
                        else break;
                    }
                    // only the longest group can be followed by `"\s+`
                    if (q > g && q < s.size() && s[q] == '"') {
                        size_t t = q + 1;
                        if (t < s.size() && is_space(s[t])) {
                            t = skip_ws(s, t);
                            if (t + 1 < s.size() && is_hex(s[t]) && is_hex(s[t + 1]) && tail_ok(s, t + 2)) {
                                U32 key;
                                U32 grp = s.substr(g, q - g);
                                for (size_t k = 0; k < grp.size(); k++) {
                                    key += grp[k];
                                    if (grp[k] == '"' && k + 1 < grp.size() && grp[k + 1] == '"') k++;
                                }
                                int code = hexval(s[t]) * 16 + hexval(s[t + 1]);
                                if (!char_code_.count(key)) order.push_back(key);
                                char_code_[key] = code;
                                continue;
                            }
                        }
                    }
                }
            }
        }
        // cmd\s+(\w+)\s+([0-9A-Fa-f]{2}|-)\s*(string)?\s*(?:;.*)?$
        if (starts(s, 0, u32("cmd"))) {
            size_t p = 3;
            if (p < s.size() && is_space(s[p])) {
                p = skip_ws(s, p);
                size_t w0 = p;
                while (p < s.size() && is_word(s[p])) p++;
                if (p > w0 && p < s.size() && is_space(s[p])) {
                    U32 name = s.substr(w0, p - w0);
                    p = skip_ws(s, p);
                    int pre = -2;
                    if (p + 1 < s.size() && is_hex(s[p]) && is_hex(s[p + 1])) {
                        pre = hexval(s[p]) * 16 + hexval(s[p + 1]);
                        p += 2;
                    } else if (p < s.size() && s[p] == '-') {
                        pre = -1;
                        p += 1;
                    }
                    if (pre != -2) {
                        size_t t = skip_ws(s, p);
                        bool matched = false, takes = false;
                        if (starts(s, t, u32("string")) && tail_ok(s, t + 6)) {
                            matched = takes = true;
                        } else if (tail_ok(s, t)) {
                            matched = true;
                        }
                        if (matched) {
                            cmds[encode_utf8(name)] = {pre, takes};
                            continue;
                        }
                    }
                }
            }
        }
        throw CharmapError(path + ":" + std::to_string(ln + 1) + ": bad charmap line: " + encode_utf8(raw));
    }
    keys32_ = order;
    std::stable_sort(keys32_.begin(), keys32_.end(),
                     [](const U32& a, const U32& b) { return a.size() > b.size(); });
    chars.clear();
    for (const U32& k : order) chars.emplace_back(encode_utf8(k), char_code_[k]);
    keys.clear();
    for (const U32& k : keys32_) keys.push_back(encode_utf8(k));
}

Bytes Charmap::encode(const std::string& str) const {
    U32 s = decode_utf8(str);
    Bytes out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '{' && i + 3 < s.size() && is_hex(s[i + 1]) && is_hex(s[i + 2]) && s[i + 3] == '}') {
            out.push_back(static_cast<uint8_t>(hexval(s[i + 1]) * 16 + hexval(s[i + 2])));
            i += 4;
            continue;
        }
        bool found = false;
        for (const U32& k : keys32_) {
            if (starts(s, i, k)) {
                out.push_back(static_cast<uint8_t>(char_code_.at(k)));
                i += k.size();
                found = true;
                break;
            }
        }
        if (!found) throw CharmapError("no character code for " + repr1(s[i]));
    }
    return out;
}

std::optional<Bytes> Charmap::command(const std::string& line_utf8) const {
    // ^\s*([a-z_]+)(?:\s+"([^"]*)")?\s*(?:;.*)?$
    U32 s = decode_utf8(line_utf8);
    size_t p = skip_ws(s, 0), w0 = p;
    while (p < s.size() && ((s[p] >= 'a' && s[p] <= 'z') || s[p] == '_')) p++;
    if (p == w0) return std::nullopt;
    std::string name = encode_utf8(s.substr(w0, p - w0));
    bool has_str = false;
    U32 arg;
    bool matched = false;
    if (p < s.size() && is_space(s[p])) {
        size_t r = skip_ws(s, p);
        if (r < s.size() && s[r] == '"') {
            size_t e = s.find(U'"', r + 1);
            if (e != U32::npos && tail_ok(s, e + 1)) {
                matched = has_str = true;
                arg = s.substr(r + 1, e - r - 1);
            }
        }
    }
    if (!matched && tail_ok(s, p)) matched = true;
    if (!matched) return std::nullopt;
    auto it = cmds.find(name);
    if (it == cmds.end()) return std::nullopt;
    int pre = it->second.first;
    bool takes = it->second.second;
    if (takes != has_str)
        throw CharmapError("`" + name + "` " + (takes ? "needs a string" : "takes no string"));
    Bytes out;
    if (pre >= 0) out.push_back(static_cast<uint8_t>(pre));
    if (takes) {
        Bytes e = encode(encode_utf8(arg));
        out.insert(out.end(), e.begin(), e.end());
    }
    return out;
}

std::string text_to_db(const Charmap& charmap, const std::string& text, const std::string& source) {
    std::vector<std::string> out;
    if (!source.empty())
        out.push_back("; generated by unitron text from " + source + ": edit the text, not this file");
    const std::string where = source.empty() ? std::string("text") : source;
    // Slot mode (`slot WIDTH PAD`): each string runs from a `text` line to the next `text` line,
    // label, `slot` line or the end of the file, and is padded with PAD to WIDTH bytes.
    size_t slot = 0, str_line = 0, str_bytes = 0, pad_at = 0;
    unsigned pad = 0xFF;
    bool in_str = false;
    auto close_string = [&]() {
        if (!in_str) return;
        in_str = false;
        if (str_bytes > slot)
            throw CharmapError(where + ":" + std::to_string(str_line) + ": the string needs " + std::to_string(str_bytes) +
                               " bytes, the slot is " + std::to_string(slot));
        if (str_bytes < slot)
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(pad_at),
                       "\tdfb\t" + std::to_string(slot - str_bytes) + "," + hex2(pad) + "\t; pad to " + std::to_string(slot));
    };
    std::vector<U32> lines = splitlines(decode_utf8(text));
    for (size_t n = 0; n < lines.size(); n++) {
        const U32& line = lines[n];
        U32 s = strip(line);
        if (s.empty() || s[0] == ';') {
            out.push_back(encode_utf8(rstrip(line)));
            continue;
        }
        // slot\s+(\d+)(?:\s+([0-9A-Fa-f]{2}))?\s*(?:;.*)?$  (the pad byte may be left out only for slot 0)
        if (starts(s, 0, u32("slot")) && (s.size() == 4 || is_space(s[4]))) {
            size_t p = skip_ws(s, 4), d0 = p;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9' && p - d0 < 9) p++;
            bool ok = p > d0 && (p == s.size() || !(s[p] >= '0' && s[p] <= '9'));
            size_t width = 0;
            for (size_t k = d0; ok && k < p; k++) width = width * 10 + size_t(s[k] - '0');
            int byte = -1;
            if (ok) {
                size_t q = skip_ws(s, p);
                if (q > p && q + 1 < s.size() && is_hex(s[q]) && is_hex(s[q + 1]) &&
                    (q + 2 == s.size() || is_space(s[q + 2]) || s[q + 2] == ';')) {
                    byte = hexval(s[q]) * 16 + hexval(s[q + 1]);
                    q += 2;
                }
                ok = tail_ok(s, byte < 0 ? p : q) && (byte >= 0 || width == 0);
            }
            if (!ok)
                throw CharmapError(where + ":" + std::to_string(n + 1) + ": bad slot line: " + encode_utf8(s) +
                                   " (slot WIDTH PAD, e.g. slot 7 FF; slot 0 ends slot mode)");
            close_string();
            slot = width;
            if (byte >= 0) pad = unsigned(byte);
            out.push_back("; " + encode_utf8(s));
            continue;
        }
        // re.fullmatch(r"[A-Za-z_.][\w.']*:", s)
        {
            char32_t c0 = s[0];
            bool lab = s.size() >= 2 && s.back() == ':' &&
                       ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_' || c0 == '.');
            for (size_t k = 1; lab && k + 1 < s.size(); k++)
                lab = is_word(s[k]) || s[k] == '.' || s[k] == '\'';
            if (lab) {
                close_string();
                out.push_back(encode_utf8(s));
                continue;
            }
        }
        std::string loc = where + ":" + std::to_string(n + 1) + ": ";
        std::optional<Bytes> b;
        try {
            b = charmap.command(encode_utf8(line));
        } catch (const CharmapError& e) {
            throw CharmapError(loc + e.what());
        }
        if (!b) throw CharmapError(loc + "not a text command: " + encode_utf8(s));
        if (slot) {
            size_t w = 0;
            while (w < s.size() && ((s[w] >= 'a' && s[w] <= 'z') || s[w] == '_')) w++;
            if (s.substr(0, w) == u32("text")) {
                close_string();
                in_str = true;
                str_line = n + 1;
                str_bytes = 0;
            } else if (!in_str) {
                throw CharmapError(loc + "in a slot, each string starts with a text line");
            }
            str_bytes += b->size();
        }
        std::string ss = encode_utf8(s);
        size_t total = std::max<size_t>(b->size(), 1);
        for (size_t i = 0; i < total; i += 16) {
            size_t end = std::min(b->size(), i + 16);
            if (end <= i) {
                out.push_back("\t; " + ss);
                continue;
            }
            std::string l = "\tdb\t";
            for (size_t k = i; k < end; k++) {
                if (k > i) l += ',';
                l += hex2((*b)[k]);
            }
            if (i == 0) l += "\t; " + ss;
            out.push_back(l);
        }
        if (in_str) pad_at = out.size();
    }
    close_string();
    std::string r;
    for (size_t i = 0; i < out.size(); i++) {
        if (i) r += '\n';
        r += out[i];
    }
    return r + "\n";
}

}  // namespace unitron
