"""Every decodable opcode form: assemble(text(decode(b))) == b, or a documented policy alternative.

Policy alternatives (docs/encoding.md) decode to the same text and are never longer:
shorter forms (extended register codes, wider displacements/addresses, long immediates),
the `ld r,r` prefix rule, the lower of two equal-length forms (memory-to-memory LD, jp/call #16), the MUL/DIV rr,# low-half rule, and `ld R,#3` vs `ld R,#`.
Set UNITRON_FULL=1 for the full operand-value sweep (slower).
"""
import collections
import os

from opspace import space
from unitron.asm.line import assemble_line
from unitron.cpu.decode import decode_bytes

PC = 0x200000


def _category(i, r):
    if len(r) < len(i.raw):
        return "shorter"
    if i.mnem == "LD" and len(i.ops) == 2 and r[0] >> 4 in (0xC, 0xD, 0xE) and i.raw[0] >> 4 in (0xC, 0xD, 0xE):
        if i.raw[-1] & 0xF0 in (0x80, 0x90) and r[-1] & 0xF0 in (0x80, 0x90):
            return "ld r,r prefix rule"
    if i.mnem == "LD" and (i.raw[-1] & 0xF8) == 0xA8 and 0x20 <= r[0] <= 0x27:
        return "ld R,#3 vs ld R,#"
    if i.mnem in ("LDB", "LDW") and i.ops[0].startswith("(") and i.ops[1].startswith("("):
        return "ld (#16),(mem) vs ld (mem),(#16)"
    if i.mnem in ("JP", "CALL") and r[0] in (0x1A, 0x1C):
        return "jp/call #16 vs jp/call (#8)"
    if i.mnem in ("MUL", "MULS", "DIV", "DIVS"):
        return "mul/div rr,# low half"
    return None


def test_opcode_space():
    full = os.environ.get("UNITRON_FULL")
    seen, cats, bad = set(), collections.Counter(), []
    for n, b in enumerate(space()):
        if not full and n % 3:
            continue
        i = decode_bytes(b, PC)
        if i.invalid or i.illegal or i.raw in seen:
            continue
        seen.add(i.raw)
        t = i.text()
        try:
            r = assemble_line(t, PC)
        except Exception as e:           # noqa: BLE001
            bad.append((i.raw.hex(" "), t, f"error: {e}"))
            continue
        if r == i.raw:
            cats["identical"] += 1
            continue
        j = decode_bytes(r, PC)
        if j.text() != t or j.size != len(r):
            bad.append((i.raw.hex(" "), t, f"-> {r.hex(' ')} = {j.text()}"))
            continue
        c = _category(i, r)
        if c is None:
            bad.append((i.raw.hex(" "), t, f"-> {r.hex(' ')} (uncategorized)"))
        cats[c] += 1
    assert not bad, bad[:20]
    assert cats["identical"] > 100000
    print(dict(cats))
