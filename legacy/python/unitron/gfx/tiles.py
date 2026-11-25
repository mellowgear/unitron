"""K2GE 2bpp tiles and palettes <-> indexed PNG, plain sheets or laid out by a tile map.

Tile: 8x8, 16 bytes, each row a little-endian u16 with pixel 0 in bits 15:14 (SNK K2GETechRef
§4-3-5-1). Palette entry: u16 0x0BGR, 4 bits per channel (§4-15). Scroll map entry: character
code bits 8:0, palette bits 12:9, V flip bit 14, H flip bit 15 (§4-4-4).

Positioned layout (`--map`): the PNG is the size of the map. Each sheet tile is drawn once, at
the first map cell that uses it, in its stored orientation; cells that repeat or mirror an earlier
tile stay blank; tiles the map never uses go in extra rows below. The tile positions are derived
again from the map when reading back, and also stored in a PNG text chunk.
"""
import json
from pathlib import Path

GREYS = [(255, 255, 255), (170, 170, 170), (85, 85, 85), (0, 0, 0)]
LAYOUT_KEY = "unitron-layout"


def tiles_to_indices(data):
    """bytes -> list of tiles, each 64 pixel values 0..3."""
    out = []
    for t in range(len(data) // 16):
        px = []
        for row in range(8):
            w = data[t * 16 + row * 2] | data[t * 16 + row * 2 + 1] << 8
            px += [(w >> (14 - 2 * x)) & 3 for x in range(8)]
        out.append(px)
    return out


def indices_to_tiles(tiles):
    b = bytearray()
    for px in tiles:
        for row in range(8):
            w = 0
            for x in range(8):
                w |= (px[row * 8 + x] & 3) << (14 - 2 * x)
            b += bytes([w & 0xFF, w >> 8])
    return bytes(b)


def color(v):
    """u16 0x0BGR -> (r, g, b) 8-bit."""
    return ((v & 15) * 17, ((v >> 4) & 15) * 17, ((v >> 8) & 15) * 17)


def read_palettes(data):
    """Raw u16 palette entries -> list of palettes of 4 (r, g, b)."""
    vals = [data[i] | data[i + 1] << 8 for i in range(0, len(data) - 1, 2)]
    return [[color(v) for v in vals[i:i + 4]] + [(0, 0, 0)] * (4 - len(vals[i:i + 4]))
            for i in range(0, len(vals), 4)]


def read_map(data, fmt="wh8", width=None):
    """-> (width, height, entries). fmt 'wh8': a width byte and a height byte, then entries;
    'raw': entries only (width needed)."""
    if fmt == "wh8":
        w, h = data[0], data[1]
        body = data[2:2 + 2 * w * h]
    else:
        if not width:
            raise ValueError("a raw map needs its width")
        w = width
        body = data
        h = len(body) // 2 // w
    ents = [body[2 * i] | body[2 * i + 1] << 8 for i in range(w * h)]
    return w, h, ents


def layout(count, w, h, entries, base=0):
    """Cell (x, y) per sheet tile and the palette it is drawn with: first map use, then extra rows."""
    pos = [None] * count
    pal = [0] * count
    for i, v in enumerate(entries):
        c = v & 0x1FF
        if c == 0:
            continue
        n = c - base - 1
        if 0 <= n < count and pos[n] is None:
            pos[n] = (i % w, i // w)
            pal[n] = (v >> 9) & 15
    k = 0
    for n in range(count):
        if pos[n] is None:
            pos[n] = (k % w, h + k // w)
            k += 1
    rows = h + (k + w - 1) // w
    return pos, pal, rows


def _image(width_px, height_px, palettes):
    from PIL import Image
    img = Image.new("P", (width_px, height_px), 0)
    flat = [c for p in palettes for rgb in p for c in rgb]
    img.putpalette(flat + [0] * (768 - len(flat)))
    return img


def to_png(data, path, width_tiles=16, palettes=None, tile_map=None, base=0, pal_index=0):
    """Write tiles as an indexed PNG. tile_map = (w, h, entries) for the positioned layout."""
    from PIL.PngImagePlugin import PngInfo
    tiles = tiles_to_indices(data)
    n = len(tiles)
    info = PngInfo()
    pals = palettes or [GREYS]
    if tile_map:
        w, h, ents = tile_map
        pos, pal, rows = layout(n, w, h, ents, base)
        if not palettes:
            pal = [0] * n
        info.add_text(LAYOUT_KEY, json.dumps({"w": w, "rows": rows, "pos": pos}, separators=(",", ":")))
        img = _image(w * 8, rows * 8, pals)
    else:
        w = min(width_tiles, n) if n else 1
        rows = (n + w - 1) // w
        pos = [(i % w, i // w) for i in range(n)]
        pal = [pal_index if palettes else 0] * n
        img = _image(w * 8, max(rows, 1) * 8, pals)
    px = img.load()
    for i, t in enumerate(tiles):
        tx, ty = pos[i][0] * 8, pos[i][1] * 8
        off = 4 * pal[i] if pal[i] < len(pals) else 0
        for k, v in enumerate(t):
            px[tx + k % 8, ty + k // 8] = off + v
    img.save(path, optimize=True, pnginfo=info)
    return n


def from_png(path, count, tile_map=None, base=0):
    """Read `count` tiles back from a PNG written by to_png."""
    from PIL import Image
    img = Image.open(path)
    if img.mode != "P":
        raise ValueError(f"{path}: expected an indexed (palette) PNG")
    if tile_map:
        w, h, ents = tile_map
        pos, _, _ = layout(count, w, h, ents, base)
    elif LAYOUT_KEY in img.info:
        pos = [tuple(p) for p in json.loads(img.info[LAYOUT_KEY])["pos"]][:count]
    else:
        w = img.width // 8
        pos = [(i % w, i // w) for i in range(count)]
    px = img.load()
    tiles = []
    for i in range(count):
        tx, ty = pos[i][0] * 8, pos[i][1] * 8
        tiles.append([px[tx + k % 8, ty + k // 8] & 3 for k in range(64)])
    return indices_to_tiles(tiles)


def _map_arg(a):
    if not getattr(a, "map", None):
        return None
    return read_map(Path(a.map).read_bytes(), a.map_format, a.map_width)


def cli(a):
    tm = _map_arg(a)
    pals = read_palettes(Path(a.palette).read_bytes()) if getattr(a, "palette", None) else None
    if a.gcmd == "topng":
        data = Path(a.input).read_bytes()
        n = to_png(data, a.output, a.width, pals, tm, a.base, a.pal)
        print(f"wrote {a.output}: {n} tiles")
    else:
        data = from_png(a.input, a.count, tm, a.base)
        Path(a.output).write_bytes(data)
        print(f"wrote {a.output}: {a.count} tiles")
    return 0


def add_parser(sp, fn):
    p = sp.add_parser("gfx", help="2bpp tiles <-> indexed PNG (plain sheet or map layout)")
    g = p.add_subparsers(dest="gcmd", required=True)
    for name in ("topng", "frompng"):
        q = g.add_parser(name)
        q.add_argument("input")
        q.add_argument("output")
        q.add_argument("--map", help="tile map file: lay the sheet out as the map shows it")
        q.add_argument("--map-format", choices=["wh8", "raw"], default="wh8",
                       help="wh8: width byte, height byte, entries (default); raw: entries only")
        q.add_argument("--map-width", type=int)
        q.add_argument("--base", type=lambda s: int(s, 0), default=0,
                       help="sheet tile n is character code base+n+1 in the map")
        if name == "topng":
            q.add_argument("--width", type=int, default=16, help="tiles per row (plain sheet)")
            q.add_argument("--palette", help="raw u16 0x0BGR entries, 4 per palette")
            q.add_argument("--pal", type=int, default=0, help="palette for a plain sheet")
        else:
            q.add_argument("--count", type=int, required=True)
        q.set_defaults(fn=fn)
