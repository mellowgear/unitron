// PNG for indexed images, on top of libpng: write color type 3 / bit depth 8; read any
// non-interlaced PNG's chunks and decode the pixels of palette images (bit depth 1/2/4/8).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "bytes.hpp"

namespace unitron {

using Rgb = std::array<uint8_t, 3>;

struct PngText {
    std::string chunk;   // "tEXt", "zTXt" or "iTXt"
    std::string key;     // UTF-8 (decoded from latin-1)
    std::string value;   // UTF-8 (tEXt/zTXt decoded from latin-1, iTXt as stored)
    bool before_idat;    // Pillow's Image.open() only puts these in img.info before load()
};

struct PngImage {
    uint32_t width = 0, height = 0;
    int bit_depth = 0, color_type = 0, interlace = 0;
    std::vector<Rgb> palette;     // PLTE entries
    Bytes pixels;                 // color type 3 only: one palette index per pixel, row-major
    std::vector<PngText> text;    // in file order; Pillow semantics (empty keys dropped, etc.)
};

// Parse a PNG with libpng (signature, chunk CRCs, IHDR). For color type 3 (and decode=true)
// it decodes the image into `pixels` and reads on to IEND; otherwise it stops at the first
// IDAT (pixels empty, only the text chunks before IDAT) so callers can inspect or reject it.
// Interlaced palette images are rejected. Throws std::runtime_error; a bad signature gives
// "PNG: not a PNG file".
PngImage png_read(const Bytes& file, bool decode = true);

// Write an 8-bit palette PNG: IHDR, PLTE, one tEXt per (key, text) pair (both latin-1 bytes,
// key 1-79 bytes, no NUL), IDAT, IEND (written by libpng). pixels.size() must be width*height and
// palette 1..256 entries. Throws std::invalid_argument on bad arguments.
Bytes png_write_indexed(uint32_t width, uint32_t height, const Bytes& pixels,
                        const std::vector<Rgb>& palette,
                        const std::vector<std::pair<std::string, std::string>>& text = {});

}  // namespace unitron
