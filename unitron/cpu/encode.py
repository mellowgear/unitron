"""TLCS-900/H instruction encoder (asm900 operands -> bytes).

Forms and codes follow the datasheet instruction lists ("900/L1 Instruction Lists", the 900 core
the NGPC's 900/H shares). Where several encodings are legal the policy is ours (docs/encoding.md):
  1. the shortest legal encoding; equal lengths: the lowest byte string; but a byte `ld R,#n`
     is always 20+R unless written `n:3` (asm900), and `:n` forbids fields narrower than n;
  2. `ld r,r`: the prefix holds the source (`LD R,r`, 88+R), as all 404 in the reference ROM do;
  3. byte MUL/MULS/DIV/DIVS rr,#: the prefix names the low byte of rr (odd short code);
  4. (r+)/(-r) step = operand size unless `:n` is written;
  5. `(r+0)` is an 8-bit displacement of 0, `(r)` has none;
  6. absolute addresses: shortest width for the value, or the displacement width of a
     relocatable/extern symbol, or the `:n` width written in the source;
  7. jr/jrl/jp/call/calr as written; only the pseudo-instructions j/cal choose.
"""
from .regs import SHORT, ALU, SHIFT, CC_CODE
from .operands import Reg, Mem, Imm, Cond, Special, Ctl


class EncodeError(Exception):
    pass


# Legal forms per mnemonic (for operand-mismatch messages), from the datasheet lists.
FORMS = {
    "LD": "LD R,r | LD r,# | LD R,(mem) | LD (mem),R | LD<W> (mem),# | LD<W> (#16),(mem) | LD<W> (mem),(#16)",
    "PUSH": "PUSH F | PUSH A | PUSH SR | PUSH r | PUSH<W> # | PUSH<W> (mem)",
    "POP": "POP F | POP A | POP SR | POP r | POP<W> (mem)",
    "LDA": "LDA R,mem (R word or long)",
    "LDAR": "LDAR R,label (R word or long)",
    "EX": "EX F,F' | EX R,r | EX (mem),R",
    "ALU": "{m} R,r | {m} r,# | {m} R,(mem) | {m} (mem),R | {m}<W> (mem),#",
    "CP": "CP R,r | CP r,# | CP R,(mem) | CP (mem),R | CP<W> (mem),#",
    "INC": "{m} #3,r | {m}<W> #3,(mem)",
    "MUL": "{m} RR,r | {m} rr,# | {m} RR,(mem)",
    "SHIFT": "{m} #4,r | {m} A,r | {m}<W> (mem)",
    "CF": "{m} #4,r | {m} A,r | {m} #3,(mem) | {m} A,(mem)",
    "BIT": "{m} #4,r | {m} #3,(mem)",
    "JP": "JP #16 | JP #24 | JP [cc,]mem",
    "CALL": "CALL #16 | CALL #24 | CALL [cc,]mem",
}
ALUS = {n: i for i, n in enumerate(ALU)}
SHIFTS = {n: i for i, n in enumerate(SHIFT)}
MULS = {"MUL": 0, "MULS": 1, "DIV": 2, "DIVS": 3}
CFS = {"ANDCF": 0, "ORCF": 1, "XORCF": 2, "LDCF": 3, "STCF": 4}
BITS_REG = {"RES": 0x30, "SET": 0x31, "CHG": 0x32, "BIT": 0x33, "TSET": 0x34}
BITS_MEM = {"TSET": 0xA8, "RES": 0xB0, "SET": 0xB8, "CHG": 0xC0, "BIT": 0xC8}
SIMPLE = {"NOP": b"\x00", "HALT": b"\x05", "RETI": b"\x07", "INCF": b"\x0C", "DECF": b"\x0D",
          "RCF": b"\x10", "SCF": b"\x11", "CCF": b"\x12", "ZCF": b"\x13", "DI": b"\x06\x07"}
SINGLE = {"NEG": (0x07, (1, 2)), "CPL": (0x06, (1, 2)), "EXTZ": (0x12, (2, 4)),
          "EXTS": (0x13, (2, 4)), "PAA": (0x14, (2, 4)), "DAA": (0x10, (1,)),
          "MIRR": (0x16, (2,)), "MULA": (0x19, (2,)), "UNLK": (0x0D, (4,))}
MINC = {"MINC1": (0x38, 1), "MINC2": (0x39, 2), "MINC4": (0x3A, 4),
        "MDEC1": (0x3C, 1), "MDEC2": (0x3D, 2), "MDEC4": (0x3E, 4)}
BLOCK = {"LDI": 0x10, "LDIR": 0x11, "LDD": 0x12, "LDDR": 0x13}
CPBLK = {"CPI": 0x14, "CPIR": 0x15, "CPD": 0x16, "CPDR": 0x17}
JUMPS = {"JR", "JRL", "JP", "CALL", "CALR", "DJNZ", "J", "CAL"}

MNEMONICS = (set(SIMPLE) | set(SINGLE) | set(MINC) | set(BLOCK) | set(CPBLK) | set(ALUS) |
             set(SHIFTS) | set(MULS) | set(CFS) | set(BITS_REG) | JUMPS |
             {"LD", "PUSH", "POP", "LDA", "LDAR", "EX", "INC", "DEC", "EI", "SWI", "LDF", "RET",
              "RETD", "LDC", "LINK", "SCC", "RLD", "RRD", "BS1F", "BS1B", "LDX", "NORMAL", "MAX"})
# mnemonics that take a condition code as an optional first operand
COND_FIRST = {"JR", "JRL", "JP", "CALL", "RET", "SCC", "J", "CAL"}
# mnemonics whose address operand is written bare (no parentheses)
BARE_ADDR = {"JP", "CALL", "LDA", "J", "CAL"}
SIZE_OF = {"B": 1, "W": 2, "L": 4}


def split_mnemonic(m):
    """'LDW' -> ('LD', 2); 'SLL' -> ('SLL', None). Returns None if unknown."""
    u = m.upper()
    if u in MNEMONICS:
        return u, None
    if len(u) > 1 and u[-1] in SIZE_OF and u[:-1] in MNEMONICS:
        return u[:-1], SIZE_OF[u[-1]]
    return None


def _le(v, n):
    v &= (1 << (8 * n)) - 1
    return v.to_bytes(n, "little")


class _E:
    """One encode call: values, checks and helpers."""
    __slots__ = ("pc", "ev", "final", "m", "sfx")

    def __init__(self, m, sfx, pc, ev, final):
        self.m, self.sfx, self.pc, self.ev, self.final = m, sfx, pc, ev, final

    def err(self, msg):
        return EncodeError(msg)

    def mismatch(self, key=None):
        f = FORMS.get(key or self.m)
        if f:
            return EncodeError(f"operand type mismatch; legal forms: {f.format(m=self.m)}")
        return EncodeError("operand type mismatch")

    def val(self, e):
        return self.ev(e)

    def imm(self, e, size, lo=None, hi=None, what="value"):
        return self.immw(e, size, lo, hi, what)[0]

    def immw(self, e, size, lo=None, hi=None, what="value"):
        """Immediate value and its width hint (`:n` or a relocatable symbol's width)."""
        v, w = self.ev(e)
        if lo is None:
            lo, hi = -(1 << (8 * size - 1)), (1 << (8 * size)) - 1
            if self.final and w > 8 * size:
                raise EncodeError(f"a {w}-bit value does not fit a {8 * size}-bit operand")
        if self.final and not lo <= v <= hi:
            raise EncodeError(f"{what} {v} out of range {lo}..{hi}")
        return v, w

    def size(self, opsize, allowed, default=None):
        s = self.sfx
        if opsize is not None and s is not None and s != opsize:
            raise EncodeError(f"size suffix does not match the {opsize * 8}-bit operand")
        z = opsize or s or default
        if z is None:
            raise EncodeError("operand size unknown; add a B/W/L suffix")
        if z not in allowed:
            names = "/".join({1: "byte", 2: "word", 4: "long"}[a] for a in allowed)
            raise EncodeError(f"{self.m} is {names} only")
        return z

    def rel(self, target, end, bits):
        d = target - end
        lim = 1 << (bits - 1)
        if self.final and not -lim <= d < lim:
            raise EncodeError(f"relative target out of range ({d:+d} bytes, E227)")
        return _le(d, bits // 8)


def _reg_prefix(r):
    base = {1: 0xC8, 2: 0xD8, 4: 0xE8}[r.size]
    if r.short is not None:
        return bytes([base | r.short])
    return bytes([base - 1, r.code])


def _need_short(r, e):
    if r.short is None:
        raise e.err(f"{r.name} must be a current-bank register (W A B C D E H L / WA..SP / XWA..XSP) here")
    return r.short


def _mem(e, m, group, opsize):
    """Prefix bytes for a memory operand. group: 1/2/4 = source group of that size, 0 = dst."""
    base = {1: 0x80, 2: 0x90, 4: 0xA0, 0: 0xB0}[group]
    hi = base + 0x40
    mode = m.mode
    if mode in ("ind", "disp", "idx", "pre", "post"):
        r = m.reg
        if r.size != 4:
            raise e.err(f"memory base register {r.name} must be 32-bit")
    if mode == "ind":
        if r.short is not None:
            return bytes([base | r.short])
        return bytes([hi | 3, r.code])
    if mode == "disp":
        d, w = e.val(m.disp)
        if w > 16 and e.final:
            raise e.err(f"a displacement is at most 16 bits, not {w}")
        if w > 8 and 0x8000 <= d <= 0xFFFF:
            d -= 0x10000                     # (r-2:16): the masked value is signed
        if r.short is not None and -128 <= d <= 127 and w <= 8:
            return bytes([base | 8 | r.short, d & 0xFF])
        if -32768 <= d <= 32767 or not e.final:
            return bytes([hi | 3, r.code | 1]) + _le(d, 2)
        raise e.err(f"displacement {d} out of range -32768..32767")
    if mode == "idx":
        x = m.idx
        return bytes([hi | 3, 3 if x.size == 1 else 7, r.code, x.code])
    if mode in ("pre", "post"):
        step = m.step or opsize or 1
        return bytes([hi | (4 if mode == "pre" else 5), r.code | {1: 0, 2: 1, 4: 2}[step]])
    if mode == "abs":
        a, w = e.val(m.addr)
        if a < 0 or a > 0xFFFFFF:
            if e.final:
                raise e.err(f"address 0x{a & 0xFFFFFFFF:X} out of range")
            a &= 0xFFFFFF
        need = 1 if a < 0x100 else 2 if a < 0x10000 else 3
        if w > 24 and e.final:
            raise e.err(f"an absolute address is at most 24 bits, not {w}")
        n = max(need, (w + 7) // 8)
        if w and need * 8 > w and e.final:
            raise e.err(f"address 0x{a:X} does not fit the {w}-bit width of its symbol")
        return bytes([hi | (n - 1)]) + _le(a, n)
    raise e.err("block-transfer operand not allowed here")


def _abs16(e, m):
    """(#16) operand of LD<W> (#16),(mem) / (mem),(#16)."""
    a, w = e.val(m.addr)
    if e.final and not 0 <= a <= 0xFFFF:
        raise e.err(f"address 0x{a:X} is not 16-bit")
    if e.final and w > 16:
        raise e.err("a large (24-bit) symbol cannot be a #16 address")
    return _le(a, 2)


def _best(cands):
    return min(cands, key=lambda b: (len(b), b))


def encode(m, sfx, ops, pc, ev, final=True):
    """Bytes for mnemonic m (base, upper case) with size suffix sfx (1/2/4/None)."""
    e = _E(m, sfx, pc, ev, final)
    n = len(ops)
    k = [o.kind for o in ops]

    if m in SIMPLE:
        if n:
            raise e.err(f"{m} takes no operands")
        return SIMPLE[m]
    if m in ("NORMAL", "MAX"):
        raise e.err(f"{m} is undefined on the 900/H")

    if m == "LD":
        if n != 2:
            raise e.mismatch()
        a, b = ops
        if k == ["reg", "reg"]:
            if a.size != b.size:
                raise e.err("register sizes differ")
            e.size(a.size, (1, 2, 4))
            if a.short is not None:        # LD R,r: the prefix holds the source
                return _reg_prefix(b) + bytes([0x88 | a.short])
            if b.short is not None:
                return _reg_prefix(a) + bytes([0x98 | b.short])
            raise e.err("one of the registers must be a current-bank register")
        if k == ["reg", "imm"]:
            s = e.size(a.size, (1, 2, 4))
            v, w = e.immw(b.expr, s)
            short3 = 0 <= v <= 7 and w <= 3
            if short3 and (s > 1 or w or a.short is None):
                return _reg_prefix(a) + bytes([0xA8 | v])      # LD r,#3
            if a.short is not None:
                return bytes([{1: 0x20, 2: 0x30, 4: 0x40}[s] | a.short]) + _le(v, s)   # LD R,#
            return _reg_prefix(a) + b"\x03" + _le(v, s)     # LD r,#
        if k == ["reg", "mem"]:
            s = e.size(a.size, (1, 2, 4))
            return _mem(e, b, s, s) + bytes([0x20 | _need_short(a, e)])
        if k == ["mem", "reg"]:
            s = e.size(b.size, (1, 2, 4))
            return _mem(e, a, 0, s) + bytes([{1: 0x40, 2: 0x50, 4: 0x60}[s] | _need_short(b, e)])
        if k == ["mem", "imm"]:
            s = e.size(None, (1, 2), 1)
            v = e.imm(b.expr, s)
            c = [_mem(e, a, 0, s) + bytes([0 if s == 1 else 2]) + _le(v, s)]
            if a.mode == "abs":
                av, w = e.val(a.addr)
                if 0 <= av < 0x100 and w <= 8:
                    c.append(bytes([0x08 if s == 1 else 0x0A, av]) + _le(v, s))
            return _best(c)
        if k == ["mem", "mem"]:
            s = e.size(None, (1, 2), 1)
            c, why = [], None
            for dst_abs, form in ((a, 0), (b, 1)):
                if dst_abs.mode != "abs":
                    continue
                try:
                    if form == 0:      # LD<W> (#16),(mem)
                        c.append(_mem(e, b, s, s) + b"\x19" + _abs16(e, a))
                    else:              # LD<W> (mem),(#16)
                        c.append(_mem(e, a, 0, s) + bytes([0x14 if s == 1 else 0x16]) + _abs16(e, b))
                except EncodeError as x:
                    why = x
            if not c:
                raise why or e.err("one side of a memory-to-memory LD must be a 16-bit address")
            return _best(c)
        raise e.mismatch()

    if m in ("PUSH", "POP"):
        if n != 1:
            raise e.mismatch()
        a = ops[0]
        push = m == "PUSH"
        if a.kind == "special":
            if a.name == "F":
                return b"\x18" if push else b"\x19"
            if a.name == "SR":
                return b"\x02" if push else b"\x03"
            raise e.mismatch()
        if a.kind == "reg":
            s = e.size(a.size, (1, 2, 4))
            c = [_reg_prefix(a) + (b"\x04" if push else b"\x05")]
            if a.name == "A":
                c.append(b"\x14" if push else b"\x15")
            if s > 1 and a.short is not None:
                c.append(bytes([(0x28 if push else 0x48) + (0x10 if s == 4 else 0) + a.short]))
            return _best(c)
        if a.kind == "imm" and push:
            s = e.size(None, (1, 2), 1)
            return bytes([0x09 if s == 1 else 0x0B]) + _le(e.imm(a.expr, s), s)
        if a.kind == "mem":
            s = e.size(None, (1, 2), 1)
            if push:
                return _mem(e, a, s, s) + b"\x04"
            return _mem(e, a, 0, s) + bytes([4 if s == 1 else 6])
        raise e.mismatch()

    if m == "LDA":
        if n != 2 or k[0] != "reg" or k[1] != "mem":
            raise e.mismatch()
        a, b = ops
        s = e.size(a.size, (2, 4))
        return _mem(e, b, 0, None) + bytes([(0x20 if s == 2 else 0x30) | _need_short(a, e)])

    if m == "LDAR":
        if n != 2 or k[0] != "reg" or k[1] not in ("imm", "mem"):
            raise e.mismatch()
        a, b = ops
        s = e.size(a.size, (2, 4))
        expr = b.expr if b.kind == "imm" else (b.addr if b.mode == "abs" else None)
        if expr is None:
            raise e.mismatch()
        t, _ = e.val(expr)
        return b"\xF3\x13" + e.rel(t, pc + 4, 16) + bytes([(0x20 if s == 2 else 0x30) | _need_short(a, e)])

    if m == "EX":
        if n != 2:
            raise e.mismatch()
        a, b = ops
        if k == ["special", "special"] and a.name == "F" and b.name == "F'":
            return b"\x16"
        if k == ["reg", "reg"]:
            if a.size != b.size:
                raise e.err("register sizes differ")
            e.size(a.size, (1, 2))
            if a.short is not None:
                return _reg_prefix(b) + bytes([0xB8 | a.short])
            if b.short is not None:
                return _reg_prefix(a) + bytes([0xB8 | b.short])
            raise e.err("one of the registers must be a current-bank register")
        if k in (["mem", "reg"], ["reg", "mem"]):
            mem, r = (a, b) if k[0] == "mem" else (b, a)
            s = e.size(r.size, (1, 2))
            return _mem(e, mem, s, s) + bytes([0x30 | _need_short(r, e)])
        raise e.mismatch()

    if m in SINGLE:
        op, allowed = SINGLE[m]
        if n != 1 or k[0] != "reg":
            raise e.err(f"{m} takes one register")
        e.size(ops[0].size, allowed)
        return _reg_prefix(ops[0]) + bytes([op])

    if m in BLOCK:
        s = e.size(None, (1, 2), 1)
        if n == 0:
            p = 3
        elif n == 2 and k == ["mem", "mem"]:
            want = "dec" if m in ("LDD", "LDDR") else "post"
            a, b = ops
            names = (a.reg.name if a.reg else None, b.reg.name if b.reg else None)
            ok_mode = a.mode == want and b.mode == want and a.step is None and b.step is None
            if ok_mode and names == ("XDE", "XHL"):
                p = 3
            elif ok_mode and names == ("XIX", "XIY"):
                p = 5
            else:
                sign = "-" if want == "dec" else "+"
                raise e.err(f"{m} operands are (XDE{sign}),(XHL{sign}) or (XIX{sign}),(XIY{sign})")
        else:
            raise e.mismatch()
        return bytes([(0x80 if s == 1 else 0x90) | p, BLOCK[m]])

    if m in CPBLK:
        want = "dec" if m in ("CPD", "CPDR") else "post"
        if n == 0:
            s = e.size(None, (1, 2), 1)
            r = 3
        elif n == 2 and k == ["reg", "mem"] and ops[0].name in ("A", "WA") and ops[1].mode == want \
                and ops[1].step is None and ops[1].reg.short is not None:
            s = e.size(ops[0].size, (1, 2))
            r = ops[1].reg.short
        else:
            sign = "-" if want == "dec" else "+"
            raise e.err(f"{m} operands are A,(R{sign}) or WA,(R{sign})")
        return bytes([(0x80 if s == 1 else 0x90) | r, CPBLK[m]])

    if m in ALUS:
        i = ALUS[m]
        key = "CP" if m == "CP" else "ALU"
        if n != 2:
            raise e.mismatch(key)
        a, b = ops
        if k == ["reg", "reg"]:
            if a.size != b.size:
                raise e.err("register sizes differ")
            e.size(a.size, (1, 2, 4))
            return _reg_prefix(b) + bytes([0x80 + 0x10 * i + _need_short(a, e)])
        if k == ["reg", "imm"]:
            s = e.size(a.size, (1, 2, 4))
            v, w = e.immw(b.expr, s)
            if m == "CP" and s < 4 and 0 <= v <= 7 and w <= 3:
                return _reg_prefix(a) + bytes([0xD8 | v])      # CP r,#3 (byte/word only)
            return _reg_prefix(a) + bytes([0xC8 + i]) + _le(v, s)
        if k == ["reg", "mem"]:
            s = e.size(a.size, (1, 2, 4))
            return _mem(e, b, s, s) + bytes([0x80 + 0x10 * i + _need_short(a, e)])
        if k == ["mem", "reg"]:
            s = e.size(b.size, (1, 2, 4))
            return _mem(e, a, s, s) + bytes([0x88 + 0x10 * i + _need_short(b, e)])
        if k == ["mem", "imm"]:
            s = e.size(None, (1, 2), 1)
            return _mem(e, a, s, s) + bytes([0x38 + i]) + _le(e.imm(b.expr, s), s)
        raise e.mismatch(key)

    if m in ("INC", "DEC"):
        if n != 2 or k[0] != "imm" or k[1] not in ("reg", "mem"):
            raise e.mismatch("INC")
        c = e.imm(ops[0].expr, 1, 1, 8, "count")
        op = (0x60 if m == "INC" else 0x68) | (c & 7)
        b = ops[1]
        if b.kind == "reg":
            e.size(b.size, (1, 2, 4))
            return _reg_prefix(b) + bytes([op])
        s = e.size(None, (1, 2), 1)
        return _mem(e, b, s, s) + bytes([op])

    if m in MULS:
        i = MULS[m]
        if n != 2 or k[0] != "reg":
            raise e.mismatch("MUL")
        rr, b = ops
        if rr.size not in (2, 4):
            raise e.err(f"{m}: the first operand is the double-size register (WA.. or XWA..)")
        s = e.size(rr.size // 2, (1, 2))
        if b.kind == "imm":
            v = e.imm(b.expr, s)
            if s == 1:
                pre = bytes([0xC8 | (2 * rr.short + 1)]) if rr.short is not None and rr.short < 4 \
                    else bytes([0xC7, rr.code])
            else:
                pre = bytes([0xD8 | rr.short]) if rr.short is not None else bytes([0xD7, rr.code])
            return pre + bytes([0x08 + i]) + _le(v, s)
        if s == 1:
            if rr.short is None or rr.short > 3:
                raise e.err(f"{m} RR,r: RR must be WA, BC, DE or HL")
            R = 2 * rr.short + 1
        else:
            R = _need_short(rr, e)
        op = 0x40 + 8 * i + R
        if b.kind == "reg":
            if b.size != s:
                raise e.err("register sizes do not fit")
            return _reg_prefix(b) + bytes([op])
        if b.kind == "mem":
            return _mem(e, b, s, s) + bytes([op])
        raise e.mismatch("MUL")

    if m in MINC:
        op, step = MINC[m]
        if n != 2 or k != ["imm", "reg"]:
            raise e.err(f"{m} takes #,r")
        e.size(ops[1].size, (2,))
        v = e.imm(ops[0].expr, 2)
        return _reg_prefix(ops[1]) + bytes([op]) + _le(v - step, 2)

    if m in CFS or m in BITS_REG:
        key = "CF" if m in CFS else "BIT"
        if n != 2:
            raise e.mismatch(key)
        a, b = ops
        if a.kind == "reg" and a.name == "A" and m in CFS:
            if b.kind == "reg":
                e.size(b.size, (1, 2))
                return _reg_prefix(b) + bytes([0x28 + CFS[m]])
            if b.kind == "mem":
                e.size(None, (1,), 1)
                return _mem(e, b, 0, 1) + bytes([0x28 + CFS[m]])
            raise e.mismatch(key)
        if a.kind != "imm":
            raise e.mismatch(key)
        if b.kind == "reg":
            e.size(b.size, (1, 2))
            v = e.imm(a.expr, 1, 0, 15, "bit number")
            op = 0x20 + CFS[m] if m in CFS else BITS_REG[m]
            return _reg_prefix(b) + bytes([op, v])
        if b.kind == "mem":
            e.size(None, (1,), 1)
            v = e.imm(a.expr, 1, 0, 7, "bit number")
            op = (0x80 + 8 * CFS[m]) if m in CFS else BITS_MEM[m]
            return _mem(e, b, 0, 1) + bytes([op + (v & 7)])
        raise e.mismatch(key)

    if m in SHIFTS:
        i = SHIFTS[m]
        if n == 1 and k[0] == "mem":
            s = e.size(None, (1, 2), 1)
            return _mem(e, ops[0], s, s) + bytes([0x78 + i])
        if n == 2 and k[1] == "reg":
            a, b = ops
            e.size(b.size, (1, 2, 4))
            if a.kind == "reg" and a.name == "A":
                return _reg_prefix(b) + bytes([0xF8 + i])
            if a.kind == "imm":
                v = e.imm(a.expr, 1, 1, 16, "shift count")
                return _reg_prefix(b) + bytes([0xE8 + i, v & 0xF])
        raise e.mismatch("SHIFT")

    if m in ("RLD", "RRD"):
        if n == 2 and k[0] == "reg" and ops[0].name == "A":
            ops = ops[1:]
            n = 1
        if n != 1 or ops[0].kind != "mem":
            raise e.err(f"{m} takes [A,](mem)")
        e.size(None, (1,), 1)
        return _mem(e, ops[0], 1, 1) + (b"\x06" if m == "RLD" else b"\x07")

    if m in ("BS1F", "BS1B"):
        if n != 2 or k != ["reg", "reg"] or ops[0].name != "A":
            raise e.err(f"{m} takes A,r")
        e.size(ops[1].size, (2,))
        return _reg_prefix(ops[1]) + (b"\x0E" if m == "BS1F" else b"\x0F")

    if m == "EI":
        v = e.imm(ops[0].expr, 1, 0, 7, "interrupt level") if n == 1 and k[0] == "imm" else 0
        if n > 1 or (n == 1 and k[0] != "imm"):
            raise e.err("EI takes [#3]")
        return bytes([0x06, v])
    if m == "SWI":
        if n > 1 or (n == 1 and k[0] != "imm"):
            raise e.err("SWI takes [#3]")
        v = e.imm(ops[0].expr, 1, 0, 7, "SWI number") if n else 7
        return bytes([0xF8 | v])
    if m == "LDF":
        if n != 1 or k[0] != "imm":
            raise e.err("LDF takes #3")
        return bytes([0x17, e.imm(ops[0].expr, 1, 0, 7, "register bank")])
    if m == "RETD":
        if n != 1 or k[0] != "imm":
            raise e.err("RETD takes d16")
        return b"\x0F" + _le(e.imm(ops[0].expr, 2, -32768, 65535), 2)
    if m == "RET":
        if n == 0:
            return b"\x0E"
        if n == 1 and k[0] == "cc":
            cc = ops[0].code
            return b"\x0E" if cc == 8 else bytes([0xB0, 0xF0 | cc])
        raise e.err("RET takes [cc]")

    if m == "LDC":
        if n != 2:
            raise e.err("LDC takes cr,r or r,cr")
        a, b = ops
        if k == ["cr", "reg"]:
            cr, r, op = a, b, 0x2E
        elif k == ["reg", "cr"]:
            r, cr, op = a, b, 0x2F
        else:
            raise e.err("LDC takes cr,r or r,cr")
        if r.size != cr.size:
            raise e.err(f"{cr.name} is {cr.size * 8}-bit")
        e.size(r.size, (1, 2, 4))
        return _reg_prefix(r) + bytes([op, cr.code])

    if m == "LINK":
        if n != 2 or k != ["reg", "imm"]:
            raise e.err("LINK takes r,d16")
        e.size(ops[0].size, (4,))
        return _reg_prefix(ops[0]) + b"\x0C" + _le(e.imm(ops[1].expr, 2, -32768, 65535), 2)

    if m == "SCC":
        if n != 2 or k != ["cc", "reg"]:
            raise e.err("SCC takes cc,r")
        e.size(ops[1].size, (1, 2))
        return _reg_prefix(ops[1]) + bytes([0x70 | ops[0].code])

    if m == "LDX":
        if n != 2 or k != ["mem", "imm"] or ops[0].mode != "abs":
            raise e.err("LDX takes (#8),#")
        a, _ = e.val(ops[0].addr)
        if final and not 0 <= a <= 0xFF:
            raise e.err("LDX address must be 8-bit")
        return bytes([0xF7, 0, a & 0xFF, 0, e.imm(ops[1].expr, 1) & 0xFF, 0])

    if m in JUMPS:
        return _jump(e, m, ops, k)

    raise e.err(f"unknown mnemonic {m}")


def _target(e, op):
    """Value of a branch target operand (expression or bare absolute address)."""
    if op.kind == "imm":
        return e.val(op.expr)
    if op.kind == "mem" and op.mode == "abs":
        return e.val(op.addr)
    raise e.err("branch target must be an address")


def _jump(e, m, ops, k):
    pc = e.pc
    cc = 8
    if ops and k[0] == "cc":
        cc = ops[0].code
        ops = ops[1:]
        k = k[1:]
    if m == "DJNZ":
        if len(ops) == 2 and k[0] == "reg":
            r, t = ops
        elif len(ops) == 1:
            from .operands import Reg as _R
            r, t = _R("B"), ops[0]
        else:
            raise e.err("DJNZ takes [r,]label")
        e.size(r.size, (1, 2))
        pre = _reg_prefix(r)
        tv, _ = _target(e, t)
        if len(pre) == 1 and tv - (pc + 3) == 128:
            pre = bytes([pre[0] - 1 - (pre[0] & 7), r.code])   # only the long form reaches
        end = pc + len(pre) + 2
        return pre + b"\x1C" + e.rel(tv, end, 8)
    if len(ops) != 1:
        raise e.err(f"{m} takes [cc,]address")
    t = ops[0]
    if m in ("JR", "JRL"):
        tv, _ = _target(e, t)
        if m == "JR":
            return bytes([0x60 | cc]) + e.rel(tv, pc + 2, 8)
        return bytes([0x70 | cc]) + e.rel(tv, pc + 3, 16)
    if m == "CALR":
        if cc != 8:
            raise e.err("CALR has no condition")
        tv, _ = _target(e, t)
        return b"\x1E" + e.rel(tv, pc + 3, 16)
    call = m in ("CALL", "CAL")
    if t.kind == "mem" and t.mode != "abs":
        return _mem(e, t, 0, None) + bytes([(0xE0 if call else 0xD0) | cc])
    tv, w = _target(e, t)
    c = []
    if m in ("J", "CAL") and w == 0:       # only absolute targets relax
        if m == "J":
            if -128 <= tv - (pc + 2) < 128:
                c.append(bytes([0x60 | cc]) + _le(tv - (pc + 2), 1))
            if -32768 <= tv - (pc + 3) < 32768:
                c.append(bytes([0x70 | cc]) + _le(tv - (pc + 3), 2))
        elif cc == 8 and -32768 <= tv - (pc + 3) < 32768:
            c.append(b"\x1E" + _le(tv - (pc + 3), 2))
    if cc == 8:
        if (w and w <= 16) or (w == 0 and 0 <= tv < 0x10000):
            if e.final and not 0 <= tv < 0x10000:
                raise e.err(f"address 0x{tv & 0xFFFFFFFF:X} does not fit the symbol's medium displacement")
            c.append(bytes([0x1C if call else 0x1A]) + _le(tv, 2))
        else:
            if e.final and not 0 <= tv <= 0xFFFFFF:
                raise e.err(f"address 0x{tv & 0xFFFFFFFF:X} out of range")
            c.append(bytes([0x1D if call else 0x1B]) + _le(tv, 3))
    else:
        c.append(_mem(e, Mem("abs", addr=t.expr if t.kind == "imm" else t.addr), 0, None)
                 + bytes([(0xE0 if call else 0xD0) | cc]))
    return _best(c)


__all__ = ["encode", "split_mnemonic", "EncodeError", "COND_FIRST", "BARE_ADDR", "MNEMONICS",
           "Reg", "Mem", "Imm", "Cond", "Special", "Ctl", "SHORT", "CC_CODE"]
