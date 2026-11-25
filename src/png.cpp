#include "png.hpp"

#include <png.h>

#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "zlib.hpp"

namespace unitron {
namespace {

[[noreturn]] void fail(const std::string& why) { throw std::runtime_error("PNG: " + why); }

std::string latin1_to_utf8(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else {
            s += static_cast<char>(0xC0 | (c >> 6));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// Strict UTF-8 check (as bytes.decode("utf-8", "strict")).
bool valid_utf8(const uint8_t* p, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        int more;
        uint8_t lo = 0x80, hi = 0xBF;
        if (c < 0x80) { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) more = 1;
        else if (c == 0xE0) { more = 2; lo = 0xA0; }
        else if (c == 0xED) { more = 2; hi = 0x9F; }
        else if (c >= 0xE1 && c <= 0xEF) more = 2;
        else if (c == 0xF0) { more = 3; lo = 0x90; }
        else if (c == 0xF4) { more = 3; hi = 0x8F; }
        else if (c >= 0xF1 && c <= 0xF3) more = 3;
        else return false;
        for (int k = 1; k <= more; k++) {
            if (i + k >= n) return false;
            uint8_t d = p[i + k];
            if (d < (k == 1 ? lo : 0x80) || d > (k == 1 ? hi : 0xBF)) return false;
        }
        i += more + 1;
    }
    return true;
}

size_t find_nul(const Bytes& b, size_t from) {
    for (size_t i = from; i < b.size(); i++)
        if (b[i] == 0) return i;
    return std::string::npos;
}

// Pillow's PngStream.chunk_tEXt / chunk_zTXt / chunk_iTXt.
void text_chunk(PngImage& img, const std::string& type, const Bytes& d, bool before_idat) {
    if (type == "tEXt" || type == "zTXt") {
        size_t z = find_nul(d, 0);
        size_t klen = z == std::string::npos ? d.size() : z;
        Bytes v;
        if (z != std::string::npos) v.assign(d.begin() + static_cast<std::ptrdiff_t>(z + 1), d.end());
        if (type == "zTXt") {
            int method = v.empty() ? 0 : v[0];
            if (method != 0) fail("unknown compression method in zTXt chunk");
            if (!v.empty()) v = zlib_decompress(v.data() + 1, v.size() - 1);
            else v = zlib_decompress(v.data(), 0);
        }
        if (klen == 0) return;
        img.text.push_back({type, latin1_to_utf8(d.data(), klen), latin1_to_utf8(v.data(), v.size()), before_idat});
        return;
    }
    // iTXt: key \0 flag method lang \0 translated-key \0 text; malformed ones are ignored.
    size_t z = find_nul(d, 0);
    if (z == std::string::npos) return;
    size_t r = z + 1;
    if (d.size() - r < 2) return;
    uint8_t cf = d[r], cm = d[r + 1];
    size_t z2 = find_nul(d, r + 2);
    if (z2 == std::string::npos) return;
    size_t z3 = find_nul(d, z2 + 1);
    if (z3 == std::string::npos) return;
    Bytes v(d.begin() + static_cast<std::ptrdiff_t>(z3 + 1), d.end());
    if (cf) {
        if (cm != 0) return;
        try {
            v = zlib_decompress(v);
        } catch (const std::runtime_error&) {
            return;
        }
    }
    if (!valid_utf8(d.data() + r + 2, z2 - (r + 2)) || !valid_utf8(d.data() + z2 + 1, z3 - (z2 + 1)) ||
        !valid_utf8(v.data(), v.size()))
        return;
    img.text.push_back({type, latin1_to_utf8(d.data(), z), std::string(v.begin(), v.end()), before_idat});
}

// libpng is C: errors longjmp back to the setjmp in run_read / run_write. The frames in between
// are libpng's and our callbacks', which hold no objects with destructors when they call
// png_error; all C++ state lives in the context structs, owned by the callers of run_*.
struct ReadCtx {
    const Bytes* file = nullptr;
    size_t pos = 0;
    PngImage* img = nullptr;
    bool decode = false;
    bool after_idat = false;   // png_read_info has returned (it stops at the first IDAT)
    bool image_done = false;   // all rows read; only png_read_end is left
    bool eof_after_image = false;
    std::string error;
    bool error_is_whole = false;  // error is a complete message (from text_chunk), not libpng's
    std::vector<png_bytep> rows;
};

void on_error(png_structp png, png_const_charp msg) {
    auto* err = static_cast<std::string*>(png_get_error_ptr(png));
    if (err->empty()) *err = msg ? msg : "error";
    png_longjmp(png, 1);
}

void on_warning(png_structp, png_const_charp) {}

void read_data(png_structp png, png_bytep out, size_t len) {
    auto* c = static_cast<ReadCtx*>(png_get_io_ptr(png));
    if (c->file->size() - c->pos < len) {
        if (c->image_done) c->eof_after_image = true;  // a missing IEND is tolerated (as Pillow)
        png_error(png, "image file is truncated");
    }
    std::memcpy(out, c->file->data() + c->pos, len);
    c->pos += len;
}

// Returns false (with c.error set) on a chunk Pillow rejects.
bool handle_text(ReadCtx& c, const png_unknown_chunk& ch) {
    try {
        std::string type(reinterpret_cast<const char*>(ch.name), 4);
        text_chunk(*c.img, type, Bytes(ch.data, ch.data + ch.size), !c.after_idat);
        return true;
    } catch (const std::exception& e) {
        c.error = e.what();
        c.error_is_whole = true;
        return false;
    }
}

int read_chunk(png_structp png, png_unknown_chunkp ch) {
    auto* c = static_cast<ReadCtx*>(png_get_user_chunk_ptr(png));
    if (!handle_text(*c, *ch)) png_error(png, "text chunk");  // c->error is kept
    return 1;
}

// The text chunks are handed to read_chunk as unknown chunks so they keep Pillow's semantics
// (text_chunk) instead of libpng's keyword checks.
const png_byte TEXT_CHUNKS[] = "tEXt\0zTXt\0iTXt";

bool run_read(ReadCtx& c) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &c.error, on_error, on_warning);
    if (!png) {
        c.error = "out of memory";
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        c.error = "out of memory";
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return c.eof_after_image;
    }
    png_set_read_fn(png, &c, read_data);
    png_set_crc_action(png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_set_user_limits(png, 0x7FFFFFFF, 0x7FFFFFFF);
    png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_ALWAYS, TEXT_CHUNKS, 3);
    png_set_read_user_chunk_fn(png, &c, read_chunk);
    png_read_info(png, info);
    c.after_idat = true;
    PngImage& img = *c.img;
    int interlace = 0;
    png_get_IHDR(png, info, &img.width, &img.height, &img.bit_depth, &img.color_type, &interlace, nullptr,
                 nullptr);
    img.interlace = interlace;
    png_colorp plte = nullptr;
    int n = 0;
    if (png_get_PLTE(png, info, &plte, &n) & PNG_INFO_PLTE)
        for (int i = 0; i < n; i++) img.palette.push_back({plte[i].red, plte[i].green, plte[i].blue});
    if (img.color_type == PNG_COLOR_TYPE_PALETTE && c.decode) {
        if (interlace != PNG_INTERLACE_NONE) {
            c.error = "interlaced PNGs are not supported";
            png_error(png, "interlaced");
        }
        if (img.width > SIZE_MAX / img.height) png_error(png, "image too large");
        png_set_packing(png);  // bit depths 1/2/4: one index per byte
        png_read_update_info(png, info);
        img.pixels.resize(static_cast<size_t>(img.width) * img.height);
        c.rows.resize(img.height);
        for (size_t y = 0; y < img.height; y++) c.rows[y] = &img.pixels[y * img.width];
        png_read_image(png, c.rows.data());
        c.image_done = true;
        png_read_end(png, nullptr);
    }
    png_destroy_read_struct(&png, &info, nullptr);
    return true;
}

struct WriteCtx {
    Bytes out;
    std::string error;
    std::vector<png_bytep> rows;
    std::vector<png_color> plte;
    std::vector<png_text> text;
};

void write_data(png_structp png, png_bytep data, size_t len) {
    auto* c = static_cast<WriteCtx*>(png_get_io_ptr(png));
    c->out.insert(c->out.end(), data, data + len);
}

void flush_data(png_structp) {}

bool run_write(WriteCtx& c, uint32_t width, uint32_t height) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, &c.error, on_error, on_warning);
    if (!png) {
        c.error = "out of memory";
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        c.error = "out of memory";
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_set_write_fn(png, &c, write_data, flush_data);
    png_set_user_limits(png, 0x7FFFFFFF, 0x7FFFFFFF);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_PLTE(png, info, c.plte.data(), static_cast<int>(c.plte.size()));
    if (!c.text.empty()) png_set_text(png, info, c.text.data(), static_cast<int>(c.text.size()));
    png_write_info(png, info);  // IHDR, PLTE, tEXt
    png_write_image(png, c.rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return true;
}

}  // namespace

PngImage png_read(const Bytes& f, bool decode) {
    if (f.size() < 8 || png_sig_cmp(f.data(), 0, 8) != 0) fail("not a PNG file");
    PngImage img;
    ReadCtx c;
    c.file = &f;
    c.img = &img;
    c.decode = decode;
    if (!run_read(c)) {
        if (c.error_is_whole) throw std::runtime_error(c.error);
        fail(c.error);
    }
    return img;
}

Bytes png_write_indexed(uint32_t width, uint32_t height, const Bytes& pixels, const std::vector<Rgb>& palette,
                        const std::vector<std::pair<std::string, std::string>>& text) {
    if (width == 0 || height == 0 || width > 0x7FFFFFFFu || height > 0x7FFFFFFFu)
        throw std::invalid_argument("PNG: bad image size");
    if (pixels.size() != static_cast<size_t>(width) * height) throw std::invalid_argument("PNG: pixel count");
    if (palette.empty() || palette.size() > 256) throw std::invalid_argument("PNG: palette needs 1-256 entries");
    WriteCtx c;
    for (const Rgb& p : palette) c.plte.push_back({p[0], p[1], p[2]});
    for (const auto& kv : text) {
        if (kv.first.empty() || kv.first.size() > 79 || kv.first.find('\0') != std::string::npos)
            throw std::invalid_argument("PNG: bad tEXt key");
        png_text t{};
        t.compression = PNG_TEXT_COMPRESSION_NONE;
        t.key = const_cast<char*>(kv.first.c_str());
        t.text = const_cast<char*>(kv.second.c_str());  // libpng stops at a NUL
        c.text.push_back(t);
    }
    c.rows.resize(height);
    for (size_t y = 0; y < height; y++) c.rows[y] = const_cast<png_bytep>(&pixels[y * width]);
    if (!run_write(c, width, height)) fail(c.error);
    return std::move(c.out);
}

}  // namespace unitron
