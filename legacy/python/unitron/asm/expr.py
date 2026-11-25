"""asm900 expressions: numbers, symbols, `$`, char constants and the operators of TR p.12-16.

Precedence (high to low): unary - ~, sizeof() startof(); * / %; + -; >> <<; &; ^; |; and the
width operator `expr:n` (asm900 grammar, not in TR): the value masked to n bits, encoded at least
n bits wide. A parsed expression is an int (constant-folded) or a tuple tree.
"""
import re


class ExprError(Exception):
    pass


_TOKEN = re.compile(r"""
    \s*(?:
      (?P<num>0[xX][0-9A-Fa-f]+|0[bByY][01]+|[0-9][0-9A-Za-z_]*)
    | (?P<chr>'(?:[^'\\]|\\.)*')
    | (?P<id>[A-Za-z_.][A-Za-z0-9_.']*)
    | (?P<op><<|>>|[-+*/%&|^~()$:])
    )""", re.X)

ESCAPES = {"0": 0, "b": 8, "n": 10, "t": 9, '"': 0x22, "?": 0x3F, "a": 7, "f": 12,
           "r": 13, "v": 11, "'": 0x27, "\\": 0x5C}


def unescape(body):
    """Bytes of a string or char constant body with TR p.10 escape sequences."""
    out = bytearray()
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            o = ord(c)
            if o > 0xFF:
                raise ExprError(f"character {c!r} is not a byte")
            out.append(o)
            i += 1
            continue
        i += 1
        if i >= len(body):
            raise ExprError("string ends in a backslash")
        c = body[i]
        if c == "x":
            m = re.match(r"[0-9A-Fa-f]{1,2}", body[i + 1:])
            if not m:
                raise ExprError("\\x needs hex digits")
            out.append(int(m.group(0), 16))
            i += 1 + len(m.group(0))
        elif c in "01234567":
            m = re.match(r"[0-7]{1,3}", body[i:])
            v = int(m.group(0), 8)
            if v > 0xFF:
                raise ExprError(f"octal escape \\{m.group(0)} above 377")
            out.append(v)
            i += len(m.group(0))
        elif c in ESCAPES:
            out.append(ESCAPES[c])
            i += 1
        else:
            raise ExprError(f"unknown escape \\{c}")
    return bytes(out)


# asm900's -H option: Intel-style suffixed numbers (1234H, 101B, 17O/17Q, 99D) as well.
INTEL_SUFFIX = False


def parse_number(t):
    """0x hex, 0y binary, leading 0 octal, else decimal (asm900; see docs/OPEN.md on `0b`)."""
    tl = t.lower()
    try:
        if tl.startswith("0x"):
            return int(tl[2:], 16)
        if tl.startswith("0y"):
            return int(tl[2:], 2)
        if INTEL_SUFFIX and len(tl) > 1 and tl[-1] in "hboqd":
            return int(tl[:-1], {"h": 16, "b": 2, "o": 8, "q": 8, "d": 10}[tl[-1]])
        if len(tl) > 1 and tl.startswith("0"):
            return int(tl[1:], 8)
        return int(tl, 10)
    except ValueError:
        pass
    if tl.startswith("0b"):
        raise ExprError(f"illegal numeric constant {t!r}: asm900 writes binary as 0y (0b is rejected, E202)")
    if tl[-1:] in "hboqd" and not INTEL_SUFFIX:
        raise ExprError(f"illegal numeric constant {t!r}: suffixed numbers need -H (E202)")
    raise ExprError(f"illegal numeric constant {t!r} (E202)")


def char_value(body):
    b = unescape(body)
    if not b:
        raise ExprError("empty character constant")
    v = 0
    for x in b[-4:]:
        v = (v << 8) | x
    return v


_BIN = {"*": 2, "/": 2, "%": 2, "+": 3, "-": 3, ">>": 4, "<<": 4, "&": 5, "^": 6, "|": 7}
FUNCS = ("sizeof", "startof")


def _wrap(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def _apply(op, a, b):
    if op == "+":
        return _wrap(a + b)
    if op == "-":
        return _wrap(a - b)
    if op == "*":
        return _wrap(a * b)
    if op in ("/", "%"):
        if b == 0:
            raise ExprError("division by zero")
        q = abs(a) // abs(b)
        q = q if (a >= 0) == (b >= 0) else -q
        return _wrap(q) if op == "/" else _wrap(a - q * b)
    if op in (">>", "<<"):
        if b < 0:
            raise ExprError("negative shift count")
        if op == "<<":
            return _wrap(a << b) if b < 64 else 0
        return _wrap((a & 0xFFFFFFFF) >> b) if a >= 0 else _wrap(a >> b)
    if op == "&":
        return _wrap(a & b)
    if op == "^":
        return _wrap(a ^ b)
    if op == "|":
        return _wrap(a | b)
    raise ExprError(op)


class _P:
    def __init__(self, text):
        self.text = text
        self.toks = []
        pos = 0
        n = len(text)
        while pos < n:
            if text[pos].isspace():
                pos += 1
                continue
            m = _TOKEN.match(text, pos)
            if not m or m.end() == pos:
                raise ExprError(f"unexpected {text[pos:]!r}")
            pos = m.end()
            k = m.lastgroup
            self.toks.append((k, m.group(k)))
        self.i = 0

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def take(self):
        t = self.peek()
        self.i += 1
        return t

    def expr(self, level=8):
        if level == 8:
            a = self.expr(7)
            while self.peek() == ("op", ":"):
                self.take()
                b = self.expr(7)
                if not isinstance(b, int) or not 1 <= b <= 32:
                    raise ExprError("the width after ':' must be a constant 1..32")
                a = (":", a, b)
            return a
        if level == 1:
            return self.unary()
        a = self.expr(level - 1)
        while True:
            k, v = self.peek()
            if k == "op" and _BIN.get(v) == level:
                self.take()
                b = self.expr(level - 1)
                a = _apply(v, a, b) if isinstance(a, int) and isinstance(b, int) else (v, a, b)
            else:
                return a

    def unary(self):
        k, v = self.take()
        if k == "op" and v in ("-", "~", "+"):
            a = self.unary()
            if v == "+":
                return a
            if isinstance(a, int):
                return _wrap(-a) if v == "-" else _wrap(~a)
            return ("neg" if v == "-" else "not", a)
        if k == "op" and v == "(":
            a = self.expr()
            if self.take() != ("op", ")"):
                raise ExprError("unbalanced parentheses")
            return a
        if k == "op" and v == "$":
            return ("$",)
        if k == "num":
            return _wrap(parse_number(v))
        if k == "chr":
            return _wrap(char_value(v[1:-1]))
        if k == "id":
            if v.lower() in FUNCS and self.peek() == ("op", "("):
                self.take()
                kk, name = self.take()
                if kk != "id" or self.take() != ("op", ")"):
                    raise ExprError(f"{v}() takes a section name")
                return (v.lower(), name)
            return ("sym", v)
        if k is None:
            raise ExprError("expression expected")
        raise ExprError(f"unexpected {v!r}")


def parse(text):
    """Parse an expression: an int if constant, else a tree."""
    p = _P(text)
    if not p.toks:
        raise ExprError("expression expected")
    e = p.expr()
    if p.i != len(p.toks):
        raise ExprError(f"unexpected {p.toks[p.i][1]!r}")
    return e


def symbols_in(e, out=None):
    out = set() if out is None else out
    if isinstance(e, tuple):
        if e[0] == "sym":
            out.add(e[1])
        else:
            for x in e[1:]:
                if isinstance(x, tuple):
                    symbols_in(x, out)
    return out


class Unknown(Exception):
    """A symbol is not defined (yet)."""
    def __init__(self, name):
        super().__init__(name)
        self.name = name


def evaluate(e, lookup, here, func=None):
    """Value of e -> (value, width bits). lookup(name) -> (value, width): width is the displacement
    width of a relocatable/extern symbol (8/16/24) or 0; `:n` sets n."""
    if isinstance(e, int):
        return e, 0
    k = e[0]
    if k == "sym":
        return lookup(e[1])
    if k == "$":
        return here()
    if k in FUNCS:
        return func(k, e[1])
    if k == "neg":
        v, w = evaluate(e[1], lookup, here, func)
        return _wrap(-v), w
    if k == "not":
        v, w = evaluate(e[1], lookup, here, func)
        return _wrap(~v), w
    a, wa = evaluate(e[1], lookup, here, func)
    if k == ":":
        n = e[2]
        return a & ((1 << n) - 1), n
    b, wb = evaluate(e[2], lookup, here, func)
    return _apply(k, a, b), max(wa, wb)
