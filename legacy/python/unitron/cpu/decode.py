"""TLCS-900/H instruction decoder (the NGPC CPU: the 900 core in permanent maximum mode).

Built from the Toshiba TLCS-900 CPU datasheet (instruction lists and code maps). Text output is
asm900 syntax: lowercase mnemonic, uppercase registers, `jp XHL`, `(XHL+:1)` for a step that
differs from the operand size.
"""

from .regs import REG8, REG16, REG32, CC, ALU, SHIFT, SFX, CR, ext_reg


def hx(v):
    return f"0x{v:X}" if v > 9 else str(v)


def s8(v):
    return v - 0x100 if v & 0x80 else v


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


class Ref(str):
    """Operand text that carries an absolute address (for labels). kind: 'code' or 'mem'."""
    def __new__(cls, text, addr, kind):
        o = super().__new__(cls, text)
        o.addr, o.kind = addr, kind
        return o


class Insn:
    __slots__ = ("addr", "size", "mnem", "ops", "target", "is_call", "is_jump", "conditional",
                 "ends_block", "invalid", "illegal", "raw", "mem_refs", "imm32", "dst_reg", "comment")

    def __init__(self, addr):
        self.addr = addr
        self.size = 0
        self.mnem = "?"
        self.ops = []
        self.target = None        # direct branch/call target
        self.is_call = self.is_jump = self.conditional = self.ends_block = False
        self.invalid = False      # no instruction decodes here
        self.illegal = None       # decodes, but is not a datasheet form (reason)
        self.raw = b""
        self.mem_refs = []        # absolute data addresses referenced
        self.imm32 = None         # 32-bit immediate loaded into a register
        self.dst_reg = None
        self.comment = None

    def text(self, sym=None):
        """asm900 text; sym(op) may replace operands (e.g. addresses by labels)."""
        ops = [sym(o) for o in self.ops] if sym else self.ops
        m = self.mnem.lower()
        return f"{m}\t{','.join(ops)}" if ops else m

    def __repr__(self):
        return f"Insn({self.addr:06X} {self.raw.hex()} {self.text()!r})"


class _Rd:
    __slots__ = ("mem", "pc")

    def __init__(self, mem, addr):
        self.mem, self.pc = mem, addr

    def u8(self):
        v = self.mem(self.pc)
        if v is None:
            raise EOFError
        self.pc += 1
        return v

    def u16(self):
        return self.u8() | (self.u8() << 8)

    def u24(self):
        return self.u16() | (self.u8() << 16)

    def u32(self):
        return self.u16() | (self.u16() << 16)

    def imm(self, size):
        return self.u8() if size == 1 else self.u16() if size == 2 else self.u32()


def _bad(ins, why):
    if ins.illegal is None:
        ins.illegal = why


def _reg_ext(ins, code, size):
    n = ext_reg(code, size)
    if n is None:
        _bad(ins, f"undefined register code 0x{code:02X}")
        return f"r?{code:02X}"
    if (size == 2 and code & 1) or (size == 4 and code & 3):
        _bad(ins, f"register code 0x{code:02X} has low bits set")
    return n


def _mem_operand(rd, p, opsize, ins):
    """Memory operand selected by prefix byte p (text with parentheses)."""
    if p < 0xC0:                                   # 80-BF: (r32) / (r32+d8)
        r = REG32[p & 7]
        if p & 8:
            d = s8(rd.u8())
            return f"({r}{'+' if d >= 0 else '-'}{hx(abs(d))})"
        return f"({r})"
    mode = p & 7
    if mode in (0, 1, 2):
        a = [rd.u8, rd.u16, rd.u24][mode]()
        ins.mem_refs.append(a)
        return Ref(f"({hx(a)})", a, "mem")
    if mode == 3:
        b = rd.u8()
        m = b & 3
        if m == 0:
            return f"({_reg_ext(ins, b & 0xFC, 4)})"
        if m == 1:
            d = s16(rd.u16())
            return f"({_reg_ext(ins, b & 0xFC, 4)}{'+' if d >= 0 else '-'}{hx(abs(d))})"
        if b in (0x03, 0x07):
            r, x = rd.u8(), rd.u8()
            return f"({_reg_ext(ins, r, 4)}+{_reg_ext(ins, x, 1 if b == 3 else 2)})"
        ins.invalid = True
        return "(?)"
    if mode in (4, 5):
        b = rd.u8()
        inc = {0: 1, 1: 2, 2: 4}.get(b & 3)
        if inc is None:
            ins.invalid = True
            inc = 0
        r = _reg_ext(ins, b & 0xFC, 4)
        note = "" if inc == (opsize or 1) else f":{inc}"
        return f"(-{r}{note})" if mode == 4 else f"({r}+{note})"
    ins.invalid = True
    return "(?)"


def _dbl(R, size):
    """RR operand of MUL/DIV: the register twice the operation size holding R."""
    return REG16[R >> 1] if size == 1 else REG32[R]


def _regname(R, size):
    return (REG8 if size == 1 else REG16 if size == 2 else REG32)[R]


def _set(ins, mnem, *ops):
    ins.mnem = mnem
    ins.ops = list(ops)


def _decode_reg(rd, p, ins):
    size = {0xC: 1, 0xD: 2, 0xE: 4}[p >> 4]
    if p & 0xF == 7:
        code = rd.u8()
        r = _reg_ext(ins, code, size)
        if size < 4:
            rr = ext_reg(code & (0xFE if size == 1 else 0xFC), size * 2) or "?"
        else:
            rr = "?"
    else:
        idx = p & 7
        r = _regname(idx, size)
        rr = _dbl(idx, size) if size < 4 else "?"
    op = rd.u8()
    R = op & 7
    Rn = _regname(R, size)
    M = _set

    if op == 0x03:
        v = rd.imm(size)
        M(ins, "LD", r, hx(v))
        if size == 4:
            ins.imm32, ins.dst_reg = v, r
    elif op in (0x04, 0x05):
        M(ins, "PUSH" if op == 4 else "POP", r)
    elif op in (0x06, 0x07):
        M(ins, "CPL" if op == 6 else "NEG", r)
        if size == 4:
            _bad(ins, "CPL/NEG are byte/word only")
    elif 0x08 <= op <= 0x0B and size < 4:
        M(ins, ["MUL", "MULS", "DIV", "DIVS"][op - 8], rr, hx(rd.imm(size)))
    elif op == 0x0C and size == 4:
        M(ins, "LINK", r, str(s16(rd.u16())))
    elif op == 0x0D and size == 4:
        M(ins, "UNLK", r)
    elif op in (0x0E, 0x0F) and size == 2:
        M(ins, "BS1F" if op == 0x0E else "BS1B", "A", r)
    elif op == 0x10 and size == 1:
        M(ins, "DAA", r)
    elif op in (0x12, 0x13, 0x14) and size > 1:
        M(ins, ["EXTZ", "EXTS", "PAA"][op - 0x12], r)
    elif op == 0x16 and size == 2:
        M(ins, "MIRR", r)
    elif op == 0x19 and size == 2:
        M(ins, "MULA", r)
    elif op == 0x1C and size < 4:
        d = s8(rd.u8())
        ins.target = (rd.pc + d) & 0xFFFFFF
        ins.is_jump = ins.conditional = True
        M(ins, "DJNZ", r, Ref(hx(ins.target), ins.target, "code"))
    elif 0x20 <= op <= 0x24 and size < 4:
        b = rd.u8()
        if b > 15:
            _bad(ins, "bit number above 15")
        M(ins, ["ANDCF", "ORCF", "XORCF", "LDCF", "STCF"][op - 0x20], hx(b), r)
    elif 0x28 <= op <= 0x2C and size < 4:
        M(ins, ["ANDCF", "ORCF", "XORCF", "LDCF", "STCF"][op - 0x28], "A", r)
    elif op in (0x2E, 0x2F):
        c = rd.u8()
        cr = CR.get((c, size))
        if cr is None:
            cr = f"cr{c:02X}"
            _bad(ins, f"undefined control register 0x{c:02X}")
        M(ins, "LDC", *((cr, r) if op == 0x2E else (r, cr)))
    elif 0x30 <= op <= 0x34 and size < 4:
        b = rd.u8()
        if b > 15:
            _bad(ins, "bit number above 15")
        M(ins, ["RES", "SET", "CHG", "BIT", "TSET"][op - 0x30], hx(b), r)
    elif op in (0x38, 0x39, 0x3A, 0x3C, 0x3D, 0x3E) and size == 2:
        n = [1, 2, 4][op & 3]
        v = (rd.u16() + n) & 0xFFFF
        M(ins, ("MINC" if op < 0x3C else "MDEC") + str(n), hx(v), r)
    elif 0x40 <= op <= 0x5F and size < 4 and (size == 2 or R & 1):
        M(ins, ["MUL", "MULS", "DIV", "DIVS"][(op - 0x40) >> 3], _dbl(R, size), r)
    elif 0x60 <= op <= 0x6F:
        M(ins, "INC" if op < 0x68 else "DEC", str(R or 8), r)
    elif 0x70 <= op <= 0x7F and size < 4:
        M(ins, "SCC", CC[op & 0xF], r)
    elif op >= 0x80:
        hi, lo8 = op >> 4, op & 8
        if not lo8:
            M(ins, ["ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"][hi - 8], Rn, r)
        elif hi == 8:
            M(ins, "LD", Rn, r)
        elif hi == 9:
            M(ins, "LD", r, Rn)
        elif hi == 0xA:
            M(ins, "LD", r, str(R))
        elif hi == 0xB:
            if size == 4:
                ins.invalid = True
            M(ins, "EX", Rn, r)
        elif hi == 0xC:
            M(ins, ALU[R], r, hx(rd.imm(size)))
        elif hi == 0xD:
            M(ins, "CP", r, str(R))
            if size == 4:
                _bad(ins, "CP r,#3 is byte/word only")
        elif hi == 0xE:
            b = rd.u8()
            if b > 15:
                _bad(ins, "shift count byte above 15")
            M(ins, SHIFT[R], str((b & 0xF) or 16), r)
        else:
            M(ins, SHIFT[R], "A", r)
    else:
        ins.invalid = True


def _decode_src(rd, p, ins):
    size = {0x8: 1, 0x9: 2, 0xA: 4, 0xC: 1, 0xD: 2, 0xE: 4}[p >> 4]
    mem = _mem_operand(rd, p, size, ins)
    op = rd.u8()
    R = op & 7
    Rn = _regname(R, size)
    sfx = SFX[size]
    M = _set
    bw = size < 4

    if op == 0x04 and bw:
        M(ins, "PUSH" + sfx, mem)
    elif op in (0x06, 0x07) and size == 1:
        M(ins, "RLD" if op == 0x06 else "RRD", "A", mem)
    elif 0x10 <= op <= 0x17 and bw and p < 0xA0 and (p & 8) == 0 and \
            (op >= 0x14 or (p & 7) in (3, 5)):
        name = ["LDI", "LDIR", "LDD", "LDDR", "CPI", "CPIR", "CPD", "CPDR"][op - 0x10]
        name += "W" if size == 2 else ""
        d = "+" if op in (0x10, 0x11, 0x14, 0x15) else "-"
        if op < 0x14:
            a, b = ("XDE", "XHL") if (p & 7) == 3 else ("XIX", "XIY")
            M(ins, name, f"({a}{d})", f"({b}{d})")
        else:
            M(ins, name, "A" if size == 1 else "WA", f"({REG32[p & 7]}{d})")
    elif op == 0x19 and bw:
        a = rd.u16()
        ins.mem_refs.append(a)
        M(ins, "LD" + sfx, Ref(f"({hx(a)})", a, "mem"), mem)
    elif 0x20 <= op <= 0x27:
        M(ins, "LD", Rn, mem)
    elif 0x30 <= op <= 0x37 and bw:
        M(ins, "EX", mem, Rn)
    elif 0x38 <= op <= 0x3F and bw:
        M(ins, ALU[R] + sfx, mem, hx(rd.imm(size)))
    elif 0x40 <= op <= 0x5F and bw and (size == 2 or R & 1):
        M(ins, ["MUL", "MULS", "DIV", "DIVS"][(op - 0x40) >> 3], _dbl(R, size), mem)
    elif 0x60 <= op <= 0x6F and bw:
        M(ins, ("INC" if op < 0x68 else "DEC") + sfx, str(R or 8), mem)
    elif 0x78 <= op <= 0x7F and bw:
        M(ins, SHIFT[R] + sfx, mem)
    elif op >= 0x80:
        name = ALU[(op >> 4) - 8]
        M(ins, name, *((Rn, mem) if not op & 8 else (mem, Rn)))
    else:
        ins.invalid = True


def _bare(mem):
    """`(XIX+2)` -> `XIX+2` (JP/CALL/LDA operands have no parentheses in asm900)."""
    if isinstance(mem, Ref):
        return Ref(mem[1:-1], mem.addr, mem.kind)
    return mem[1:-1]


def _decode_dst(rd, p, ins):
    M = _set
    if p == 0xF3 and rd.mem(rd.pc) == 0x13:
        # LDAR R,$+4+d16: F3 13 d16 (20+R | 30+R) [datasheet LDAR, "$ + 4 + d16"]
        rd.u8()
        d = s16(rd.u16())
        base = rd.pc
        op = rd.u8()
        tgt = (base + d) & 0xFFFFFF
        size = 2 if (op & 0xF8) == 0x20 else 4 if (op & 0xF8) == 0x30 else None
        if size is None:
            ins.invalid = True
            return
        ins.mem_refs.append(tgt)
        M(ins, "LDAR", _regname(op & 7, size), Ref(hx(tgt), tgt, "mem"))
        return
    mem = _mem_operand(rd, p, None, ins)
    op = rd.u8()
    R = op & 7
    dsize = {0x00: 1, 0x04: 1, 0x14: 1, 0x02: 2, 0x06: 2, 0x16: 2}.get(op) or \
        {4: 1, 5: 2, 6: 4}.get(op >> 4) or (1 if 0x80 <= op <= 0xCF or 0x28 <= op <= 0x2C else None)
    if dsize and dsize != 1 and not isinstance(mem, Ref):
        # _mem_operand assumed operand size 1; re-spell the step for the real size
        mem = _respell_step(mem, dsize)
    if op == 0x00:
        M(ins, "LDB", mem, hx(rd.u8()))
    elif op == 0x02:
        M(ins, "LDW", mem, hx(rd.u16()))
    elif op == 0x04:
        M(ins, "POPB", mem)
    elif op == 0x06:
        M(ins, "POPW", mem)
    elif op in (0x14, 0x16):
        a = rd.u16()
        ins.mem_refs.append(a)
        M(ins, "LDB" if op == 0x14 else "LDW", mem, Ref(f"({hx(a)})", a, "mem"))
    elif 0x20 <= op <= 0x27:
        M(ins, "LDA", REG16[R], _bare(mem))
    elif 0x30 <= op <= 0x37:
        M(ins, "LDA", REG32[R], _bare(mem))
    elif 0x28 <= op <= 0x2C:
        M(ins, ["ANDCF", "ORCF", "XORCF", "LDCF", "STCF"][op - 0x28], "A", mem)
    elif 0x40 <= op <= 0x47:
        M(ins, "LD", mem, REG8[R])
    elif 0x50 <= op <= 0x57:
        M(ins, "LD", mem, REG16[R])
    elif 0x60 <= op <= 0x67:
        M(ins, "LD", mem, REG32[R])
    elif 0x80 <= op <= 0xCF:
        M(ins, ["ANDCF", "ORCF", "XORCF", "LDCF", "STCF", "TSET", "RES", "SET", "CHG", "BIT"]
          [(op - 0x80) >> 3], str(R), mem)
    elif 0xD0 <= op <= 0xEF:
        cc = op & 0xF
        call = op >= 0xE0
        ins.is_call, ins.is_jump = call, not call
        ins.conditional = cc != 8
        ins.ends_block = (cc == 8) and not call
        if isinstance(mem, Ref):
            ins.target = mem.addr
            ins.mem_refs.remove(mem.addr)
            tgt = Ref(hx(mem.addr), mem.addr, "code")
        else:
            tgt = _bare(mem)
        M(ins, "CALL" if call else "JP", *([tgt] if cc == 8 else [CC[cc], tgt]))
    elif op >= 0xF0 and p == 0xB0:
        cc = op & 0xF
        ins.ends_block = cc == 8
        ins.conditional = cc != 8
        M(ins, "RET", *([] if cc == 8 else [CC[cc]]))
    else:
        ins.invalid = True


def _respell_step(mem, size):
    """Adjust an (r+)/(-r) operand decoded with an assumed size 1 to the real operand size."""
    if mem.startswith("(-"):
        body = mem[2:-1]
        r, _, n = body.partition(":")
        n = int(n) if n else 1
        return f"(-{r}{'' if n == size else f':{n}'})"
    if mem.endswith("+)") or "+:" in mem:
        body = mem[1:-1]
        r, _, n = body.partition("+")
        n = int(n[1:]) if n.startswith(":") else 1
        return f"({r}+{'' if n == size else f':{n}'})"
    return mem


def decode(mem, addr):
    """Decode one instruction. mem(addr) -> byte or None."""
    rd = _Rd(mem, addr)
    ins = Insn(addr)
    M = _set
    try:
        p = rd.u8()

        def code_ref(t):
            t &= 0xFFFFFF
            ins.target = t
            return Ref(hx(t), t, "code")

        if p == 0x00:
            M(ins, "NOP")
        elif p in (0x01, 0x04):
            M(ins, "NORMAL" if p == 1 else "MAX")
            _bad(ins, "900 minimum/maximum mode switch; undefined on 900/H")
        elif p == 0x02:
            M(ins, "PUSH", "SR")
        elif p == 0x03:
            M(ins, "POP", "SR")
        elif p == 0x05:
            M(ins, "HALT")
        elif p == 0x06:
            n = rd.u8()
            if n > 7:
                _bad(ins, "EI level above 7")
            M(ins, "DI") if n == 7 else M(ins, "EI", str(n))
        elif p == 0x07:
            M(ins, "RETI")
            ins.ends_block = True
        elif p == 0x08:
            a = rd.u8()
            v = rd.u8()
            ins.mem_refs.append(a)
            M(ins, "LDB", Ref(f"({hx(a)})", a, "mem"), hx(v))
        elif p == 0x09:
            M(ins, "PUSH", hx(rd.u8()))
        elif p == 0x0A:
            a = rd.u8()
            v = rd.u16()
            ins.mem_refs.append(a)
            M(ins, "LDW", Ref(f"({hx(a)})", a, "mem"), hx(v))
        elif p == 0x0B:
            M(ins, "PUSHW", hx(rd.u16()))
        elif p == 0x0C:
            M(ins, "INCF")
        elif p == 0x0D:
            M(ins, "DECF")
        elif p == 0x0E:
            M(ins, "RET")
            ins.ends_block = True
        elif p == 0x0F:
            M(ins, "RETD", str(s16(rd.u16())))
            ins.ends_block = True
        elif 0x10 <= p <= 0x13:
            M(ins, ["RCF", "SCF", "CCF", "ZCF"][p - 0x10])
        elif p == 0x14:
            M(ins, "PUSH", "A")
        elif p == 0x15:
            M(ins, "POP", "A")
        elif p == 0x16:
            M(ins, "EX", "F", "F'")
        elif p == 0x17:
            n = rd.u8()
            if n > 7:
                _bad(ins, "LDF bank above 7")
            M(ins, "LDF", str(n))
        elif p == 0x18:
            M(ins, "PUSH", "F")
        elif p == 0x19:
            M(ins, "POP", "F")
        elif p in (0x1A, 0x1B, 0x1C, 0x1D):
            a = rd.u16() if p in (0x1A, 0x1C) else rd.u24()
            call = p >= 0x1C
            ins.is_call, ins.is_jump, ins.ends_block = call, not call, not call
            M(ins, "CALL" if call else "JP", code_ref(a))
        elif p == 0x1E:
            d = s16(rd.u16())
            ins.is_call = True
            M(ins, "CALR", code_ref(rd.pc + d))
        elif 0x20 <= p <= 0x27:
            M(ins, "LD", REG8[p & 7], hx(rd.u8()))
        elif 0x28 <= p <= 0x2F:
            M(ins, "PUSH", REG16[p & 7])
        elif 0x30 <= p <= 0x37:
            M(ins, "LD", REG16[p & 7], hx(rd.u16()))
        elif 0x38 <= p <= 0x3F:
            M(ins, "PUSH", REG32[p & 7])
        elif 0x40 <= p <= 0x47:
            v = rd.u32()
            ins.imm32, ins.dst_reg = v, REG32[p & 7]
            M(ins, "LD", REG32[p & 7], hx(v))
        elif 0x48 <= p <= 0x4F:
            M(ins, "POP", REG16[p & 7])
        elif 0x58 <= p <= 0x5F:
            M(ins, "POP", REG32[p & 7])
        elif 0x60 <= p <= 0x7F:
            d = s8(rd.u8()) if p < 0x70 else s16(rd.u16())
            cc = p & 0xF
            ins.is_jump = True
            ins.conditional = cc != 8
            ins.ends_block = cc == 8
            t = code_ref(rd.pc + d)
            M(ins, "JR" if p < 0x70 else "JRL", *([t] if cc == 8 else [CC[cc], t]))
        elif p & 0xF0 in (0x80, 0x90, 0xA0) or (p & 0xF0 in (0xC0, 0xD0, 0xE0) and (p & 0xF) <= 5):
            _decode_src(rd, p, ins)
        elif p & 0xF0 == 0xB0 or (p & 0xF0 == 0xF0 and (p & 0xF) <= 5):
            _decode_dst(rd, p, ins)
        elif p & 0xF0 in (0xC0, 0xD0, 0xE0) and (p & 0xF) >= 7:
            _decode_reg(rd, p, ins)
        elif p == 0xF7:
            # LDX (#8),#: F7 00 #8 00 # 00
            z1 = rd.u8()
            a = rd.u8()
            z2 = rd.u8()
            b = rd.u8()
            z3 = rd.u8()
            if z1 or z2 or z3:
                _bad(ins, "LDX filler bytes not zero")
            ins.mem_refs.append(a)
            M(ins, "LDX", Ref(f"({hx(a)})", a, "mem"), hx(b))
        elif p >= 0xF8:
            M(ins, "SWI", str(p & 7))
        else:
            ins.invalid = True
    except EOFError:
        ins.invalid = True
    if ins.invalid:
        rd.pc = addr + 1
        b = mem(addr)
        ins.mnem, ins.ops = "DB", [f"0x{b or 0:02X}"]
        ins.target = None
        ins.is_call = ins.is_jump = ins.conditional = ins.ends_block = False
        ins.mem_refs = []
        ins.illegal = None
    ins.size = rd.pc - addr
    ins.raw = bytes(mem(a) or 0 for a in range(addr, rd.pc))
    return ins


def decode_bytes(b, addr=0):
    """Decode the first instruction of a byte string placed at addr."""
    return decode(lambda a: b[a - addr] if 0 <= a - addr < len(b) else None, addr)
