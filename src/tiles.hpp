// K2GE 2bpp tiles and palettes <-> indexed PNG, plain sheets or laid out by a tile map.
// Port of legacy/python/unitron/gfx/tiles.py (pixel semantics as Pillow sees them).
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bytes.hpp"
#include "png.hpp"

namespace unitron {

using Tile = std::array<uint8_t, 64>;   // pixel values 0..3, row-major
using Palette = std::array<Rgb, 4>;

extern const Palette GREYS;
extern const char* const LAYOUT_KEY;   // "unitron-layout"

struct TileMap {
    int64_t w = 0, h = 0;
    std::vector<int64_t> entries;
};

struct Layout {
    std::vector<std::pair<int64_t, int64_t>> pos;   // cell (x, y) per sheet tile
    std::vector<int64_t> pal;                       // palette per sheet tile
    int64_t rows = 0;
};

std::vector<Tile> tiles_to_indices(const Bytes& data);
Bytes indices_to_tiles(const std::vector<Tile>& tiles);
Rgb color(unsigned v);                                   // u16 0x0BGR -> 8-bit RGB
std::vector<Palette> read_palettes(const Bytes& data);

// fmt "wh8" (width byte, height byte, entries) or anything else = raw (width needed; 0 = None,
// which throws std::runtime_error("a raw map needs its width")). Short data throws
// std::out_of_range("index out of range") like Python's IndexError.
TileMap read_map(const Bytes& data, const std::string& fmt = "wh8", int64_t width = 0);

// w == 0 throws std::runtime_error with Python's ZeroDivisionError text.
Layout layout(int64_t count, int64_t w, int64_t h, const std::vector<int64_t>& entries, int64_t base = 0);

// An empty `palettes` means None (Python tests it for truth). Returns the number of tiles.
// to_png writes `path`; only the .png/.apng extensions are accepted (Pillow picks the format
// from the extension): others throw std::runtime_error("unknown file extension: <ext>").
size_t to_png(const Bytes& data, const std::string& path, int64_t width_tiles = 16,
              const std::vector<Palette>& palettes = {}, const std::optional<TileMap>& tile_map = std::nullopt,
              int64_t base = 0, int64_t pal_index = 0);
// The same image as PNG file bytes (no extension check, nothing written).
Bytes to_png_bytes(const Bytes& data, int64_t width_tiles = 16, const std::vector<Palette>& palettes = {},
                   const std::optional<TileMap>& tile_map = std::nullopt, int64_t base = 0,
                   int64_t pal_index = 0);

// Read `count` tiles back. Non-palette PNGs throw std::runtime_error("{path}: expected an
// indexed (palette) PNG"); a pixel outside the image throws std::out_of_range("image index out
// of range"); a non-PNG file throws std::runtime_error("cannot identify image file '{path}'").
Bytes from_png(const std::string& path, int64_t count, const std::optional<TileMap>& tile_map = std::nullopt,
               int64_t base = 0);
// The same from PNG file bytes; path is only used in messages.
Bytes from_png_bytes(const Bytes& png, const std::string& path, int64_t count,
                     const std::optional<TileMap>& tile_map = std::nullopt, int64_t base = 0);

// How many tiles `gfx frompng` reads when --count is not given. A plain sheet: every 8x8 cell,
// row-major ((width / 8) * (height / 8)). With trim: less the blank cells (every pixel palette
// index 0) at the end of the last row of cells; that row's first cell always stays. A PNG with a
// unitron-layout chunk: every position in it (trim is an error there).
int64_t from_png_count(const Bytes& file, const std::string& path, bool trim);
// Every pixel's palette index, one byte each, row-major; width and height are set.
Bytes png_indices(const Bytes& file, const std::string& path, uint32_t& width, uint32_t& height);
// The first `colors` PLTE entries as little-endian 0x0BGR words, 4 bits per channel (nearest
// level, round(c / 17)). Fewer entries than `colors` is an error.
Bytes png_palette_words(const Bytes& file, const std::string& path, int64_t colors);

}  // namespace unitron
