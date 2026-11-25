#!/usr/bin/env python3
"""Freeze the Python reference implementation's behavior as data files (tests/vectors/), so the
C++ port can be checked byte for byte without Python. Run once, before the Python version goes.

  python -I tools/freeze_vectors.py opspace|lines|all

opspace.tsv.z  every distinct instruction of the opcode space (tests/opspace.py, full sweep;
               "=" = assembles back to the same bytes):
               decoder output and what the assembler makes of its text
opspace_invalid.bin.z  one bit per enumerated input: 1 = the decoder found no instruction
lines.tsv.z    instruction lines (mnemonic x operand spellings) -> bytes or the error message
(zlib streams; lodepng's inflate reads them in the C++ tests.)
"""
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # legacy/python: the package and its test helpers
REPO = Path(__file__).resolve().parents[3]  # the unitron repo: where the vectors live
sys.path[:0] = [str(ROOT), str(ROOT / "tests")]
OUT = REPO / "tests" / "vectors"

from unitron.asm.line import assemble_line, LineError  # noqa: E402
from unitron.cpu.decode import decode_bytes  # noqa: E402

PC = 0x200000


def asm_result(text):
    try:
        return assemble_line(text, PC).hex()
    except LineError as e:
        return "!" + str(e)
    except Exception as e:  # noqa: BLE001  (a crash in the reference: recorded, never expected)
        return "!!" + type(e).__name__


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def opspace():
    from opspace import space
    rows = {}
    bits = bytearray()
    cur, nb = 0, 0
    for b in space():
        i = decode_bytes(b, PC)
        v = 1 if i.invalid else 0
        cur |= v << nb
        nb += 1
        if nb == 8:
            bits.append(cur)
            cur, nb = 0, 0
        if i.invalid or i.raw in rows:
            continue
        refs = ";".join(f"{k}:{o.addr:X}:{o.kind}" for k, o in enumerate(i.ops) if hasattr(o, "addr"))
        flags = "".join(c if f else "-" for c, f in zip("cjqe", (i.is_call, i.is_jump, i.conditional, i.ends_block)))
        rows[i.raw] = "\t".join([
            i.raw.hex(), i.mnem, "|".join(i.ops), esc(i.illegal or "-"), flags,
            "-" if i.target is None else f"{i.target:X}",
            ",".join(f"{a:X}" for a in i.mem_refs) or "-",
            "-" if i.imm32 is None else f"{i.dst_reg}={i.imm32:X}",
            refs or "-", "=" if asm_result(i.text()) == i.raw.hex() else esc(asm_result(i.text()))])
    if nb:
        bits.append(cur)
    text = "".join(r + "\n" for r in rows.values())
    (OUT / "opspace.tsv.z").write_bytes(zlib.compress(text.encode(), 9))
    (OUT / "opspace_invalid.bin.z").write_bytes(zlib.compress(bytes(bits), 9))
    print(f"opspace: {len(rows)} instructions, {nb + 8 * (len(bits) - (1 if nb else 0))} inputs")


# operand spellings for the line corpus: every operand kind, sizes, widths, errors
OPS = ["A", "W", "B", "C", "L", "WA", "BC", "HL", "SP", "IX", "XWA", "XHL", "XIX", "XSP", "RA3", "QW",
       "QWA", "XHL'", "WA'", "RW0", "F", "F'", "SR", "INTNEST", "XNSP", "DMAS0", "DMAC1", "DMAM2",
       "Z", "NC", "T", "EQ", "0", "1", "3", "7", "8", "15", "16", "-1", "0x7F", "0x80", "0xFF", "0x100",
       "0x1234", "0xFFFF", "0x10000", "0x123456", "0xFFFFFF", "0x1000000", "0x12345678", "-129",
       "2:3", "2:16", "0:8", "0x20:24", "5:32", "(XHL)", "(XIX)", "(QWA)", "(RA3)", "(XWA')", "(XHL+5)",
       "(XHL-5)", "(XHL+0)", "(XHL+0x80)", "(XHL-0x81)", "(XHL+0xF:16)", "(XHL-2:16)", "(XHL+0x8000)",
       "(XHL+A)", "(XHL+DE)", "(XHL+XDE)", "(XHL-A)", "(-XHL)", "(-XSP:4)", "(-XHL:3)", "(XHL+)",
       "(XHL+:1)", "(XHL+:2)", "(XHL+:4)", "(XDE+)", "(XHL-)", "(XIX+)", "(XIY+)", "(XDE-)",
       "(XIX-)", "(XIY-)", "(HL)", "(0x20)", "(0x82)", "(0x1234)", "(0x123456)", "(0x62AA:24)",
       "(0x20:16)", "(0x1000000)", "(-1)", "XHL+2", "XIX-3", "XHL+DE", "XWA+C", "(XHL", "XHL)",
       "()", "''", "'A'", "'AB'", "\"s\"", "0b1", "12H", "08", "1+", "nosym", "$", "$+4", "1/0",
       "sizeof(x)", "~0", "-0x80000000", "1<<31", "(1+2)*3", "0y101", "017", "XHL+", "-XHL", ""]
# operand pairs use this middle set (every kind once or twice) to keep the file small
MID = ["A", "B", "WA", "SP", "XWA", "XIX", "RA3", "QWA", "XHL'", "F", "F'", "SR", "INTNEST", "DMAM2",
       "Z", "T", "0", "3", "7", "8", "16", "-1", "0xFF", "0x100", "0x10000", "0x123456", "2:3", "2:16",
       "(XHL)", "(QWA)", "(XHL+5)", "(XHL+0)", "(XHL-0x81)", "(XHL+0xF:16)", "(XHL+A)", "(XHL+DE)",
       "(-XHL)", "(-XSP:4)", "(XHL+)", "(XHL+:1)", "(XDE+)", "(XHL-)", "(XIY-)", "(0x82)", "(0x1234)",
       "(0x123456)", "(0x62AA:24)", "XHL+2", "XHL+DE", "'A'", "nosym", "$+4", ""]
SMALL = ["A", "WA", "XWA", "RA3", "5", "0x1234", "(XHL)", "(XHL+5)", "(0x1234)", "(XHL+)", "(-XHL)",
         "XHL", "Z", "F", ""]


def lines():
    from unitron.cpu.encode import MNEMONICS
    mn = sorted(MNEMONICS)
    spell = []
    for m in mn:
        spell.append((m.lower(), True))
        for s in "BWL":
            spell.append((m.lower() + s.lower(), False))
    spell += [("bogus", False), ("LD", False), ("Ld", False)]
    rows = []
    seen = set()

    def add(t):
        if t not in seen:
            seen.add(t)
            rows.append(f"{esc(t)}\t{esc(asm_result(t))}")
            # a crash in the reference ("!!") means: any error is acceptable

    for m, base in spell:
        add(m)
        ops = OPS if base else SMALL
        for a in ops:
            add(f"{m}\t{a}")
        pairs = MID if base else SMALL
        for a in pairs:
            for b in pairs:
                add(f"{m}\t{a},{b}")
        for a in SMALL[:6]:
            add(f"{m}\t{a},{a},{a}")
    text = "".join(r + "\n" for r in rows)
    (OUT / "lines.tsv.z").write_bytes(zlib.compress(text.encode(), 9))
    crashes = sum("\t!!" in r for r in rows)
    print(f"lines: {len(rows)} lines, {crashes} reference crashes")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("opspace", "all"):
        opspace()
    if what in ("lines", "all"):
        lines()


# ---------------------------------------------------------------- golden CLI cases
GOLDEN = REPO / "tests" / "golden"


def parse_cases(text):
    """cases.txt -> [(name, [(path, bytes)], [args])]."""
    import shlex
    cases, cur, f = [], None, None
    for line in text.split("\n"):
        if f is not None and not line.startswith(("=== ", "--- ", "$ ")):
            f[1].append(line)
            continue
        if line.startswith("=== "):
            cur = (line[4:].strip(), [], [])
            cases.append(cur)
            f = None
        elif line.startswith("--- "):
            parts = line[4:].split()
            f = [parts[0], [], len(parts) > 1 and parts[1] == "hex"]
            cur[1].append(f)
        elif line.startswith("$ "):
            f = None
            cur[2].append(shlex.split(line[2:]))
        elif line.strip() and not line.startswith("#"):
            raise SystemExit(f"cases.txt: stray line {line!r}")
    out = []
    for name, files, cmds in cases:
        fl = []
        for path, lines, is_hex in files:
            while lines and lines[-1] == "":
                lines.pop()
            data = bytes.fromhex(" ".join(lines)) if is_hex else "".join(x + "\n" for x in lines).encode()
            fl.append((path, data))
        out.append((name, fl, cmds))
    return out


def show_bytes(path, data):
    """Text form of one output file: text, hex, or a decoded PNG (pixels, not zlib bytes)."""
    if path.endswith(".png"):
        import io
        import json
        from PIL import Image
        img = Image.open(io.BytesIO(data))
        pal = img.getpalette() or []
        px = list(img.getdata())
        used = sorted(set(px))
        lines = [f"png {img.width}x{img.height} mode {img.mode}"]
        lines += [f"color {i} {pal[3 * i]:02x}{pal[3 * i + 1]:02x}{pal[3 * i + 2]:02x}" for i in used]
        for k, v in sorted(img.text.items()):
            lines.append(f"text {k} {json.dumps(json.loads(v), separators=(',', ':'))}")
        for y in range(img.height):
            lines.append("".join(f"{p:x}" if p < 16 else f"[{p}]" for p in px[y * img.width:(y + 1) * img.width]))
        return "".join("|" + x + "\n" for x in lines)
    try:
        t = data.decode("utf-8")
        if "\0" in t:
            raise ValueError
    except ValueError:
        h = data.hex()
        return "".join(f"#{h[i:i + 64]}\n" for i in range(0, len(h), 64))
    return show_text(t)


def show_text(t):
    if not t:
        return ""
    lines = t.split("\n")
    tail = lines.pop()
    out = "".join("|" + x + "\n" for x in lines)
    return out + ("|" + tail + "\n~\n" if tail else "")


def snapshot(d):
    return {str(p.relative_to(d)): p.read_bytes() for p in sorted(d.rglob("*")) if p.is_file()}


def golden():
    import subprocess
    import tempfile
    cases = parse_cases((GOLDEN / "cases.txt").read_text())
    out = []
    crashes = 0
    for name, files, cmds in cases:
        out.append(f"=== {name}\n")
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            for p, data in files:
                (d / p).parent.mkdir(parents=True, exist_ok=True)
                (d / p).write_bytes(data)
            for args in cmds:
                before = snapshot(d)
                r = subprocess.run([sys.executable, "-I", "-m", "unitron", *args], cwd=d, capture_output=True,
                                   env={"PYTHONPATH": str(ROOT), "PATH": "/usr/bin:/bin"})
                out.append("$ " + " ".join(args) + "\n")
                err = r.stderr.decode("utf-8", "surrogateescape")
                if "Traceback" in err:
                    crashes += 1
                    out.append("crash\n")       # the reference crashed: any failure is acceptable
                    continue
                out.append(f"exit {r.returncode}\nstdout\n")
                out.append(show_bytes("", r.stdout) if r.stdout else "")
                out.append("stderr\n" + show_text(err))
                after = snapshot(d)
                for p in sorted(after):
                    if before.get(p) != after[p]:
                        out.append(f"file {p}\n" + show_bytes(p, after[p]))
    (GOLDEN / "expected.txt").write_text("".join(out))
    print(f"golden: {len(cases)} cases, {sum(len(c[2]) for c in cases)} commands, {crashes} reference crashes")


if __name__ == "__main__" and (sys.argv[1:2] in (["golden"], ["all"])):
    golden()


# ---------------------------------------------------------------- ROM and sample hashes (optional tests)
def rom_hashes():
    """sha256 of what the reference writes for the the reference ROM ROM and Toshiba's samples. Only hashes:
    the ROM and the samples are not ours to commit."""
    import hashlib
    import subprocess
    import tempfile
    rom = REPO.parent / "roms/rom1/rom1.ngp"
    samples = REPO.parent / "ngpc-project-template/ngpcbins/T900/SAMPLE/ASM900"
    out = []

    def run(args, cwd):
        r = subprocess.run([sys.executable, "-I", "-m", "unitron", *args], cwd=cwd, capture_output=True,
                           env={"PYTHONPATH": str(ROOT), "PATH": "/usr/bin:/bin"})
        return r

    def h(b):
        return hashlib.sha256(b).hexdigest()

    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        if rom.is_file():
            (d / "rom.ngp").write_bytes(rom.read_bytes())
            out.append(f"rom sha256 {h(rom.read_bytes())}")
            for name, args in [("dis_listing", ["dis", "rom.ngp", "--count", "4000"]),
                               ("dis_o", ["dis", "rom.ngp", "-o", "src"]),
                               ("dis_nosweep", ["dis", "rom.ngp", "-o", "src_ns", "--no-sweep"]),
                               ("roundtrip", ["roundtrip", "rom.ngp"]),
                               ("lift", ["lift", "rom.ngp", "-o", "lifted"])]:
                r = run(args, d)
                out.append(f"{name} exit {r.returncode} stdout {h(r.stdout)} stderr {h(r.stderr)}")
            for f in ("src/main.asm", "src/hw.inc", "src_ns/main.asm", "lifted/game.c", "lifted/ngpc_regs.h",
                      "lifted/ngpc_rt.h"):
                out.append(f"file {f} {h((d / f).read_bytes())}")
        if (samples / "SAMPLE1.ASM").is_file():
            for f in ("SAMPLE1.ASM", "SAMPLE2.ASM"):
                (d / f).write_bytes((samples / f).read_bytes())
            r = run(["asm", "SAMPLE1.ASM", "SAMPLE2.ASM", "-o", "s.bin", "--origin", "0x8000", "--size", "0x500",
                     "--place", "MAIN=0x8100", "--place", "TIME30=0x8200", "--place", "subrout=0x8300",
                     "--place", "ROM_DAT=0x8400", "--sym", "s.sym"], d)
            out.append(f"samples exit {r.returncode} stdout {h(r.stdout)} stderr {h(r.stderr)}")
            out.append(f"file s.bin {h((d / 's.bin').read_bytes())}")
            out.append(f"file s.sym {h((d / 's.sym').read_bytes())}")
            for f in ("SAMPLE1.ASM", "SAMPLE2.ASM"):
                r = run(["asm", f, "--no-link"], d)
                out.append(f"check {f} exit {r.returncode} stderr {h(r.stderr)}")
    (OUT / "rom_hashes.txt").write_text("".join(x + "\n" for x in out))
    print("\n".join(out))


if __name__ == "__main__" and (sys.argv[1:2] in (["rom"], ["all"])):
    rom_hashes()
