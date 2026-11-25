"""asm900 assembler and linker in one step: modules -> sections -> a memory image.

Language: Toshiba TLCS-900 Assembler Reference (tde122-01_96, "TR") chapters 3-5; see
docs/language.md. Each source file is one module; relocatable sections are placed after all
modules are sized (our placement, docs/language.md "Linking"), absolute sections stay where `abs=`
puts them. Errors carry Toshiba's numbers where one fits (TR p.96-99).
"""
import os
import re
from pathlib import Path

from . import expr as X
from .line import parse_insn, LineError
from ..cpu.encode import encode, EncodeError, MNEMONICS, split_mnemonic

DATA_W = {"DB": 1, "DW": 2, "DP": 3, "DD": 4, "DL": 4}
FILL_W = {"DFB": 1, "DFW": 2, "DFP": 3, "DFD": 4, "DFL": 4}
SPACE_W = {"DSB": 1, "DSW": 2, "DSP": 3, "DSD": 4, "DSL": 4}
NAMING = {"EQU", "SECTION"} | set(DATA_W) | set(FILL_W) | set(SPACE_W)
DIRECTIVES = NAMING | {"MODULE", "END", "ORG", "ALIGN", "PUBLIC", "EXTERN", "FDATAS", "FDATAD",
                       "FDATAX", "FILE", "LINE", "SYMBOL_A", "SYMBOL_B", "SYMBOL_N", "SYMBOL_T"}
UNSUPPORTED = {"FDATAS", "FDATAD", "FDATAX", "FILE", "LINE", "SYMBOL_A", "SYMBOL_B", "SYMBOL_N",
               "SYMBOL_T"}
LISTING = {"LIST", "NOLIST", "TITLE", "EJECT", "SAVE", "RESTORE", "LI", "NOLI", "TT", "EJ", "SA", "RS"}
ATTR_W = {"SMALL": 8, "MEDIUM": 16, "LARGE": 24}          # address bits
ATTR_NAME = {8: "small", 16: "medium", 24: "large"}
ATTR_LIMIT = {8: 0x100, 16: 0x10000, 24: 0x1000000}
_NUMTOK = re.compile(r"^(?:0[xX][0-9A-Fa-f]+|[1-9][0-9]*|0)$")
_IDENT = re.compile(r"^[A-Za-z_.][A-Za-z0-9_.]*$")


class AsmError(Exception):
    pass


class Where:
    __slots__ = ("file", "line", "chain")

    def __init__(self, file, line, chain=()):
        self.file, self.line, self.chain = file, line, chain

    def __str__(self):
        return f"{self.file}:{self.line}"


class Diags:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, where, code, msg):
        self.errors.append(_fmt(where, "error", code, msg))

    def warn(self, where, code, msg):
        self.warnings.append(_fmt(where, "warning", code, msg))


def _fmt(where, kind, code, msg):
    head = f"{where}: " if where else ""
    num = f" E{code}" if code and kind == "error" else f" W{code}" if code else ""
    s = f"{head}{kind}{num}: {msg}"
    if where is not None and getattr(where, "chain", None):
        s += "".join(f"\n    included from {c}" for c in where.chain)
    return s


def split_comment(line):
    """Code part of a line: up to a `;` outside "strings" and 'char constants'."""
    if ";" not in line:
        return line
    q = None
    esc = False
    for i, c in enumerate(line):
        if q:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == q:
                q = None
            continue
        if c == ";":
            return line[:i]
        if c == '"' or (c == "'" and not (i and (line[i - 1].isalnum() or line[i - 1] in "_."))):
            q = c
    return line


def _reserved(word):
    u = word.upper()
    return u in DIRECTIVES or split_mnemonic(u) is not None


from ..cpu.regs import REG as _REG, CC_CODE as _CC, CR_CODE as _CR  # noqa: E402
# every asm900 keyword is reserved in any case (keyword lookup runs before the symbol table)
RESERVED = (set(_REG) | set(_CC) | set(_CR) | DIRECTIVES | {
    "F", "F'", "SR", "CODE", "DATA", "ROMDATA", "SIZEOF", "STARTOF", "INCLUDE", "MAX", "MIN",
    "LIST", "LI", "TITLE", "TT", "SA", "RS", "EJ", "NOR", "SYS", "BANK", "LOC0", "COM0", "SMALL",
    "MEDIUM", "LARGE", "ABS"})


def is_reserved(name):
    u = name.upper()
    return u in RESERVED or split_mnemonic(u) is not None


def parse_statement(code):
    """-> (label, op, field). label may be None; op is the first word (or None)."""
    s = code.rstrip()
    if not s.strip():
        return None, None, ""
    label = None
    if not s[0].isspace():
        m = re.match(r"([^\s:]+)(:?)", s)
        tok, colon = m.group(1), m.group(2)
        rest = s[m.end():]
        if colon:
            label, s = tok, rest
        elif not (tok.startswith("$") or _reserved(tok)):
            label, s = tok, rest
    s = s.strip()
    if not s:
        return label, None, ""
    m = re.match(r"(\S+)\s*(.*)$", s)
    op, field = m.group(1), m.group(2)
    if label is None and op.endswith(":") and len(op) > 1:
        label, s = op[:-1], field
        if not s:
            return label, None, ""
        m = re.match(r"(\S+)\s*(.*)$", s)
        op, field = m.group(1), m.group(2)
    elif label is None and not _reserved(op) and not op.startswith("$"):
        m2 = re.match(r"(\S+)\s*(.*)$", field)
        if m2 and m2.group(1).upper() in NAMING:
            label, op, field = op, m2.group(1), m2.group(2)
    return label, op, field


class Item:
    """One statement that occupies (or moves) the location counter."""
    __slots__ = ("kind", "where", "a", "b", "c", "off", "size", "data")

    def __init__(self, kind, where, a=None, b=None, c=None):
        self.kind, self.where, self.a, self.b, self.c = kind, where, a, b, c
        self.off = 0
        self.size = 0
        self.data = None


class Section:
    def __init__(self, module, name, kind, attr, abs_addr, align, where):
        self.module, self.name, self.kind, self.attr = module, name, kind, attr
        self.abs = abs_addr
        self.align = align
        self.where = where
        self.items = []
        self.base = abs_addr if abs_addr is not None else 0
        self.size = 0

    @property
    def width(self):
        return 0 if self.abs is not None else ATTR_W[self.attr.upper()]


class Sym:
    __slots__ = ("kind", "sec", "item", "expr", "where", "width", "busy")

    def __init__(self, kind, where, sec=None, item=None, expr=None, width=0):
        self.kind, self.where, self.sec, self.item, self.expr, self.width = kind, where, sec, item, expr, width
        self.busy = False


class Module:
    def __init__(self, name, where):
        self.name = name
        self.where = where
        self.syms = {}
        self.sections = []
        self.secmap = {}
        self.public = {}
        self.ended = False
        self.maximum = False


class Assembler:
    def __init__(self, include_dirs=(), strict=False, mpp=True, defines=None):
        self.include_dirs = [Path(d) for d in include_dirs]
        th = os.environ.get("THOME900")
        if th:
            self.include_dirs.append(Path(th) / "include")
        self.strict = strict
        self.use_mpp = mpp
        self.defines = defines or {}
        self.d = Diags()
        self.modules = []
        self.final = False
        self.undefined = set()

    # ------------------------------------------------------------ reading
    def _lines(self, path):
        if self.use_mpp:
            from .. import mpp
            try:
                return mpp.preprocess(path, self.include_dirs, self.defines)
            except mpp.MppError as e:
                self.d.error(e.where, 0, str(e))
                return []
        return [(t, Where(str(path), n)) for n, t in enumerate(_read(path).splitlines(), 1)]

    def _find_include(self, name, cur):
        p = Path(name)
        if p.is_absolute():
            return p if p.is_file() else None
        for d in [Path(cur).parent] + self.include_dirs:
            if (d / p).is_file():
                return d / p
        return None

    def add_module(self, path):
        path = Path(path)
        lines = self._lines(path)
        mod = Module(path.stem, Where(str(path), 1))
        self.modules.append(mod)
        self._cur_sec = None
        self._depth = 0
        self._first = True
        self._run(mod, lines, path)
        if not mod.ended:
            self.d.warn(Where(str(path), len(lines)), 506, "no `end` directive")
        seen = {}
        for n, sym in mod.syms.items():
            if len(n) > 32:
                other = seen.setdefault(n[:32], n)
                if other != n:
                    self.d.warn(sym.where, 0, f"{n!r} and {other!r} agree in their first 32 characters "
                                "(asm900 V2.2d keeps only 32)")
        return mod

    def _run(self, mod, lines, path, chain=()):
        for text, where in lines:
            if "\x1a" in text:          # DOS end-of-file mark
                text = text.replace("\x1a", "")
            if mod.ended:
                if split_comment(text).strip():
                    self.d.warn(where, 507, "text after `end` is ignored")
                    return
                continue
            if chain:
                where = Where(where.file, where.line, chain)
            try:
                self._statement(mod, text, where, path)
            except (AsmError, X.ExprError, LineError) as e:
                self.d.error(where, getattr(e, "code", 200), str(e))

    # ------------------------------------------------------------ statements
    def _statement(self, mod, text, where, path):
        code = split_comment(text)
        if not code.strip():
            return
        label, op, field = parse_statement(code)
        if op is not None and op.startswith("$"):
            if label:
                raise _err(200, "a control instruction cannot have a label")
            self._control(mod, op[1:].upper(), field, where, path)
            return
        if self._first:
            self._first = False
            if not mod.maximum:
                self.d.error(where, 218, "`$maximum` must be the first statement (TR p.25)")
                mod.maximum = True
        u = op.upper() if op else None
        if u in ("EQU", "SECTION"):
            if not label:
                raise _err(200, f"`{op}` needs a name")
            if u == "EQU":
                self._define(mod, label, Sym("equ", where, expr=X.parse(field)))
            else:
                self._section(mod, label, field, where)
            return
        if label:
            if not _IDENT.match(label) or label[0].isdigit():
                raise _err(208, f"illegal label {label!r}")
            sec = self._need_section(where)
            it = Item("label", where)
            sec.items.append(it)
            self._define(mod, label, Sym("label", where, sec=sec, item=it))
        if u is None:
            return
        if u in DATA_W or u in FILL_W or u in SPACE_W:
            self._data(u, field, where)
        elif u == "MODULE":
            if mod.name != Path(where.file).stem and getattr(mod, "_named", False):
                self.d.warn(where, 509, "second `module` directive ignored")
            else:
                mod.name = field.strip()
                mod._named = True
        elif u == "END":
            mod.ended = True
        elif u == "ORG":
            self._need_section(where).items.append(Item("org", where, X.parse(field)))
        elif u == "ALIGN":
            e = X.parse(field)
            if not isinstance(e, int):
                e = self._abs_now(mod, e, where)
            sec = self._need_section(where)
            if e <= 0 or e & (e - 1):
                self.d.warn(where, 508, f"align {e} is not a power of 2")
            sec.items.append(Item("align", where, e))
            sec.align = max(sec.align, e)
        elif u == "PUBLIC":
            for n in _names(field):
                mod.public.setdefault(n, where)
        elif u == "EXTERN":
            parts = field.split(None, 1)
            w = 24
            if parts and parts[0].upper() in ATTR_W:
                w = ATTR_W[parts[0].upper()]
                field = parts[1] if len(parts) > 1 else ""
            for n in _names(field):
                if n in mod.syms and mod.syms[n].kind != "extern":
                    raise _err(203, f"multi-defined symbol {n!r} (defined at {mod.syms[n].where})")
                mod.syms[n] = Sym("extern", where, width=w)
        elif u in UNSUPPORTED:
            raise _err(200, f"`{op}` is not supported (floating point / debug information)")
        else:
            sm = split_mnemonic(u)
            if sm is None:
                raise _err(200, f"unknown mnemonic or directive {op!r}")
            sec = self._need_section(where)
            if sec.kind != "code":
                self.d.warn(where, 504, f"instruction in a {sec.kind} section")
            base, sfx, ops = parse_insn(op, field)
            sec.items.append(Item("ins", where, base, sfx, ops))

    def _control(self, mod, name, field, where, path):
        if name == "MAXIMUM":
            if not self._first:
                self.d.warn(where, 218, "`$maximum` should be the first statement")
            mod.maximum = True
            self._first = False
            return
        if name == "MINIMUM":
            raise _err(218, "$minimum is not supported (the 900/H has only maximum mode)")
        if name in ("SYSTEM", "NORMAL"):
            self.d.warn(where, 0, f"${name.lower()} only records a mode in asm900; ignored")
            return
        if name in LISTING:
            return
        if name == "INCLUDE":
            m = re.match(r'^\s*"([^"]+)"\s*$', field) or re.match(r"^\s*<([^>]+)>\s*$", field)
            if not m:
                raise _err(218, '$include needs "file"')
            chain = (where.chain or ()) + (where,)
            if len(chain) > 8 and self.strict:
                raise _err(234, "include nesting deeper than 8")
            if len(chain) > 64:
                raise _err(234, "include nesting too deep (recursive include?)")
            f = self._find_include(m.group(1), where.file)
            if f is None:
                raise _err(20, f"can't open {m.group(1)!r}")
            lines = [(t, Where(str(f), n)) for n, t in enumerate(_read(f).splitlines(), 1)]
            self._run(mod, lines, f, chain)
            return
        raise _err(218, f"unknown control instruction ${name.lower()}")

    def _need_section(self, where):
        if self._cur_sec is None:
            raise _err(215, "no section definition before this statement")
        return self._cur_sec

    def _define(self, mod, name, sym):
        if is_reserved(name):
            raise _err(208, f"{name!r} is a reserved word")
        old = mod.syms.get(name)
        if old is not None:
            raise _err(203, f"multi-defined symbol {name!r} (first defined at {old.where})")
        mod.syms[name] = sym

    def _abs_now(self, mod, e, where):
        try:
            v, w = X.evaluate(e, lambda n: self._lookup(mod, n, strict=True), lambda: (0, 0))
        except X.Unknown as u:
            raise _err(223, f"{u.name!r} must be defined before this point") from None
        if w:
            raise _err(222, "absolute expression expected")
        return v

    def _section(self, mod, name, field, where):
        toks = [t for t in re.split(r"\s+", field.strip()) if t]
        if not toks or toks[0].upper() not in ("CODE", "DATA", "ROMDATA"):
            raise _err(212, "section type must be code, data or romdata")
        kind = toks[0].lower()
        attr, abs_addr, align = None, None, 1
        rest = " ".join(toks[1:])
        for part in re.findall(r"(abs\s*=\s*[^\s]+|align\s*=\s*[^\s]+|\w+)", rest, re.I):
            p = part.lower().replace(" ", "")
            if p.startswith("abs="):
                abs_addr = self._abs_now(mod, X.parse(part.split("=", 1)[1]), where)
            elif p.startswith("align="):
                a = part.split("=", 1)[1].split(",")[0]
                align = self._abs_now(mod, X.parse(a), where) if a else 1
            elif p.upper() in ATTR_W:
                attr = p.lower()
            else:
                raise _err(216, f"bad section attribute {part!r}")
        if attr == "small" and kind != "data":
            raise _err(216, "only data sections can be small (TR p.19)")
        if attr and abs_addr is not None:
            raise _err(216, "a section is either relocatable (small/medium/large) or abs=")
        if abs_addr is None and attr is None:
            attr = "large"
        old = mod.secmap.get(name)
        if old is not None:
            if old.kind != kind or (abs_addr is not None and old.abs != abs_addr) or \
                    (attr and old.attr != attr):
                self.d.warn(where, 517, f"section {name} already has a different attribute; continuing it")
            self._cur_sec = old
            return
        sec = Section(mod, name, kind, attr, abs_addr, align, where)
        mod.sections.append(sec)
        mod.secmap[name] = sec
        self._cur_sec = sec

    def _data(self, u, field, where):
        sec = self._need_section(where)
        if u in DATA_W:
            w = DATA_W[u]
            vals = _data_values(field, w)
            if not vals:
                raise _err(231, f"{u.lower()} needs values")
            it = Item("data", where, w, vals)
            if all(isinstance(v, (int, bytes)) for v in vals):
                it.data = _pack(w, vals, where, self.d)
                it.kind = "bin"
            sec.items.append(it)
        elif u in FILL_W:
            w = FILL_W[u]
            parts = _split_args(field)
            if len(parts) != 2:
                raise _err(231, f"{u.lower()} takes count,value")
            n = X.parse(parts[0])
            v = _data_values(parts[1], w)
            if len(v) != 1:
                raise _err(231, f"{u.lower()} takes one value")
            sec.items.append(Item("fill", where, w, n, v[0]))
        else:
            n = X.parse(field)
            sec.items.append(Item("space", where, SPACE_W[u], n))

    # ------------------------------------------------------------ symbols
    def _lookup(self, mod, name, strict=False):
        s = mod.syms.get(name)
        if s is None:
            if strict or self.final:
                raise X.Unknown(name)
            self.undefined.add(name)
            return 0, 0
        k = s.kind
        if k == "label":
            sec = s.sec
            return sec.base + s.item.off, sec.width
        if k == "equ":
            if isinstance(s.expr, int):
                return s.expr, 0
            if s.busy:
                raise AsmError(f"{name!r} is defined in terms of itself")
            s.busy = True
            try:
                v, w = X.evaluate(s.expr, lambda n: self._lookup(mod, n, strict), lambda: (0, 0),
                                  lambda f, sn: self._func(mod, f, sn))
            finally:
                s.busy = False
            return v, 0
        if k == "extern":
            t = self.publics.get(name) if hasattr(self, "publics") else None
            if t is None:
                if self.final and self.link:
                    raise X.Unknown(name)
                return 0, s.width
            v, _ = self._lookup(t[0], name, strict)
            return v, s.width
        raise X.Unknown(name)

    def _func(self, mod, f, name):
        sec = mod.secmap.get(name)
        if sec is None:
            raise AsmError(f"section {name!r} does not exist (E232)")
        if f == "sizeof":
            return sec.size, 0
        return sec.base, sec.width

    # ------------------------------------------------------------ layout
    def _layout(self):
        changed = False
        for mod in self.modules:
            for sec in mod.sections:
                lc = 0
                for it in sec.items:
                    k = it.kind
                    if k == "org":
                        v, _ = self._value(mod, sec, it, it.a)
                        target = v - sec.base if sec.abs is not None else v
                        if target < lc:
                            if self.final:
                                self.d.error(it.where, 229, f"org 0x{v:X} is below the location counter "
                                             f"0x{sec.base + lc:X}")
                            target = lc
                        lc = target
                        it.off = lc
                        continue
                    if k == "align":
                        a = it.a if it.a > 0 else 1
                        pos = sec.base + lc if sec.abs is not None else lc
                        lc += (-pos) % a
                        it.off = lc
                        continue
                    if it.off != lc:
                        it.off = lc
                        changed = True
                    if k == "ins":
                        n = self._ins_size(mod, sec, it)
                    elif k == "bin":
                        n = len(it.data)
                    elif k == "data":
                        n = it.a * len(it.b)
                    elif k in ("fill", "space"):
                        cnt = self._count(mod, sec, it)
                        n = it.a * cnt
                    else:
                        n = 0
                    if n != it.size:
                        it.size = n
                        changed = True
                    lc += n
                if sec.size != lc:
                    sec.size = lc
                    changed = True
        return changed

    def _count(self, mod, sec, it):
        try:
            v, w = X.evaluate(it.b, lambda n: self._lookup(mod, n, strict=True), lambda: (0, 0))
        except X.Unknown as u:
            if self.final:
                self.d.error(it.where, 223, f"count uses {u.name!r}, not defined before this line")
            return 0
        if v <= 0 and it.kind == "space":
            if self.final:
                self.d.warn(it.where, 501, "ds count of 0 or less; 1 assumed")
            v = 1
        return max(v, 0)

    def _value(self, mod, sec, it, e):
        if isinstance(e, int):
            return e, 0
        try:
            return X.evaluate(e, lambda n: self._lookup(mod, n), lambda: (sec.base + it.off, sec.width),
                              lambda f, n: self._func(mod, f, n))
        except X.Unknown as u:
            if self.final:
                self.d.error(it.where, 221, f"undefined symbol {u.name!r}")
            return 0, 0
        except (X.ExprError, AsmError) as e2:
            if self.final:
                self.d.error(it.where, 206, str(e2))
            return 0, 0

    def _ins_size(self, mod, sec, it):
        try:
            b = self._encode(mod, sec, it, False)
        except (EncodeError, X.ExprError, AsmError, X.Unknown):
            return it.size or 1
        return len(b)

    def _encode(self, mod, sec, it, final):
        ev = lambda e: self._ev_strict(mod, sec, it, e)  # noqa: E731
        return encode(it.a, it.b, it.c, sec.base + it.off, ev, final)

    def _ev_strict(self, mod, sec, it, e):
        if isinstance(e, int):
            return e, 0
        return X.evaluate(e, lambda n: self._lookup(mod, n), lambda: (sec.base + it.off, sec.width),
                          lambda f, n: self._func(mod, f, n))

    # ------------------------------------------------------------ linking
    def _resolve_publics(self):
        self.publics = {}
        for mod in self.modules:
            for n, where in mod.public.items():
                s = mod.syms.get(n)
                if s is None or s.kind == "extern":
                    self.d.error(where, 210, f"public {n!r} is not defined in module {mod.name}")
                    continue
                if n in self.publics:
                    other = self.publics[n][1]
                    self.d.error(where, 203, f"public {n!r} also defined at {other}")
                    continue
                self.publics[n] = (mod, where)
        if self.link:
            for mod in self.modules:
                for n, s in mod.syms.items():
                    if s.kind != "extern":
                        continue
                    t = self.publics.get(n)
                    if t is None:
                        continue
                    ds = t[0].syms[n]
                    if ds.kind == "label" and ds.sec.abs is None and ds.sec.width != s.width:
                        self.d.error(s.where, 216, f"extern {n!r} is declared "
                                     f"{ATTR_NAME[s.width]} but its section "
                                     f"{ds.sec.name} is {ds.sec.attr}")

    def _place(self, placements, data_base, code_base):
        """Give every relocatable section an address: --place NAME=ADDR, else first fit."""
        used = []
        for mod in self.modules:
            for sec in mod.sections:
                if sec.abs is not None and sec.size:
                    used.append((sec.base, sec.base + sec.size))
        groups = {}
        order = []
        for mod in self.modules:
            for sec in mod.sections:
                if sec.abs is None:
                    if sec.name not in groups:
                        groups[sec.name] = []
                        order.append(sec.name)
                    groups[sec.name].append(sec)
        for name in order:
            secs = groups[name]
            total_align = max(s.align for s in secs)
            size = 0
            for s in secs:
                size += (-size) % s.align
                size += s.size
            limit = ATTR_LIMIT[secs[0].width]
            if name in placements:
                start = placements[name]
            else:
                start = None
                bases = [data_base if secs[0].kind == "data" else code_base, 0x100 if secs[0].width > 8 else 0]
                for b in bases:
                    start = _first_fit(used, b, size, total_align, limit)
                    if start is not None:
                        break
                if start is None:
                    self.d.error(secs[0].where, 216, f"no room for section {name} "
                                 f"({size} bytes, {secs[0].attr})")
                    continue
            if start + size > limit:
                self.d.error(secs[0].where, 216, f"section {name} at 0x{start:X} does not fit its "
                             f"{secs[0].attr} displacement")
            pos = start
            for s in secs:
                pos += (-pos) % s.align
                s.base = pos
                pos += s.size
            if size:
                used.append((start, start + size))

    # ------------------------------------------------------------ driver
    def assemble(self, link=True, placements=None, data_base=0x4000, code_base=0x200000, max_passes=24):
        self.link = link
        self.final = False
        self._resolve_publics()
        for _ in range(max_passes):
            if not self._layout():
                break
        else:
            self.d.error(None, 0, "instruction sizes did not settle")
        if link:
            self._place(placements or {}, data_base, code_base)
        else:
            for mod in self.modules:
                for sec in mod.sections:
                    if sec.abs is None:
                        sec.base = 0
        self.final = True
        self._layout()
        return self._emit()

    def _emit(self):
        """Encode every item at its final address; returns {addr: bytes} chunks and item spans."""
        chunks = []
        spans = []
        for mod in self.modules:
            for sec in mod.sections:
                for it in sec.items:
                    if not it.size:
                        continue
                    addr = sec.base + it.off
                    if self.link or sec.abs is not None:
                        spans.append((addr, addr + it.size, it.where, sec))
                    b = self._item_bytes(mod, sec, it)
                    if b is None:
                        continue
                    if len(b) != it.size:
                        self.d.error(it.where, 0, f"size changed from {it.size} to {len(b)} bytes "
                                     "between passes (internal)")
                        continue
                    chunks.append((addr, b, sec))
        self._overlaps(spans)
        return chunks

    def _item_bytes(self, mod, sec, it):
        k = it.kind
        if k == "ins":
            try:
                return self._encode(mod, sec, it, True)
            except EncodeError as e:
                self.d.error(it.where, 230, str(e))
            except X.Unknown as u:
                self.d.error(it.where, 221, f"undefined symbol {u.name!r}")
            except (X.ExprError, AsmError) as e:
                self.d.error(it.where, 206, str(e))
            return None
        if k == "bin":
            return it.data
        if k == "data":
            vals = []
            for v in it.b:
                if isinstance(v, (int, bytes)):
                    vals.append(v)
                else:
                    vals.append(self._value(mod, sec, it, v)[0])
            return _pack(it.a, vals, it.where, self.d)
        if k == "fill":
            v = it.c
            if not isinstance(v, (int, bytes)):
                v = self._value(mod, sec, it, v)[0]
            one = _pack(it.a, [v], it.where, self.d)
            return one * (it.size // max(len(one), 1)) if one else b""
        return None       # space: reserved, no bytes

    def _overlaps(self, spans):
        spans.sort(key=lambda s: (s[0], s[1]))
        last = None
        for s in spans:
            if last is not None and s[0] < last[1]:
                a, b = last, s
                self.d.error(b[2], 217, f"sections overlap at 0x{b[0]:X}: {b[2]} "
                             f"({b[3].name}) runs into bytes from {a[2]} ({a[3].name}, "
                             f"0x{a[0]:X}-0x{a[1] - 1:X})")
            if last is None or s[1] > last[1]:
                last = s

    def symbols(self):
        out = {}
        for mod in self.modules:
            for n, s in mod.syms.items():
                if s.kind == "extern":
                    continue
                try:
                    out[n] = self._lookup(mod, n)[0]
                except Exception:        # noqa: BLE001
                    pass
        return out


def _first_fit(used, base, size, align, limit):
    pos = base + (-base) % align
    for a, b in sorted(used):
        if b <= pos:
            continue
        if a >= pos + size:
            break
        pos = b + (-b) % align
    if pos + size > limit:
        return None
    return pos


def _err(code, msg):
    e = AsmError(msg)
    e.code = code
    return e


def _read(path):
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        return f.read()


def _names(field):
    out = []
    for n in field.split(","):
        n = n.strip()
        if not n:
            continue
        if not _IDENT.match(n):
            raise _err(208, f"illegal symbol name {n!r}")
        out.append(n)
    return out


def _split_args(field):
    from ..cpu.operands import split
    return split(field)


def _data_values(field, w):
    out = []
    for p in _split_args(field):
        if not p:
            raise _err(206, "empty value")
        if _NUMTOK.match(p):
            out.append(X.parse_number(p))
        elif p.startswith('"'):
            if w != 1:
                raise _err(209, "strings are allowed in db/dfb only")
            if len(p) < 2 or not p.endswith('"'):
                raise _err(209, "unterminated string")
            b = X.unescape(p[1:-1])
            if len(b) > 511:
                raise _err(209, "string longer than 511 bytes")
            out.append(b)
        else:
            out.append(X.parse(p))
    return out


def _pack(w, vals, where, d):
    out = bytearray()
    lo, hi = -(1 << (8 * w - 1)), (1 << (8 * w)) - 1
    if w == 4:
        lo, hi = -(1 << 31), (1 << 32) - 1
    for v in vals:
        if isinstance(v, bytes):
            out += v
            continue
        if not lo <= v <= hi:
            d.error(where, 501, f"value {v} does not fit in {w} byte(s)")
        out += (v & ((1 << (8 * w)) - 1)).to_bytes(w, "little")
    return bytes(out)


def build_image(chunks, origin=None, size=None, fill=0xFF, d=None):
    """Raw image from chunks. Default origin/size: the span of code/romdata bytes."""
    rom = [(a, b) for a, b, s in chunks if s.kind != "data"]
    if origin is None:
        origin = min((a for a, _ in rom), default=0)
    if size is None:
        size = max((a + len(b) for a, b in rom), default=origin) - origin
    img = bytearray([fill]) * size
    outside = 0
    for a, b in rom:
        lo, hi = max(a, origin), min(a + len(b), origin + size)
        if lo < hi:
            img[lo - origin:hi - origin] = b[lo - a:hi - a]
        outside += len(b) - max(0, hi - lo)
    if outside and d is not None:
        d.warn(None, 0, f"{outside} code/romdata bytes lie outside the image 0x{origin:X}+0x{size:X}")
    return bytes(img), origin, size
