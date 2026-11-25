"""unitron command line: asm, mac, dis, lz, gfx, bin2db, lift."""
import argparse
import sys
from pathlib import Path


def num(s):
    return int(s, 0)


def _place(s):
    name, _, addr = s.partition("=")
    if not addr:
        raise argparse.ArgumentTypeError("--place NAME=ADDR")
    return name, int(addr, 0)


def _define(s):
    k, _, v = s.partition("=")
    return k, v or "1"


def cmd_asm(a):
    from .asm.assembler import Assembler, build_image
    from .asm import expr
    expr.INTEL_SUFFIX = a.intel
    asm = Assembler(a.include, strict=a.strict, mpp=not a.no_mpp, defines=dict(a.define))
    for src in a.sources:
        asm.add_module(src)
    chunks = asm.assemble(link=not a.no_link, placements=dict(a.place),
                          data_base=a.data_base, code_base=a.code_base)
    img = None
    if not asm.d.errors and not a.no_link and a.output:
        img, origin, size = build_image(chunks, a.origin, a.size, a.fill, asm.d)
    for w in asm.d.warnings:
        print(w, file=sys.stderr)
    for e in asm.d.errors[:200]:
        print(e, file=sys.stderr)
    if asm.d.errors:
        more = len(asm.d.errors) - 200
        print(f"{len(asm.d.errors)} error(s){f' ({more} not shown)' if more > 0 else ''}; no output written",
              file=sys.stderr)
        return 1
    if img is not None:
        Path(a.output).write_bytes(img)
        print(f"wrote {a.output}: 0x{size:X} bytes at 0x{origin:X}")
    if a.sym:
        syms = asm.symbols()
        with open(a.sym, "w") as f:
            for n, v in sorted(syms.items(), key=lambda kv: (kv[1], kv[0])):
                f.write(f"{v & 0xFFFFFFFF:06X} {n}\n")
    return 0


def cmd_mac(a):
    from . import mpp
    try:
        lines = mpp.preprocess(a.source, a.include, dict(a.define))
    except mpp.MppError as e:
        print(f"{e.where}: error: {e}" if e.where else f"error: {e}", file=sys.stderr)
        return 1
    text = "".join(t + "\n" for t, _ in lines)
    if a.output:
        Path(a.output).write_text(text)
    else:
        sys.stdout.write(text)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="unitron", description="NGPC (TLCS-900/H) toolchain")
    sp = ap.add_subparsers(dest="cmd", required=True)

    p = sp.add_parser("asm", help="assemble (and link) asm900 sources into a raw image")
    p.add_argument("sources", nargs="+", help="one module per file")
    p.add_argument("-o", "--output")
    p.add_argument("-I", "--include", action="append", default=[])
    p.add_argument("-D", "--define", action="append", default=[], type=_define)
    p.add_argument("--origin", type=num, help="image start address (default: lowest code/romdata byte)")
    p.add_argument("--size", type=num, help="image size (default: up to the highest byte)")
    p.add_argument("--fill", type=num, default=0xFF, help="byte for gaps (default 0xFF)")
    p.add_argument("--place", action="append", default=[], type=_place, metavar="SECTION=ADDR")
    p.add_argument("--data-base", type=num, default=0x4000, help="first address for data sections")
    p.add_argument("--code-base", type=num, default=0x200000, help="first address for code/romdata")
    p.add_argument("--no-link", action="store_true", help="check one module; externs stay unresolved")
    p.add_argument("--no-mpp", action="store_true", help="skip the macro preprocessor")
    p.add_argument("--strict", action="store_true", help="enforce asm900's include limits")
    p.add_argument("--sym", help="write a symbol file (address name)")
    p.add_argument("-H", dest="intel", action="store_true",
                   help="also accept suffixed numbers (1234H, 101B, 17O/17Q, 99D), as asm900 -H")
    p.set_defaults(fn=cmd_asm)

    p = sp.add_parser("mac", aliases=["preprocess"], help="run the macro preprocessor only")
    p.add_argument("source")
    p.add_argument("-o", "--output")
    p.add_argument("-I", "--include", action="append", default=[])
    p.add_argument("-D", "--define", action="append", default=[], type=_define)
    p.set_defaults(fn=cmd_mac)

    from . import cli_more
    cli_more.add(sp)

    a = ap.parse_args(argv)
    sys.exit(a.fn(a) or 0)
