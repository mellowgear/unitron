"""One instruction line: mnemonic + operand text -> parsed operands -> bytes."""
from ..cpu import operands as O
from ..cpu.encode import encode, split_mnemonic, COND_FIRST, BARE_ADDR, EncodeError
from ..cpu.regs import CC_CODE
from . import expr as X


class LineError(Exception):
    pass


def parse_insn(mnem, field):
    """-> (base mnemonic, size suffix, operands). Raises LineError."""
    sm = split_mnemonic(mnem)
    if sm is None:
        raise LineError(f"unknown mnemonic {mnem!r}")
    base, sfx = sm
    texts = O.split(field) if field.strip() else []
    ops = []
    n = len(texts)
    try:
        for i, t in enumerate(texts):
            cond = False
            if base in COND_FIRST and i == 0:
                u = t.strip().upper()
                if base == "SCC" or (base == "RET" and n == 1) or (n >= 2 and u in CC_CODE):
                    cond = True
            bare = base in BARE_ADDR and i == n - 1 and not (base == "LDA" and i == 0)
            ops.append(O.parse(t, bare_addr=bare, cond=cond))
    except (O.OperandError, X.ExprError) as e:
        raise LineError(str(e)) from None
    return base, sfx, ops


def _nosym(name):
    raise LineError(f"symbol {name!r} in an expression (no symbol table here)")


def _const(e):
    if isinstance(e, int):
        return e, 0
    return X.evaluate(e, _nosym, lambda: _nosym("$"))


def assemble_line(text, pc=0, ev=None):
    """Encode one instruction written as 'mnem operands' (no label/comment)."""
    t = text.strip()
    mnem, _, field = t.partition("\t") if "\t" in t else t.partition(" ")
    base, sfx, ops = parse_insn(mnem.strip(), field)
    try:
        return encode(base, sfx, ops, pc, ev or _const, True)
    except EncodeError as e:
        raise LineError(str(e)) from None
