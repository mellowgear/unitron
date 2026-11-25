"""asm900 operand syntax -> operand objects for the encoder.

Spellings (TR, datasheet): registers `A WA XWA RA3 QW XIX' ...`; immediates are plain expressions;
memory `(XHL)`, `(XHL+4)`, `(XHL-4)`, `(XHL+DE)`, `(XHL+A)`, `(-XHL)`, `(XHL+)`, `(-XSP:4)`,
`(XHL+:1)`, `(XDE-)` (block ops), `(0x6F82)`; JP/CALL/LDA take the address bare (`jp XHL`,
`jp C,XIX+2`, `lda XHL,XHL+DE`).
"""
from ..asm import expr as X
from .regs import REG, SHORT, CR_CODE, CC_CODE


class OperandError(Exception):
    pass


class Reg:
    __slots__ = ("name", "size", "code", "short")
    kind = "reg"

    def __init__(self, name):
        self.name = name
        self.size, self.code = REG[name]
        self.short = SHORT[self.size].get(name)

    def __repr__(self):
        return self.name


class Special:
    """F, F', SR."""
    __slots__ = ("name",)
    kind = "special"

    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return self.name


class Ctl:
    __slots__ = ("name", "code", "size")
    kind = "cr"

    def __init__(self, name):
        self.name = name
        self.code, self.size = CR_CODE[name]


class Cond:
    __slots__ = ("code", "name")
    kind = "cc"

    def __init__(self, name):
        self.name = name
        self.code = CC_CODE[name]


class Imm:
    __slots__ = ("expr",)
    kind = "imm"

    def __init__(self, e):
        self.expr = e


class Mem:
    """mode: ind (r), disp (r+d), idx (r+r8/r16), pre (-r), post (r+), dec (r-), abs (n)."""
    __slots__ = ("mode", "reg", "disp", "idx", "step", "addr", "bare")
    kind = "mem"

    def __init__(self, mode, reg=None, disp=None, idx=None, step=None, addr=None, bare=False):
        self.mode, self.reg, self.disp, self.idx, self.step, self.addr, self.bare = \
            mode, reg, disp, idx, step, addr, bare


SPECIAL = {"F", "F'", "SR"}


def reg_of(text):
    u = text.upper()
    return Reg(u) if u in REG else None


def _mem_body(body, bare):
    b = body.strip()
    if not b:
        raise OperandError("empty memory operand")
    if b.startswith("-"):
        rest = b[1:].strip()
        name, _, step = rest.partition(":")
        r = reg_of(name.strip())
        if r is not None:
            return Mem("pre", r, step=_step(step), bare=bare)
    # leading register?
    i = 0
    while i < len(b) and (b[i].isalnum() or b[i] in "_'"):
        i += 1
    head, tail = b[:i], b[i:].strip()
    r = reg_of(head) if head else None
    if r is None:
        return Mem("abs", addr=X.parse(b), bare=bare)
    if not tail:
        return Mem("ind", r, bare=bare)
    if tail == "-":
        return Mem("dec", r, bare=bare)
    if tail.startswith("+") and (tail[1:].strip() == "" or tail[1:].strip().startswith(":")):
        return Mem("post", r, step=_step(tail[1:].strip()[1:]) if ":" in tail else None, bare=bare)
    if tail[0] in "+-":
        x = reg_of(tail[1:].strip())
        if x is not None:
            if tail[0] != "+" or x.size == 4:
                raise OperandError(f"bad index register in {body!r}")
            return Mem("idx", r, idx=x, bare=bare)
        return Mem("disp", r, disp=X.parse(tail), bare=bare)
    raise OperandError(f"bad memory operand {body!r}")


def _step(s):
    s = s.strip()
    if not s:
        return None
    if s not in ("1", "2", "4"):
        raise OperandError(f"step must be 1, 2 or 4, not {s!r}")
    return int(s)


def parse(text, bare_addr=False, cond=False):
    """One operand. bare_addr: the operand is a JP/CALL/LDA address (no parentheses);
    cond: a condition code is expected here."""
    t = text.strip()
    if not t:
        raise OperandError("missing operand")
    u = t.upper()
    if cond:
        if u in CC_CODE:
            return Cond(u)
        raise OperandError(f"{t!r} is not a condition")
    if u in REG and not bare_addr:
        return Reg(u)
    if u in SPECIAL:
        return Special(u)
    if u in CR_CODE:
        return Ctl(u)
    if bare_addr:
        if t.startswith("(") and t.endswith(")") and _balanced(t[1:-1]):
            raise OperandError(f"write {t[1:-1].strip()} without parentheses (asm900 JP/CALL/LDA syntax)")
        return _mem_body(t, True)
    if t.startswith("(") and t.endswith(")") and _balanced(t[1:-1]):
        return _mem_body(t[1:-1], False)
    return Imm(X.parse(t))


def _balanced(s):
    d = 0
    for c in s:
        d += c == "("
        d -= c == ")"
        if d < 0:
            return False
    return d == 0


def split(text):
    """Split an operand field at top-level commas (quotes and parentheses respected)."""
    out, cur, depth, q, esc = [], [], 0, None, False
    for i, c in enumerate(text):
        if q:
            cur.append(c)
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == q:
                q = None
            continue
        if c in "\"'":
            prev = text[i - 1] if i else ""
            if not (c == "'" and (prev.isalnum() or prev in "_.")):
                q = c
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
            continue
        cur.append(c)
    last = "".join(cur).strip()
    if last or out:
        out.append(last)
    return out
