#!/usr/bin/env python3
"""ONE-OFF MIGRATION HELPER (not part of unitron proper; delete after the decomp has migrated).

Converts the the reference ROM decomp's current src/ (its invented `.org/.db/[size]/{n}` dialect, assembled
by its tlcs900/asm.py) into asm900 source for unitron, then proves the result: unitron assembles it
to the original ROM. The decomp is only read. Output goes to a directory of your choice (use /tmp).

  python -I tools/convert_decomp.py DECOMP_DIR ROM OUT_DIR

What changes: `.org` pieces become `c_ADDR`/`d_ADDR` abs sections; `.equ` -> `equ`; `.db/.dw/.dl`
-> `db/dw/dl`; `.fill/.fillw` -> `dfb/dfw`; `.str` and charmap text -> `db` (text in the comment);
`.incbin` -> `$include` of a bin2db file under OUT/build/; `.include` -> `#include`; `.loop` ->
`Routine.loop`; the macro -> `?macro`; `{n}`/`[size]` go away. An instruction whose default
encoding differs from the ROM gets asm900's `:n` width, or else becomes `db` (counted, listed).
"""
import hashlib
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from unitron.asm import expr as X  # noqa: E402
from unitron.asm.line import parse_insn, LineError  # noqa: E402
from unitron.cpu.encode import encode, EncodeError  # noqa: E402
from unitron.data.bin2db import to_db  # noqa: E402
from unitron.data.charmap import Charmap  # noqa: E402

CART = 0x200000
OLD_LINE = re.compile(r"^\s*([A-Z][A-Z0-9]*)(?:\{(\d+)\})?\s*(.*?)\s*\[(\d+)\]\s*(;.*)?$")
LOCAL_REF = re.compile(r"(?<![\w.])\.([A-Za-z_]\w*)")


def data_tokens(rest):
    rest = re.sub(r"\s*([-+*|])\s*(?=[\w(])", r"\1", rest.strip())
    return [t for t in re.split(r"[,\s]+", rest) if t]


def str_bytes(body):
    out, i = bytearray(), 0
    while i < len(body):
        m = re.match(r"\{([0-9A-Fa-f]{2})\}", body[i:])
        if m:
            out.append(int(m.group(1), 16))
            i += 4
        else:
            out.append(ord(body[i]))
            i += 1
    return bytes(out)


def db_line(bs, comment=None):
    t = "\tdb\t" + ",".join(f"0x{b:02X}" for b in bs)
    return f"{t}\t; {comment}" if comment else t


class Converter:
    def __init__(self, decomp, rom):
        self.root = Path(decomp)
        self.src = self.root / "src"
        self.rom = Path(rom).read_bytes()
        self.charmap = None
        self.macros = {}          # name -> (body lines, size)
        self.syms = {}            # name -> value
        self.equs = []            # (name, expr text) in order
        self.files = {}           # path -> list of line records
        self.stats = {"instructions": 0, "width_marked": 0, "db": 0, "db_lines": []}

    # ---------------------------------------------------------- pass 1: addresses
    def walk(self, path, pc=None, glob=""):
        """Record (kind, pc, raw, extra) for every line of path and its includes."""
        path = Path(path)
        recs = []
        self.files[path] = recs
        lines = path.read_text(encoding="utf-8").splitlines()
        i = 0
        while i < len(lines):
            raw = lines[i]
            i += 1
            s = raw.split(";", 1)[0].rstrip()
            st = s.strip()
            if self.charmap is not None:
                b = self.charmap.command(raw) if raw[:1].isspace() else None
                if b is not None:
                    recs.append(("text", pc, raw, b))
                    pc += len(b)
                    continue
            m = re.match(r'^\s*\.str\s+"((?:[^"\\]|\\.)*)"\s*(;.*)?$', raw)
            if m:
                b = str_bytes(m.group(1))
                recs.append(("str", pc, raw, b))
                pc += len(b)
                continue
            if not st:
                recs.append(("blank", pc, raw, None))
                continue
            w = st.split(None, 1)
            d = w[0]
            rest = w[1] if len(w) > 1 else ""
            if d == ".org":
                pc = int(rest.split()[0], 0)
                recs.append(("org", pc, raw, None))
            elif d == ".equ":
                name, val = rest.split(None, 1)
                self.equs.append((name, val.strip()))
                recs.append(("equ", pc, raw, (name, val.strip())))
            elif d == ".include":
                inc = path.parent / rest.strip().strip('"')
                recs.append(("include", pc, raw, inc))
                pc, glob = self.walk(inc, pc, glob)
            elif d == ".incbin":
                f = (path.parent / rest.strip().strip('"')).resolve()
                n = f.stat().st_size
                recs.append(("incbin", pc, raw, f))
                pc += n
            elif d == ".charmap":
                self.charmap = Charmap(path.parent / rest.strip().strip('"'))
                recs.append(("charmap", pc, raw, None))
            elif d == ".macro":
                name = rest.split()[0]
                body = []
                while not lines[i].split(";", 1)[0].strip() == ".endm":
                    body.append(lines[i])
                    i += 1
                i += 1
                size = sum(int(OLD_LINE.match(b).group(4)) for b in body if OLD_LINE.match(b))
                self.macros[name] = (body, size)
                recs.append(("macro", pc, raw, (name, body)))
            elif d in (".db", ".dw", ".dl"):
                toks = data_tokens(rest)
                recs.append(("data", pc, raw, (d[1:], toks)))
                pc += {".db": 1, ".dw": 2, ".dl": 4}[d] * len(toks)
            elif d in (".fill", ".fillw"):
                toks = data_tokens(rest)
                n = int(toks[0], 0)
                recs.append(("fill", pc, raw, (d, toks)))
                pc += n * (2 if d == ".fillw" else 1)
            elif st.endswith(":") and not raw[:1].isspace():
                name = st[:-1]
                if name.startswith("."):
                    name = glob + name
                else:
                    glob = name
                self.syms[name] = pc
                recs.append(("label", pc, raw, name))
            elif st in self.macros:
                recs.append(("call", pc, raw, (st, glob)))
                pc += self.macros[st][1]
            else:
                m = OLD_LINE.match(raw)
                if not m:
                    raise SystemExit(f"{path}:{i}: cannot read {raw!r}")
                recs.append(("ins", pc, raw, (m, glob)))
                pc += int(m.group(4))
        return pc, glob

    def resolve_equs(self):
        pending = list(self.equs)
        for _ in range(10):
            left = []
            for name, val in pending:
                try:
                    self.syms[name] = self.value(val)
                except X.Unknown:
                    left.append((name, val))
            pending = left
            if not pending:
                return
        raise SystemExit(f"cannot resolve equ: {pending[:5]}")

    def value(self, text):
        e = X.parse(asm900_expr(text))
        return X.evaluate(e, self._look, lambda: (0, 0))[0]

    def _look(self, n):
        if n in self.syms:
            return self.syms[n], 0
        raise X.Unknown(n)

    # ---------------------------------------------------------- pass 2: writing
    def out_path(self, path):
        rel = path.relative_to(self.src)
        ext = ".inc" if rel.parts[0] in ("constants", "ram") or rel.name == "macros.s" else ".asm"
        return rel.with_suffix(ext)

    def convert(self, outdir):
        out = Path(outdir)
        main = self.src / "main.s"
        self.walk(main, None)
        self.resolve_equs()
        for path, recs in self.files.items():
            text = self.convert_file(path, recs, out)
            dst = out / "src" / self.out_path(path)
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(text)
        return out / "src" / "main.asm"

    def convert_file(self, path, recs, out):
        lines = []
        is_main = path.name == "main.s"
        if is_main:
            lines += ["$maximum", "\tmodule\trom1"]
        # the kind of each .org piece: code if it holds an instruction or a macro call
        kinds = []
        k = None
        for r in recs:
            if r[0] == "org":
                k = [r[1], "romdata"]
                kinds.append(k)
            elif r[0] in ("ins", "call") and k is not None:
                k[1] = "code"
        piece = iter(kinds)
        for kind, pc, raw, x in recs:
            comment = raw.split(";", 1)[1].strip() if ";" in raw and kind not in ("text", "str") else None
            tail = f"\t; {comment}" if comment else ""
            if kind == "blank":
                lines.append(raw.rstrip() if raw.strip() else "")
            elif kind == "org":
                _, sk = next(piece)
                lines.append(f"{'c' if sk == 'code' else 'd'}_{pc:06X}\tsection\t{sk} abs=0x{pc:06X}{tail}")
            elif kind == "equ":
                name, val = x
                lines.append(f"{name}\tequ\t{asm900_expr(val)}{tail}")
            elif kind == "include":
                inc = self.out_path(x)
                rel = Path(*inc.parts)
                here = self.out_path(path).parent
                lines.append(f'#include "{_relpath(rel, here)}"{tail}')
            elif kind == "incbin":
                gen = self.gen_data(x, out)
                src_note = str(x.relative_to(self.root.resolve()))
                lines.append(f'$include "{gen}"\t; {src_note}')
            elif kind == "charmap":
                lines.append(f"; text below is converted to db with {raw.split(None, 1)[1].strip()}")
            elif kind == "macro":
                lines += self.convert_macro(*x)
            elif kind == "data":
                d, toks = x
                vals = ",".join(asm900_expr(local(t, None)) for t in toks)
                lines.append(f"\t{d}\t{vals}{tail}")
            elif kind == "fill":
                d, toks = x
                v = toks[1] if len(toks) > 1 else "0"
                lines.append(f"\t{'dfw' if d == '.fillw' else 'dfb'}\t{toks[0]},{asm900_expr(v)}{tail}")
            elif kind in ("text", "str"):
                lines += [db_line(x[i:i + 16], raw.strip() if i == 0 else None) for i in range(0, max(len(x), 1), 16)] \
                    if x else [f"\t; {raw.strip()} (no bytes)"]
            elif kind == "label":
                lines.append(f"{x}:")
            elif kind == "call":
                lines.append(f"\t{x[0]}{tail}")
            elif kind == "ins":
                lines.append(self.convert_ins(pc, x[0], x[1], tail))
        if is_main:
            lines += ["", "\tend"]
        return "\n".join(lines) + "\n"

    def convert_macro(self, name, body):
        out = [f"; {name} (macro): expands to \"R: " + " / ".join(
            _old_to_text(OLD_LINE.match(b), "R").replace("\t", " ") for b in body if OLD_LINE.match(b))
            + "\", R = this resume point",
            f"?macro {name}", "labels resume"]
        for b in body:
            s = b.split(";", 1)[0].strip()
            if s == "\\@:":
                out.append("?resume:")
                continue
            m = OLD_LINE.match(b)
            if not m:
                continue
            out.append("\t" + _old_to_text(m, "?resume"))
        out.append("?endm")
        return out

    def gen_data(self, f, out):
        """bin2db file for an .incbin target; returns its path for $include (relative to OUT)."""
        rel = f.relative_to(self.root.resolve())
        if rel.parts[0] == "build":
            rel = Path(*rel.parts[1:])
        gen = Path("build") / (str(rel) + ".asm")
        dst = out / gen
        if not dst.exists():
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(to_db(f.read_bytes(), source=str(f.relative_to(self.root.resolve()))))
        return gen.as_posix()

    def convert_ins(self, pc, m, glob, tail):
        mnem, var, rest, size = m.group(1), m.group(2), m.group(3), int(m.group(4))
        raw = self.rom[pc - CART:pc - CART + size]
        rest = local(rest, glob)
        ops = old_ops(mnem, rest)
        text = f"{mnem.lower()}\t{','.join(ops)}" if ops else mnem.lower()
        self.stats["instructions"] += 1
        if self.enc(text, pc) == raw:
            return f"\t{text}{tail}"
        for var_ops in width_variants(ops):
            t = f"{mnem.lower()}\t{','.join(var_ops)}"
            if self.enc(t, pc) == raw:
                self.stats["width_marked"] += 1
                return f"\t{t}{tail}"
        self.stats["db"] += 1
        self.stats["db_lines"].append((pc, text))
        return db_line(raw, f"{text.replace(chr(9), ' ')} (no asm900 spelling for the ROM's encoding)")

    def enc(self, text, pc):
        mn, _, field = text.partition("\t")
        try:
            base, sfx, ops = parse_insn(mn, field)
            return encode(base, sfx, ops, pc, lambda e: X.evaluate(e, self._look, lambda: (pc, 0)), True)
        except (LineError, EncodeError, X.ExprError, X.Unknown):
            return None


def _relpath(target, here):
    import os
    return Path(os.path.relpath(target, here)).as_posix()


def local(text, glob):
    if glob is None:
        return text
    return LOCAL_REF.sub(lambda m: f"{glob}.{m.group(1)}", text)


def asm900_expr(t):
    return t.strip()


def old_ops(mnem, rest):
    from unitron.cpu.operands import split
    ops = [o.strip() for o in split(rest)] if rest.strip() else []
    if mnem in ("JP", "CALL") and ops:
        last = ops[-1]
        if last.startswith("(") and last.endswith(")"):
            ops[-1] = last[1:-1]
    return ops


def _old_to_text(m, label):
    mnem, rest = m.group(1), m.group(3).replace("\\@", label)
    ops = old_ops(mnem, rest)
    return f"{mnem.lower()}\t{','.join(ops)}" if ops else mnem.lower()


def width_variants(ops):
    """Operand lists with asm900 `:n` widths added (one or two operands)."""
    single = []
    for i, o in enumerate(ops):
        if ":" in o:
            continue
        if o.startswith("(") and o.endswith(")"):
            single += [(i, 24), (i, 16)]
        else:
            single += [(i, 16), (i, 8), (i, 32), (i, 24)]

    def apply(var):
        out = list(ops)
        for i, n in var:
            o = out[i]
            out[i] = f"{o[:-1]}:{n})" if o.startswith("(") else f"{o}:{n}"
        return out
    for v in single:
        yield apply([v])
    for a in range(len(single)):
        for b in range(a + 1, len(single)):
            if single[a][0] != single[b][0]:
                yield apply([single[a], single[b]])


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    decomp, rom, out = sys.argv[1:]
    conv = Converter(decomp, rom)
    main_asm = conv.convert(out)
    st = conv.stats
    print(f"converted {len(conv.files)} files: {st['instructions']} instructions, "
          f"{st['width_marked']} with a :n width, {st['db']} as db")
    for pc, t in st["db_lines"][:50]:
        print(f"  db {pc:06X} {t}")
    from unitron.asm.assembler import Assembler, build_image
    a = Assembler(include_dirs=[out])
    a.add_module(main_asm)
    chunks = a.assemble()
    for e in a.d.errors[:20]:
        print(e)
    if a.d.errors:
        raise SystemExit(f"{len(a.d.errors)} assembler errors")
    img, _, _ = build_image(chunks, CART, len(conv.rom))
    ok = img == conv.rom
    print(f"unitron build: {'byte-identical' if ok else 'DIFFERS'}, sha1 {hashlib.sha1(img).hexdigest()}")
    if not ok:
        d = [i for i in range(len(img)) if img[i] != conv.rom[i]]
        print(f"{len(d)} bytes differ, first at {[hex(CART + x) for x in d[:8]]}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
