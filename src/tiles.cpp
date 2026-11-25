#include "tiles.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace unitron {

const Palette GREYS = {{{255, 255, 255}, {170, 170, 170}, {85, 85, 85}, {0, 0, 0}}};
const char* const LAYOUT_KEY = "unitron-layout";

namespace {

const char* const MOD_ZERO = "integer modulo by zero";
const char* const DIV_ZERO = "integer division or modulo by zero";

int64_t floordiv(int64_t a, int64_t b, const char* msg = DIV_ZERO) {
    if (b == 0) throw std::runtime_error(msg);
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

int64_t pymod(int64_t a, int64_t b) {
    if (b == 0) throw std::runtime_error(MOD_ZERO);
    int64_t m = a % b;
    if (m != 0 && ((m < 0) != (b < 0))) m += b;
    return m;
}

// Pillow PixelAccess indexing: a negative coordinate counts from the far edge once.
size_t pil_index(int64_t x, int64_t y, int64_t w, int64_t h) {
    if (x < 0) x += w;
    if (y < 0) y += h;
    if (x < 0 || x >= w || y < 0 || y >= h) throw std::out_of_range("image index out of range");
    return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
}

std::string itos(int64_t v) { return std::to_string(v); }

Bytes read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("can't open '" + path + "'");
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// --- just enough JSON (RFC 8259) to read the layout text chunk ---
struct Json {
    enum Kind { Null, Bool, Int, Float, Str, Arr, Obj } kind = Null;
    bool b = false;
    int64_t i = 0;
    bool big = false;  // integer outside int64
    double d = 0;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
};

struct JsonParser {
    const std::string& s;
    size_t p = 0;
    explicit JsonParser(const std::string& str) : s(str) {}
    static bool dig(char c) { return c >= '0' && c <= '9'; }
    [[noreturn]] void bad() { throw std::runtime_error(std::string(LAYOUT_KEY) + ": invalid JSON"); }
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
    }
    bool lit(const char* t) {
        size_t n = std::char_traits<char>::length(t);
        if (s.compare(p, n, t) == 0) {
            p += n;
            return true;
        }
        return false;
    }
    uint32_t hex4() {
        if (p + 4 > s.size()) bad();
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[p++];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) bad();
            v = v * 16 + static_cast<uint32_t>(d);
        }
        return v;
    }
    static void utf8(std::string& o, uint32_t c) {  // lone surrogates encoded as-is (never "pos")
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
    std::string str() {
        if (s[p] != '"') bad();
        p++;
        std::string out;
        for (;;) {
            if (p >= s.size()) bad();
            char c = s[p++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) bad();
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= s.size()) bad();
            char e = s[p++];
            switch (e) {
                case '"': case '\\': case '/': out += e; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && s.compare(p, 2, "\\u") == 0) {
                        size_t save = p;
                        p += 2;
                        uint32_t lo = hex4();
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else p = save;
                    }
                    utf8(out, cp);
                    break;
                }
                default: bad();
            }
        }
    }
    Json value(int depth = 0) {
        if (depth > 1000) bad();
        ws();
        if (p >= s.size()) bad();
        Json j;
        char c = s[p];
        if (c == '{') {
            j.kind = Json::Obj;
            p++;
            ws();
            if (p < s.size() && s[p] == '}') { p++; return j; }
            for (;;) {
                ws();
                if (p >= s.size()) bad();
                std::string k = str();
                ws();
                if (p >= s.size() || s[p] != ':') bad();
                p++;
                Json v = value(depth + 1);
                bool found = false;
                for (auto& kv : j.obj)
                    if (kv.first == k) { kv.second = v; found = true; }  // duplicate key: last wins
                if (!found) j.obj.emplace_back(k, v);
                ws();
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == '}') { p++; return j; }
                bad();
            }
        }
        if (c == '[') {
            j.kind = Json::Arr;
            p++;
            ws();
            if (p < s.size() && s[p] == ']') { p++; return j; }
            for (;;) {
                j.arr.push_back(value(depth + 1));
                ws();
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == ']') { p++; return j; }
                bad();
            }
        }
        if (c == '"') { j.kind = Json::Str; str(); return j; }
        if (lit("true")) { j.kind = Json::Bool; j.b = true; return j; }
        if (lit("false")) { j.kind = Json::Bool; return j; }
        if (lit("null")) return j;
        size_t st = p;
        if (s[p] == '-') p++;
        if (p >= s.size() || !dig(s[p])) bad();
        if (s[p] == '0') p++;
        else while (p < s.size() && dig(s[p])) p++;
        bool fl = false;
        if (p < s.size() && s[p] == '.') {
            fl = true;
            p++;
            if (p >= s.size() || !dig(s[p])) bad();
            while (p < s.size() && dig(s[p])) p++;
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
            fl = true;
            p++;
            if (p < s.size() && (s[p] == '+' || s[p] == '-')) p++;
            if (p >= s.size() || !dig(s[p])) bad();
            while (p < s.size() && dig(s[p])) p++;
        }
        std::string num = s.substr(st, p - st);
        if (fl) {
            j.kind = Json::Float;
            j.d = std::strtod(num.c_str(), nullptr);
        } else {
            j.kind = Json::Int;
            errno = 0;
            long long v = std::strtoll(num.c_str(), nullptr, 10);
            if (errno == ERANGE) j.big = true;
            j.i = v;
        }
        return j;
    }
};

// pos[i][k] * 8 + off as Pillow receives it (int, or a float truncated toward zero).
int64_t coord(const Json& v, int64_t off) {
    if (v.kind == Json::Bool) return (v.b ? 8 : 0) + off;
    if (v.kind == Json::Int) {
        if (v.big || v.i > (int64_t(1) << 40) || v.i < -(int64_t(1) << 40))
            throw std::out_of_range("image index out of range");
        return v.i * 8 + off;
    }
    if (v.kind == Json::Float) {
        double d = v.d * 8 + static_cast<double>(off);
        if (!(std::fabs(d) < 1e12)) throw std::out_of_range("image index out of range");
        return static_cast<int64_t>(d);  // truncates toward zero, like (int) in Pillow
    }
    throw std::runtime_error(std::string(LAYOUT_KEY) + ": a position is not a number pair");
}

std::string layout_json(int64_t w, int64_t rows, const std::vector<std::pair<int64_t, int64_t>>& pos) {
    std::string s = "{\"w\":" + itos(w) + ",\"rows\":" + itos(rows) + ",\"pos\":[";
    for (size_t i = 0; i < pos.size(); i++) {
        if (i) s += ',';
        s += '[' + itos(pos[i].first) + ',' + itos(pos[i].second) + ']';
    }
    return s + "]}";
}

}  // namespace

std::vector<Tile> tiles_to_indices(const Bytes& data) {
    std::vector<Tile> out(data.size() / 16);
    for (size_t t = 0; t < out.size(); t++)
        for (int row = 0; row < 8; row++) {
            unsigned w = data[t * 16 + row * 2] | (data[t * 16 + row * 2 + 1] << 8);
            for (int x = 0; x < 8; x++) out[t][row * 8 + x] = static_cast<uint8_t>((w >> (14 - 2 * x)) & 3);
        }
    return out;
}

Bytes indices_to_tiles(const std::vector<Tile>& tiles) {
    Bytes b;
    b.reserve(tiles.size() * 16);
    for (const Tile& px : tiles)
        for (int row = 0; row < 8; row++) {
            unsigned w = 0;
            for (int x = 0; x < 8; x++) w |= (px[row * 8 + x] & 3u) << (14 - 2 * x);
            b.push_back(static_cast<uint8_t>(w & 0xFF));
            b.push_back(static_cast<uint8_t>(w >> 8));
        }
    return b;
}

Rgb color(unsigned v) {
    return {static_cast<uint8_t>((v & 15) * 17), static_cast<uint8_t>(((v >> 4) & 15) * 17),
            static_cast<uint8_t>(((v >> 8) & 15) * 17)};
}

std::vector<Palette> read_palettes(const Bytes& data) {
    std::vector<unsigned> vals;
    for (size_t i = 0; i + 1 < data.size(); i += 2) vals.push_back(data[i] | (data[i + 1] << 8));
    std::vector<Palette> out;
    for (size_t i = 0; i < vals.size(); i += 4) {
        Palette p{};
        for (size_t k = 0; k < 4; k++) p[k] = i + k < vals.size() ? color(vals[i + k]) : Rgb{0, 0, 0};
        out.push_back(p);
    }
    return out;
}

TileMap read_map(const Bytes& data, const std::string& fmt, int64_t width) {
    TileMap m;
    Bytes body;
    if (fmt == "wh8") {
        if (data.size() < 2) throw std::out_of_range("index out of range");
        m.w = data[0];
        m.h = data[1];
        size_t end = std::min(data.size(), static_cast<size_t>(2 + 2 * m.w * m.h));
        body.assign(data.begin() + 2, data.begin() + static_cast<std::ptrdiff_t>(end));
    } else {
        if (!width) throw std::runtime_error("a raw map needs its width");
        m.w = width;
        body = data;
        m.h = floordiv(floordiv(static_cast<int64_t>(body.size()), 2), m.w);
    }
    int64_t cnt = m.w * m.h;
    for (int64_t i = 0; i < cnt; i++) {
        if (static_cast<size_t>(2 * i + 1) >= body.size())
            throw std::out_of_range("index out of range");
        m.entries.push_back(body[2 * i] | (body[2 * i + 1] << 8));
    }
    return m;
}

Layout layout(int64_t count, int64_t w, int64_t h, const std::vector<int64_t>& entries, int64_t base) {
    Layout L;
    size_t n_tiles = count > 0 ? static_cast<size_t>(count) : 0;
    std::vector<bool> set(n_tiles, false);
    L.pos.assign(n_tiles, {0, 0});
    L.pal.assign(n_tiles, 0);
    for (size_t i = 0; i < entries.size(); i++) {
        int64_t v = entries[i];
        int64_t c = v & 0x1FF;
        if (c == 0) continue;
        int64_t n = c - base - 1;
        if (0 <= n && n < count && !set[static_cast<size_t>(n)]) {
            int64_t x = pymod(static_cast<int64_t>(i), w);
            L.pos[static_cast<size_t>(n)] = {x, floordiv(static_cast<int64_t>(i), w)};
            L.pal[static_cast<size_t>(n)] = (v >> 9) & 15;
            set[static_cast<size_t>(n)] = true;
        }
    }
    int64_t k = 0;
    for (size_t n = 0; n < n_tiles; n++)
        if (!set[n]) {
            int64_t x = pymod(k, w);
            L.pos[n] = {x, h + floordiv(k, w)};
            k++;
        }
    L.rows = h + floordiv(k + w - 1, w);
    return L;
}

Bytes to_png_bytes(const Bytes& data, int64_t width_tiles, const std::vector<Palette>& palettes,
                   const std::optional<TileMap>& tile_map, int64_t base, int64_t pal_index) {
    std::vector<Tile> tiles = tiles_to_indices(data);
    const int64_t n = static_cast<int64_t>(tiles.size());
    std::vector<Palette> pals = palettes.empty() ? std::vector<Palette>{GREYS} : palettes;
    std::vector<std::pair<std::string, std::string>> text;
    std::vector<std::pair<int64_t, int64_t>> pos;
    std::vector<int64_t> pal;
    int64_t wpx, hpx;
    auto count_s = [&]() {
        return itos(int64_t(palettes.size())) + (palettes.size() == 1 ? " palette" : " palettes");
    };
    if (pal_index && palettes.empty()) throw std::runtime_error("--pal needs --palette");
    if (pal_index < 0 || (!tile_map && pal_index >= int64_t(pals.size())))
        throw std::runtime_error("--pal " + itos(pal_index) + ": the palette file has " + count_s() +
                                 (pals.size() == 1 ? " (only --pal 0)" : " (--pal 0 to " + itos(int64_t(pals.size()) - 1) + ")"));
    if (tile_map) {
        Layout L = layout(n, tile_map->w, tile_map->h, tile_map->entries, base);
        pos = L.pos;
        pal = palettes.empty() ? std::vector<int64_t>(static_cast<size_t>(n), 0) : L.pal;
        for (int64_t p : pal)
            if (p >= int64_t(pals.size()))
                throw std::runtime_error("the map uses palette " + itos(p) + ", but the palette file has " + count_s());
        text.emplace_back(LAYOUT_KEY, layout_json(tile_map->w, L.rows, pos));
        wpx = tile_map->w * 8;
        hpx = L.rows * 8;
    } else {
        int64_t w = n ? std::min(width_tiles, n) : 1;
        int64_t rows = floordiv(n + w - 1, w);
        for (int64_t i = 0; i < n; i++) pos.emplace_back(pymod(i, w), floordiv(i, w));
        pal.assign(static_cast<size_t>(n), palettes.empty() ? 0 : pal_index);
        wpx = w * 8;
        hpx = std::max<int64_t>(rows, 1) * 8;
    }
    if (wpx < 0 || hpx < 0) throw std::runtime_error("Width and height must be >= 0");
    if (wpx > 0x7FFFFFFF || hpx > 0x7FFFFFFF || (wpx && hpx > (int64_t(1) << 40) / wpx))
        throw std::runtime_error("image too large");
    if (pals.size() > 64) throw std::runtime_error("invalid palette size");
    Bytes px(static_cast<size_t>(wpx * hpx), 0);
    for (size_t i = 0; i < tiles.size(); i++) {
        int64_t tx = pos[i].first * 8, ty = pos[i].second * 8;
        int64_t off = pal[i] < static_cast<int64_t>(pals.size()) ? 4 * pal[i] : 0;
        for (int k = 0; k < 64; k++) {
            int64_t v = off + tiles[i][static_cast<size_t>(k)];
            px[pil_index(tx + k % 8, ty + k / 8, wpx, hpx)] =
                static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);  // Pillow clips to 0..255
        }
    }
    if (wpx == 0 || hpx == 0) throw std::runtime_error("cannot write empty image");
    std::vector<Rgb> plte;
    for (const Palette& p : pals) plte.insert(plte.end(), p.begin(), p.end());
    unsigned maxv = 0;
    for (uint8_t v : px) maxv = v > maxv ? v : maxv;
    while (plte.size() < maxv + 1u) plte.push_back({0, 0, 0});
    return png_write_indexed(static_cast<uint32_t>(wpx), static_cast<uint32_t>(hpx), px, plte, text);
}

size_t to_png(const Bytes& data, const std::string& path, int64_t width_tiles, const std::vector<Palette>& palettes,
              const std::optional<TileMap>& tile_map, int64_t base, int64_t pal_index) {
    Bytes png = to_png_bytes(data, width_tiles, palettes, tile_map, base, pal_index);
    // os.path.splitext: the extension starts at the last dot of the last component, not a leading dot
    size_t slash = path.find_last_of("/\\");
    size_t start = slash == std::string::npos ? 0 : slash + 1;
    while (start < path.size() && path[start] == '.') start++;
    size_t dot = path.find_last_of('.');
    std::string ext = dot != std::string::npos && dot >= start ? path.substr(dot) : "";
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (ext != ".png" && ext != ".apng") throw std::runtime_error("unknown file extension: " + ext);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + path);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!f) throw std::runtime_error("cannot write " + path);
    return data.size() / 16;
}

Bytes from_png_bytes(const Bytes& file, const std::string& path, int64_t count,
                     const std::optional<TileMap>& tile_map, int64_t base) {
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (file.size() < 8 || !std::equal(SIG, SIG + 8, file.begin()))
        throw std::runtime_error("cannot identify image file '" + path + "'");
    PngImage head = png_read(file, false);
    if (head.color_type != 3) throw std::runtime_error(path + ": expected an indexed (palette) PNG");
    const int64_t W = head.width, H = head.height;
    size_t ncount = count > 0 ? static_cast<size_t>(count) : 0;
    std::vector<std::pair<int64_t, int64_t>> ipos;   // integer positions (map / grid)
    Json root;
    std::vector<const Json*> jpos;                   // positions from the text chunk
    const PngText* lay = nullptr;
    for (const PngText& t : head.text)
        if (t.before_idat && t.key == LAYOUT_KEY) lay = &t;  // the last one wins (dict update)
    if (tile_map) {
        ipos = layout(count, tile_map->w, tile_map->h, tile_map->entries, base).pos;
    } else if (lay) {
        JsonParser jp(lay->value);
        root = jp.value();
        jp.ws();
        if (jp.p != lay->value.size()) jp.bad();
        if (root.kind != Json::Obj) throw std::runtime_error(std::string(LAYOUT_KEY) + ": not a JSON object");
        const Json* pv = nullptr;
        for (auto& kv : root.obj)
            if (kv.first == "pos") pv = &kv.second;
        if (!pv) throw std::runtime_error("'pos'");  // KeyError
        if (pv->kind != Json::Arr) throw std::runtime_error(std::string(LAYOUT_KEY) + ": pos is not a list");
        for (const Json& p : pv->arr) {
            if (p.kind != Json::Arr) throw std::runtime_error(std::string(LAYOUT_KEY) + ": a position is not a list");
            if (jpos.size() < ncount) jpos.push_back(&p);
        }
    } else {
        int64_t w = W / 8;
        for (size_t i = 0; i < ncount; i++) {
            int64_t x = pymod(static_cast<int64_t>(i), w);
            ipos.emplace_back(x, floordiv(static_cast<int64_t>(i), w));
        }
    }
    PngImage img = png_read(file, true);
    std::vector<Tile> tiles(ncount);
    for (size_t i = 0; i < ncount; i++) {
        if (lay && !tile_map) {
            if (i >= jpos.size()) throw std::out_of_range("list index out of range");
            const std::vector<Json>& p = jpos[i]->arr;
            if (p.empty()) throw std::out_of_range("tuple index out of range");
            coord(p[0], 0);  // pos[i][0] * 8 is evaluated before pos[i][1]
            if (p.size() < 2) throw std::out_of_range("tuple index out of range");
            for (int k = 0; k < 64; k++) {
                int64_t x = coord(p[0], k % 8), y = coord(p[1], k / 8);
                tiles[i][static_cast<size_t>(k)] = img.pixels[pil_index(x, y, W, H)] & 3;
            }
        } else {
            int64_t tx = ipos[i].first * 8, ty = ipos[i].second * 8;
            for (int k = 0; k < 64; k++)
                tiles[i][static_cast<size_t>(k)] = img.pixels[pil_index(tx + k % 8, ty + k / 8, W, H)] & 3;
        }
    }
    return indices_to_tiles(tiles);
}

Bytes from_png(const std::string& path, int64_t count, const std::optional<TileMap>& tile_map, int64_t base) {
    return from_png_bytes(read_file(path), path, count, tile_map, base);
}

namespace {

// The signature and color-type checks from_png_bytes makes, for the functions below.
PngImage read_indexed(const Bytes& file, const std::string& path, bool decode) {
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (file.size() < 8 || !std::equal(SIG, SIG + 8, file.begin()))
        throw std::runtime_error("cannot identify image file '" + path + "'");
    PngImage head = png_read(file, false);
    if (head.color_type != 3) throw std::runtime_error(path + ": expected an indexed (palette) PNG");
    return decode ? png_read(file, true) : head;
}

}  // namespace

int64_t from_png_count(const Bytes& file, const std::string& path, bool trim) {
    PngImage head = read_indexed(file, path, false);
    const PngText* lay = nullptr;
    for (const PngText& t : head.text)
        if (t.before_idat && t.key == LAYOUT_KEY) lay = &t;
    if (lay) {
        if (trim)
            throw std::runtime_error(path + ": --trim works on a plain sheet only (this PNG has a " +
                                     std::string(LAYOUT_KEY) + " chunk)");
        JsonParser jp(lay->value);
        Json root = jp.value();
        jp.ws();
        if (jp.p != lay->value.size()) jp.bad();
        if (root.kind != Json::Obj) throw std::runtime_error(std::string(LAYOUT_KEY) + ": not a JSON object");
        for (auto& kv : root.obj)
            if (kv.first == "pos") {
                if (kv.second.kind != Json::Arr) throw std::runtime_error(std::string(LAYOUT_KEY) + ": pos is not a list");
                return static_cast<int64_t>(kv.second.arr.size());
            }
        throw std::runtime_error("'pos'");
    }
    const int64_t W = head.width, H = head.height, tw = W / 8, th = H / 8;
    int64_t n = tw * th;
    if (!trim || n == 0) return n;
    PngImage img = read_indexed(file, path, true);
    auto blank = [&](int64_t i) {
        int64_t tx = (i % tw) * 8, ty = (i / tw) * 8;
        for (int64_t y = 0; y < 8; y++)
            for (int64_t x = 0; x < 8; x++)
                if (img.pixels[static_cast<size_t>((ty + y) * W + tx + x)] != 0) return false;
        return true;
    };
    // only cells of the last row go, and that row's first cell always stays
    while (n > tw * (th - 1) + 1 && blank(n - 1)) n--;
    return n;
}

Bytes png_indices(const Bytes& file, const std::string& path, uint32_t& width, uint32_t& height) {
    PngImage img = read_indexed(file, path, true);
    width = img.width;
    height = img.height;
    return img.pixels;
}

Bytes png_palette_words(const Bytes& file, const std::string& path, int64_t colors) {
    PngImage head = read_indexed(file, path, false);
    const int64_t have = static_cast<int64_t>(head.palette.size());
    if (have < colors)
        throw std::runtime_error(path + ": the palette has " + itos(have) + " colors, " + itos(colors) + " are needed");
    // the nearest 4-bit level of each channel, round(c / 17); c / 17 is never exactly halfway
    auto level = [](uint8_t v) { return static_cast<unsigned>((v + 8) / 17); };
    Bytes out;
    for (int64_t k = 0; k < colors; k++) {
        const Rgb& c = head.palette[static_cast<size_t>(k)];
        unsigned w = level(c[0]) | level(c[1]) << 4 | level(c[2]) << 8;   // 0x0BGR
        out.push_back(static_cast<uint8_t>(w & 0xFF));
        out.push_back(static_cast<uint8_t>(w >> 8));
    }
    return out;
}

}  // namespace unitron
