"""Subcommands besides asm/mac: dis, roundtrip, lz, gfx, bin2db, text, lift."""
import hashlib
import sys
import tempfile
from pathlib import Path


def num(s):
    return int(s, 0)


def cmd_dis(a):
    from .ngpc.rom import Rom, CART_BASE
    rom = Rom(Path(a.rom).read_bytes())
    if a.output:
        from .ngpc.writer import disassemble_rom
        st = disassemble_rom(rom, a.output, a.executed, sweep=not a.no_sweep)
        print(f"wrote {a.output}/main.asm: {st['instructions']} instructions "
              f"({st['db_fallback']} as db), {st['data_bytes']} data bytes, {st['sections']} sections")
        return 0
    from .cpu.decode import decode
    pc = a.start if a.start is not None else (rom.header()["entry"] if len(rom.data) > 0x20 else CART_BASE)
    for _ in range(a.count):
        if not rom.contains(pc):
            break
        ins = decode(rom, pc)
        t = ins.text().replace("\t", " ", 1)
        flag = f"  ; {ins.illegal}" if ins.illegal else ""
        print(f"{pc:06X}  {ins.raw.hex(' '):<24} {t}{flag}")
        pc += ins.size
    return 0


def roundtrip(rom_path, workdir=None, executed=None):
    """Disassemble a ROM to asm900 source and assemble it back. Returns (ok, stats, errors)."""
    from .ngpc.rom import Rom, CART_BASE
    from .ngpc.writer import disassemble_rom
    from .asm.assembler import Assembler, build_image
    data = Path(rom_path).read_bytes()
    rom = Rom(data)
    tmp = None
    if workdir is None:
        tmp = tempfile.TemporaryDirectory()
        workdir = tmp.name
    st = disassemble_rom(rom, workdir, executed)
    asm = Assembler()
    asm.add_module(Path(workdir) / "main.asm")
    chunks = asm.assemble()
    img = None
    if not asm.d.errors:
        img, _, _ = build_image(chunks, CART_BASE, len(data))
    if tmp:
        tmp.cleanup()
    return img == data, st, asm.d.errors, img


def cmd_roundtrip(a):
    ok, st, errs, img = roundtrip(a.rom, a.workdir, a.executed)
    for e in errs[:20]:
        print(e, file=sys.stderr)
    sha = hashlib.sha1(img).hexdigest() if img else "-"
    print(f"{'OK' if ok else 'DIFFERS'}: {st['instructions']} instructions ({st['db_fallback']} as db), "
          f"{st['data_bytes']} data bytes; sha1 {sha}")
    return 0 if ok else 1


def cmd_lz(a):
    from .codec import lzss
    data = Path(a.input).read_bytes()
    if a.mode == "d":
        out, used = lzss.decompress(data, a.length, a.offset)
        print(f"{used} input bytes -> {len(out)} bytes", file=sys.stderr)
    else:
        out = lzss.compress_okumura(data, clamp=not a.no_clamp)
    Path(a.output).write_bytes(out)
    return 0


def cmd_bin2db(a):
    from .data import bin2db
    data = Path(a.input).read_bytes()
    text = bin2db.to_db(data, a.label, per_line=a.per_line, source=a.input)
    if a.output:
        Path(a.output).write_text(text)
    else:
        sys.stdout.write(text)
    return 0


def cmd_text(a):
    from .data.charmap import Charmap, text_to_db, CharmapError
    try:
        out = text_to_db(Charmap(a.charmap), Path(a.input).read_text(encoding="utf-8"), a.input)
    except CharmapError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if a.output:
        Path(a.output).write_text(out)
    else:
        sys.stdout.write(out)
    return 0


def cmd_gfx(a):
    from .gfx import tiles
    return tiles.cli(a)


def cmd_lift(a):
    from .lift import lift
    return lift.cli(a)


def add(sp):
    p = sp.add_parser("dis", help="disassemble: a listing, or (-o DIR) rebuildable asm900 source")
    p.add_argument("rom")
    p.add_argument("-o", "--output", help="write DIR/main.asm that assembles back to the ROM")
    p.add_argument("--start", type=num)
    p.add_argument("--count", type=num, default=64)
    p.add_argument("--executed", help="file of hex code addresses (extra trace entry points)")
    p.add_argument("--no-sweep", action="store_true", help="static trace only (no gap sweep)")
    p.set_defaults(fn=cmd_dis)

    p = sp.add_parser("roundtrip", help="dis -> asm a ROM and compare the bytes")
    p.add_argument("rom")
    p.add_argument("--workdir", help="keep the source here (default: a temporary directory)")
    p.add_argument("--executed")
    p.set_defaults(fn=cmd_roundtrip)

    p = sp.add_parser("lz", help="LZSS (Okumura LZSS.C parameters N=1024 F=18)")
    p.add_argument("mode", choices=["c", "d"], help="c = compress, d = decompress")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--length", type=num, help="output length (decompress)")
    p.add_argument("--offset", type=num, default=0, help="stream start in input (decompress)")
    p.add_argument("--no-clamp", action="store_true", help="compress without cutting the last match")
    p.set_defaults(fn=cmd_lz)

    p = sp.add_parser("bin2db", help="binary file -> labeled asm900 db lines")
    p.add_argument("input")
    p.add_argument("label", nargs="?")
    p.add_argument("-o", "--output")
    p.add_argument("--per-line", type=int, default=16)
    p.set_defaults(fn=cmd_bin2db)

    p = sp.add_parser("text", help="text file + character map -> asm900 db lines")
    p.add_argument("charmap")
    p.add_argument("input")
    p.add_argument("-o", "--output")
    p.set_defaults(fn=cmd_text)

    from .gfx import tiles
    tiles.add_parser(sp, cmd_gfx)
    from .lift import lift
    lift.add_parser(sp, cmd_lift)
