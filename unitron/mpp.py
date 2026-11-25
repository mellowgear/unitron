"""Macro preprocessor: the documented subset of Toshiba's (TR part 4, ch. 12-17).

Preprocess functions (TR ch. 14): #include, #define (object and function-like, # and ##), #undef,
#if/#ifdef/#ifndef/#elif/#else/#endif, #error, #line. Macro functions (TR ch. 15): ?macro with
parameters and `labels`, ?endm; calls are `name args` in the mnemonic position. Generated labels
are the label name plus a 3-digit decimal count per name (lab1000, lab1001, ... TR p.65-66).
Comments: `;` is kept, `//` and `/* */` are dropped (TR p.60). Other ?functions are rejected.
"""
import os
import re
from pathlib import Path

from .asm.assembler import Where

_ID = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class MppError(Exception):
    def __init__(self, msg, where=None):
        super().__init__(msg)
        self.where = where


def _strip_comments(text, state):
    """Drop // and /* */ comments; keep ; comments. state['block'] carries /* across lines."""
    out = []
    i, n = 0, len(text)
    q = None
    while i < n:
        c = text[i]
        if state["block"]:
            j = text.find("*/", i)
            if j < 0:
                return "".join(out)
            state["block"] = False
            i = j + 2
            continue
        if q:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == q:
                q = None
            i += 1
            continue
        if c == ";":
            out.append(text[i:])
            break
        if c == '"' or (c == "'" and not (i and (text[i - 1].isalnum() or text[i - 1] in "_."))):
            q = c
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            break
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            state["block"] = True
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


class _Def:
    __slots__ = ("params", "body")

    def __init__(self, params, body):
        self.params, self.body = params, body


class Preprocessor:
    def __init__(self, include_dirs=(), defines=None):
        self.include_dirs = [Path(d) for d in include_dirs]
        th = os.environ.get("THOME900")
        if th:
            self.include_dirs.append(Path(th) / "include")
        self.defs = {"__900__": _Def(None, "1"), "__TOSHIBA__": _Def(None, "1"),
                     "__TRUE__": _Def(None, "1"), "__FALSE__": _Def(None, "0")}
        for k, v in (defines or {}).items():
            self.defs[k] = _Def(None, str(v))
        self.macros = {}
        self.counters = {}

    # ------------------------------------------------------------ files
    def _find(self, name, cur, angle):
        p = Path(name)
        if p.is_absolute():
            return p if p.is_file() else None
        dirs = ([] if angle else [Path(cur).parent]) + self.include_dirs
        for d in dirs:
            if (d / p).is_file():
                return d / p
        return None

    def run(self, path):
        lines = list(self._file(Path(path), (), 0))
        return self._macros(lines)

    def _file(self, path, chain, depth):
        if depth > 255:
            raise MppError("#include nested deeper than 255 levels", chain[-1] if chain else None)
        try:
            text = path.read_text(encoding="utf-8", errors="surrogateescape")
        except OSError:
            raise MppError(f"can't open {str(path)!r}", chain[-1] if chain else None) from None
        raw = text.split("\n")
        if raw and raw[-1] == "":
            raw.pop()
        state = {"block": False}
        cond = []               # stack of [active, taken, parent_active]
        i = 0
        while i < len(raw):
            ln = i + 1
            line = raw[i].rstrip("\r")
            while line.endswith("\\") and i + 1 < len(raw):
                i += 1
                line = line[:-1] + raw[i].rstrip("\r")
            i += 1
            where = Where(str(path), ln, chain)
            line = _strip_comments(line, state)
            s = line.strip()
            active = all(c[0] for c in cond)
            if s.startswith("#"):
                d = s[1:].strip()
                m = re.match(r"([A-Za-z]+)\s*(.*)$", d)
                if not m:
                    if active and d:
                        raise MppError(f"bad directive #{d}", where)
                    continue
                kw, rest = m.group(1).lower(), m.group(2)
                if kw in ("if", "ifdef", "ifndef"):
                    if not active:
                        cond.append([False, True, False])
                        continue
                    if kw == "if":
                        v = self._cond(rest, where)
                    else:
                        v = (rest.split()[0] in self.defs) if rest.split() else False
                        v = v if kw == "ifdef" else not v
                    cond.append([v, v, True])
                    if len(cond) > 63:
                        raise MppError("#if nesting deeper than 63", where)
                    continue
                if kw in ("elif", "else", "endif"):
                    if not cond:
                        raise MppError(f"#{kw} without #if", where)
                    top = cond[-1]
                    if kw == "endif":
                        cond.pop()
                    elif kw == "else":
                        top[0] = top[2] and not top[1]
                        top[1] = True
                    else:
                        v = top[2] and not top[1] and self._cond(rest, where)
                        top[0] = v
                        top[1] = top[1] or v
                    continue
                if not active:
                    continue
                if kw == "include":
                    mm = re.match(r'"([^"]+)"|<([^>]+)>', rest.strip())
                    if not mm:
                        raise MppError('#include needs "file" or <file>', where)
                    name = mm.group(1) or mm.group(2)
                    f = self._find(name, path, mm.group(2) is not None)
                    if f is None:
                        raise MppError(f"can't find include file {name!r}", where)
                    yield from self._file(f, chain + (where,), depth + 1)
                elif kw == "define":
                    self._define(rest, where)
                elif kw == "undef":
                    self.defs.pop(rest.strip(), None)
                elif kw == "error":
                    raise MppError(f"#error {rest}", where)
                elif kw == "line":
                    pass
                else:
                    raise MppError(f"unknown directive #{kw}", where)
                continue
            if not active:
                continue
            yield self._subst(line, where), where
        if cond:
            raise MppError("#if without #endif", Where(str(path), len(raw), chain))

    def _define(self, rest, where):
        m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)(\(([^)]*)\))?\s*(.*)$", rest)
        if not m:
            raise MppError("bad #define", where)
        name = m.group(1)
        params = None
        if m.group(2) is not None:
            params = [p.strip() for p in m.group(3).split(",")] if m.group(3).strip() else []
            if len(params) > 127:
                raise MppError("more than 127 #define parameters", where)
        self.defs[name] = _Def(params, m.group(4).strip())

    def _cond(self, text, where):
        t = re.sub(r"\bdefined\s*\(\s*([A-Za-z_]\w*)\s*\)|\bdefined\s+([A-Za-z_]\w*)",
                   lambda m: "1" if (m.group(1) or m.group(2)) in self.defs else "0", text)
        t = self._subst(t, where)
        t = re.sub(r"\b[A-Za-z_]\w*\b", "0", t)       # undefined identifiers are 0
        return _cexpr(t, where) != 0

    # ------------------------------------------------------------ #define substitution
    def _subst(self, line, where, depth=0, active=frozenset()):
        if not self.defs or depth > 64:
            return line
        out = []
        i, n = 0, len(line)
        q = None
        changed = False
        while i < n:
            c = line[i]
            if q:
                out.append(c)
                if c == "\\" and i + 1 < n:
                    out.append(line[i + 1])
                    i += 2
                    continue
                if c == q:
                    q = None
                i += 1
                continue
            if c == ";":
                out.append(line[i:])
                break
            if c == '"' or (c == "'" and not (i and (line[i - 1].isalnum() or line[i - 1] in "_."))):
                q = c
                out.append(c)
                i += 1
                continue
            if c.isalpha() or c == "_":
                m = _ID.match(line, i)
                name = m.group(0)
                j = m.end()
                prev = line[i - 1] if i else ""
                d = self.defs.get(name)
                if d is None or name in active or prev.isdigit():
                    out.append(name)
                    i = j
                    continue
                if d.params is None:
                    out.append(self._subst(d.body, where, depth + 1, active | {name}))
                    i = j
                    changed = True
                    continue
                k = j
                while k < n and line[k] in " \t":
                    k += 1
                if k >= n or line[k] != "(":
                    out.append(name)
                    i = j
                    continue
                args, k = _call_args(line, k, where)
                if len(args) != len(d.params) and not (len(d.params) == 0 and args == [""]):
                    raise MppError(f"{name} takes {len(d.params)} arguments", where)
                body = _expand_fn(d, [self._subst(a, where, depth + 1, active) for a in args], args)
                out.append(self._subst(body, where, depth + 1, active | {name}))
                i = k
                changed = True
                continue
            out.append(c)
            i += 1
        return "".join(out) if changed or out else line

    # ------------------------------------------------------------ ?macro
    def _macros(self, lines):
        out = []
        i = 0
        while i < len(lines):
            text, where = lines[i]
            s = text.strip()
            if s.lower().startswith("?macro"):
                i = self._def_macro(lines, i)
                continue
            if s.startswith("?"):
                word = re.match(r"\?(\w*)", s).group(1).lower()
                if word == "endm":
                    raise MppError("?endm without ?macro", where)
                if word in ("if", "elseif", "else", "endif", "repeat", "endr", "while", "endw",
                            "variable", "eval", "exit", "assign", "chop", "eqs", "len", "substr",
                            "trigger", "restrict_macro", "endres", "ges", "gts", "les", "lts", "nes"):
                    raise MppError(f"?{word} is not supported (only ?macro/?endm)", where)
            out.extend(self._expand_line(text, where, 0))
            i += 1
        return out

    def _def_macro(self, lines, i):
        text, where = lines[i]
        m = re.match(r"\s*\?macro\s+([A-Za-z_]\w*)\s*(.*)$", text, re.I)
        if not m:
            raise MppError("bad ?macro line", where)
        name = m.group(1)
        params = [p.strip() for p in m.group(2).split(";")[0].split(",") if p.strip()]
        if name in self.macros:
            raise MppError(f"macro {name} defined twice", where)
        if len(params) > 127:
            raise MppError("more than 127 macro parameters", where)
        labels = []
        body = []
        j = i + 1
        if j < len(lines):
            lm = re.match(r"\s*labels\s+(.*)$", lines[j][0], re.I)
            if lm:
                labels = [x.strip() for x in lm.group(1).split(";")[0].split(",") if x.strip()]
                if len(labels) > 64:
                    raise MppError("more than 64 macro labels", lines[j][1])
                j += 1
        while j < len(lines):
            t = lines[j][0]
            if re.match(r"\s*\?endm\b", t, re.I):
                self.macros[name] = (params, labels, body, where)
                return j + 1
            if re.match(r"\s*\?macro\b", t, re.I):
                raise MppError("nested ?macro definition", lines[j][1])
            body.append(t)
            j += 1
        raise MppError(f"?macro {name} has no ?endm", where)

    def _expand_line(self, text, where, depth):
        if not self.macros:
            return [(text, where)]
        code = text.split(";")[0] if ";" in text else text
        m = re.match(r"^(\s*)(?:([A-Za-z_.][\w.]*):)?(\s*)([A-Za-z_]\w*)\b(.*)$", code)
        if not m or m.group(4) not in self.macros:
            return [(text, where)]
        if m.group(1) == "" and m.group(2) is None:
            # column 0 without a colon is a label position (TR p.10), not a call
            return [(text, where)]
        if depth > 32:
            raise MppError("macro nesting deeper than 32", where)
        name = m.group(4)
        params, labels, body, _ = self.macros[name]
        from .cpu.operands import split
        args = split(m.group(5)) if m.group(5).strip() else []
        if len(args) > len(params):
            raise MppError(f"{name} takes {len(params)} arguments, got {len(args)}", where)
        args += [""] * (len(params) - len(args))
        gen = {}
        for lab in labels:
            k = self.counters.get(lab, 0)
            if k > 999:
                raise MppError(f"more than 1000 expansions of macro label {lab}", where)
            self.counters[lab] = k + 1
            gen[lab] = f"{lab}{k:03d}"
        sub = dict(zip(params, args))
        sub.update(gen)
        out = []
        if m.group(2):
            out.append((f"{m.group(2)}:", where))
        for b in body:
            t = re.sub(r"\?([A-Za-z_]\w*)", lambda mm: sub.get(mm.group(1), mm.group(0)), b)
            out.extend(self._expand_line(t, where, depth + 1))
        return out


def _call_args(line, k, where):
    """Arguments of a function-like macro call starting at '(' -> (list, index after ')')."""
    depth = 0
    args, cur = [], []
    q = None
    for j in range(k, len(line)):
        c = line[j]
        if q:
            cur.append(c)
            if c == q:
                q = None
            continue
        if c in "\"'":
            q = c
            cur.append(c)
            continue
        if c == "(":
            depth += 1
            if depth == 1:
                continue
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append("".join(cur).strip())
                return args, j + 1
        elif c == "," and depth == 1:
            args.append("".join(cur).strip())
            cur = []
            continue
        cur.append(c)
    raise MppError("unterminated macro call", where)


def _expand_fn(d, args, raw_args):
    body = d.body
    idx = {p: i for i, p in enumerate(d.params)}
    body = re.sub(r"#\s*([A-Za-z_]\w*)", lambda m: '"' + raw_args[idx[m.group(1)]] + '"'
                  if m.group(1) in idx else m.group(0), body)
    body = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: args[idx[m.group(1)]] if m.group(1) in idx
                  else m.group(0), body)
    return re.sub(r"\s*##\s*", "", body)


def _cexpr(t, where):
    """Evaluate a #if expression (C operators, TR p.60 precedence)."""
    toks = re.findall(r"0[xX][0-9A-Fa-f]+|0[bByY][01]+|\d+|&&|\|\||==|!=|>=|<=|<<|>>|[-+*/%&|^~!()<>]", t)
    if re.sub(r"0[xX][0-9A-Fa-f]+|0[bByY][01]+|\d+|&&|\|\||==|!=|>=|<=|<<|>>|[-+*/%&|^~!()<>]|\s", "", t):
        raise MppError(f"bad #if expression {t!r}", where)
    from .asm.expr import parse_number
    pos = [0]
    prec = [("||",), ("&&",), ("^",), ("|",), ("&",), ("==", "!="), (">", "<", ">=", "<="),
            (">>", "<<"), ("+", "-"), ("*", "/", "%")]

    def peek():
        return toks[pos[0]] if pos[0] < len(toks) else None

    def unary():
        tk = peek()
        pos[0] += 1
        if tk in ("-", "+", "~", "!"):
            v = unary()
            return {"-": -v, "+": v, "~": ~v, "!": int(not v)}[tk]
        if tk == "(":
            v = level(0)
            if peek() != ")":
                raise MppError("unbalanced parentheses in #if", where)
            pos[0] += 1
            return v
        if tk is None:
            raise MppError("#if expression ends early", where)
        return parse_number(tk)

    def level(i):
        if i == len(prec):
            return unary()
        v = level(i + 1)
        while peek() in prec[i]:
            op = toks[pos[0]]
            pos[0] += 1
            w = level(i + 1)
            v = {"||": lambda: int(bool(v or w)), "&&": lambda: int(bool(v and w)),
                 "^": lambda: v ^ w, "|": lambda: v | w, "&": lambda: v & w,
                 "==": lambda: int(v == w), "!=": lambda: int(v != w), ">": lambda: int(v > w),
                 "<": lambda: int(v < w), ">=": lambda: int(v >= w), "<=": lambda: int(v <= w),
                 ">>": lambda: v >> w, "<<": lambda: v << w, "+": lambda: v + w,
                 "-": lambda: v - w, "*": lambda: v * w, "/": lambda: int(v / w) if w else 0,
                 "%": lambda: v % w if w else 0}[op]()
        return v

    v = level(0)
    if pos[0] != len(toks):
        raise MppError(f"bad #if expression {t!r}", where)
    return v


def preprocess(path, include_dirs=(), defines=None):
    """Run the preprocessor on a file -> list of (text, Where)."""
    return Preprocessor(include_dirs, defines).run(path)
