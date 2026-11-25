"""Any NGPC ROM -> asm900 source that assembles back to the same bytes.

Code found by the trace becomes instructions; everything else becomes `db` lines. A non-default
encoding gets asm900's `:n` width (`ld A,(0x62AA:24)`); one with no asm900 spelling at all, or a
form the datasheet doesn't define, is written as `db` with the instruction in the comment.
"""
import re
from pathlib import Path

from ..asm.line import assemble_line, LineError
from ..cpu.regs import REG
from . import hwnames
from .rom import CART_BASE

DB_PER_LINE = 16


def _width_variants(ops):
    """Ways to add an asm900 `:n` width to one or two operands: (index, n) lists."""
    single = []
    for i, o in enumerate(ops):
        if ":" in o:
            continue
        if o.startswith("(") and o.endswith(")"):
            inner = o[1:-1]
            if hasattr(o, "addr"):
                single += [(i, 16), (i, 24)]
            elif re.search(r"[+-](0x[0-9A-F]+|\d+)$", inner):
                single.append((i, 16))
        elif re.fullmatch(r"-?(0x[0-9A-F]+|\d+)", o) or hasattr(o, "addr"):
            sizes = [REG[x][0] * 8 for x in ops if x in REG]
            order = sizes[:1] + [n for n in (8, 16, 24, 32) if n not in sizes[:1]]
            single += [(i, n) for n in order]
    yield from ([v] for v in single)
    for a in range(len(single)):
        for b in range(a + 1, len(single)):
            if single[a][0] != single[b][0]:
                yield [single[a], single[b]]


def _apply_width(ops, var):
    out = list(ops)
    for i, n in var:
        o = out[i]
        out[i] = f"{o[:-1]}:{n})" if o.startswith("(") else f"{o}:{n}"
    return out


def spell(ins, pc):
    """(index, width) changes that make ins.text() assemble to ins.raw, or None if none do."""
    if assemble_or_none(ins.text(), pc) == ins.raw:
        return []
    m = ins.mnem.lower()
    for var in _width_variants(ins.ops):
        t = f"{m}\t{','.join(_apply_width(ins.ops, var))}"
        if assemble_or_none(t, pc) == ins.raw:
            return var
    return None


def assemble_or_none(text, pc):
    try:
        return assemble_line(text, pc)
    except LineError:
        return None


def _db(bs):
    return "\tdb\t" + ",".join(f"0x{b:02X}" for b in bs)


def write_source(rom, insns, labels, outdir, module="game"):
    """Write outdir/main.asm (+ hw.inc). Returns statistics."""
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    data = rom.data
    end = CART_BASE + len(data)
    hw = {a: n for a, n, *_ in hwnames.NAMES}
    used_hw = {}

    # instruction starts that will actually be emitted (overlapping decodes are dropped)
    emitted = set()
    pc = CART_BASE
    while pc < end:
        ins = insns.get(pc)
        if ins is not None:
            emitted.add(pc)
            pc += ins.size
        else:
            pc += 1
    names = {a: n for a, n in labels.items() if a in emitted}

    def sym(o):
        a = getattr(o, "addr", None)
        if a is None:
            return o
        if o.kind == "code" or not o.startswith("("):
            n = names.get(a)
            if n:
                return n
            return o
        n = hw.get(a)
        if n:
            used_hw[n] = a
            return f"({n})"
        return o

    h = rom.header()
    lines = [f"; {h['title']!r}: asm900 source written by `unitron dis` from the ROM.",
             f"; Build: unitron asm main.asm -o out.ngp --origin 0x{CART_BASE:X} --size 0x{len(data):X}",
             "$maximum", f"\tmodule\t{module}", '$include "hw.inc"', ""]
    stats = {"instructions": 0, "db_fallback": 0, "width_marked": 0, "data_bytes": 0, "sections": 0}
    pc = CART_BASE
    in_code = None
    while pc < end:
        ins = insns.get(pc) if pc in emitted else None
        if ins is not None:
            if in_code is not True:
                lines.append(f"\nc_{pc:06X}\tsection\tcode abs=0x{pc:06X}")
                stats["sections"] += 1
                in_code = True
            if pc in names:
                lines.append(f"{names[pc]}:")
            why = ins.illegal
            var = None
            if why is None:
                var = spell(ins, pc)
                if var is None:
                    why = "no asm900 spelling for this encoding"
            if why:
                lines.append(f"{_db(ins.raw)}\t; {ins.text().replace(chr(9), ' ')} ({why})")
                stats["db_fallback"] += 1
            else:
                if var:
                    stats["width_marked"] += 1
                ops = _apply_width([sym(o) for o in ins.ops], var)
                t = f"{ins.mnem.lower()}\t{','.join(ops)}" if ops else ins.mnem.lower()
                if ins.comment:
                    t += f"\t; {ins.comment}"
                lines.append("\t" + t)
            stats["instructions"] += 1
            pc += ins.size
            continue
        q = pc
        while q < end and q not in emitted:
            q += 1
        lines.append(f"\nd_{pc:06X}\tsection\tromdata abs=0x{pc:06X}")
        if pc in names:
            lines.append(f"{names[pc]}:")
        stats["sections"] += 1
        in_code = False
        chunk = data[pc - CART_BASE:q - CART_BASE]
        for i in range(0, len(chunk), DB_PER_LINE):
            lines.append(_db(chunk[i:i + DB_PER_LINE]))
        stats["data_bytes"] += len(chunk)
        pc = q
    lines += ["", "\tend", ""]
    (out / "main.asm").write_text("\n".join(lines))
    cite = {n: (c, off) for _, n, _, _, c, off in hwnames.NAMES}
    inc = ["; hw.inc: the NGPC hardware names this source uses (from unitron/data/ngpc_hw.inc)."]
    for n, a in sorted(used_hw.items(), key=lambda kv: kv[1]):
        c, off = cite[n]
        inc.append(f"{n}\tequ\t0x{a:X}\t; {c} ({off})")
    (out / "hw.inc").write_text("\n".join(inc) + "\n")
    return stats


def disassemble_rom(rom, outdir, executed=None, sweep=True):
    from .trace import discover
    insns, labels, st = discover(rom, executed, do_sweep=sweep)
    labels = dict(labels)
    stats = write_source(rom, insns, labels, outdir)
    stats.update({k: v for k, v in st.items() if isinstance(v, int)})
    return stats
