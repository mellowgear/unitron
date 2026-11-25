"""LZSS, tiles/palettes and the positioned tile-sheet layout."""
import random

from unitron.codec import lzss
from unitron.data.bin2db import to_db
from unitron.gfx import tiles as T
from helpers import asm


def test_lzss_roundtrip():
    rnd = random.Random(1)
    for n in (0, 1, 17, 18, 19, 300, 5000):
        raw = bytes(rnd.choice(b"\x00\x00\x00\x11\x22\xff") for _ in range(n))
        for clamp in (True, False):
            c = lzss.compress_okumura(raw, clamp=clamp)
            out, used = lzss.decompress(c, n)
            assert out == raw
            if clamp:
                assert used == len(c)


def test_lzss_decoder_layout():
    # flags 0b01: a literal 'A' (written at ring 0x3EE), then a match at 0x3EE of length 3
    out, used = lzss.decompress(bytes([0x01, 0x41, 0xEE, 0x30]), 4)
    assert out == b"AAAA" and used == 4
    assert lzss.compress_okumura(b"AAAA") == bytes([0x01, 0x41, 0xEE, 0x30])


def test_tile_format():
    # row 0: pixel 0 = 3, pixel 7 = 1 -> u16 0xC001 little-endian
    px = [3, 0, 0, 0, 0, 0, 0, 1] + [0] * 56
    b = T.indices_to_tiles([px])
    assert b[:2] == bytes([0x01, 0xC0])
    assert T.tiles_to_indices(b)[0] == px
    assert T.color(0x0F80) == (0, 0x88, 0xFF)


def _sheet(n, seed=2):
    rnd = random.Random(seed)
    return bytes(rnd.randrange(256) for _ in range(16 * n))


def test_positioned_layout_roundtrip(tmp_path):
    n = 7
    data = _sheet(n)
    # 4x3 map: tiles 0..4 used (2 repeated, 3 mirrored), 5 and 6 unused; 0 = empty cell
    ents = [1, 2, 0, 3 | 0x0200,
            2, 4, 4 | 0x8000, 0,
            5 | 0x4400, 1, 0, 0]
    tm = (4, 3, ents)
    pos, pal, rows = T.layout(n, *tm)
    assert pos[:5] == [(0, 0), (1, 0), (3, 0), (1, 1), (0, 2)]
    assert pos[5:] == [(0, 3), (1, 3)] and rows == 4
    assert pal[2] == 1 and pal[4] == 2
    p = tmp_path / "m.png"
    pals = T.read_palettes(bytes(range(64)) * 2)
    T.to_png(data, p, palettes=pals, tile_map=tm)
    assert T.from_png(p, n, tile_map=tm) == data          # positions from the map
    assert T.from_png(p, n) == data                       # positions from the PNG text chunk
    from PIL import Image
    img = Image.open(p)
    assert img.size == (32, 32)
    assert img.getpixel((2 * 8, 0)) == 0                  # empty cell stays blank


def test_plain_sheet(tmp_path):
    data = _sheet(5)
    p = tmp_path / "s.png"
    T.to_png(data, p, width_tiles=2)
    assert T.from_png(p, 5) == data


def test_bin2db_assembles():
    text = to_db(bytes(range(20)), "Blob")
    a, img = asm("$maximum\nd\tsection\tromdata abs=0x200000\n" + text + "\tend\n", origin=0x200000)
    assert a.d.errors == [] and img == bytes(range(20))


def test_charmap_text(tmp_path):
    from unitron.data.charmap import Charmap, text_to_db
    cm = tmp_path / "c.txt"
    cm.write_text('char " " 00\nchar "a" 01\nchar "ab" 7F\ncmd text - string\ncmd line FE string\ncmd done FF\n')
    out = text_to_db(Charmap(cm), 'Hello:\n    text "ab a"\n    line "{12}"\n    done\n')
    assert "Hello:" in out
    assert '\tdb\t0x7F,0x00,0x01\t; text "ab a"' in out and "\tdb\t0xFE,0x12" in out and "\tdb\t0xFF" in out
