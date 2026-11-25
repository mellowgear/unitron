"""Lift traced TLCS-900/H code to C (experimental).

  unitron lift ROM -o OUTDIR [--executed FILE] [--symbols FILE]

Output: OUTDIR/game.c (one C function per discovered function), OUTDIR/ngpc_regs.h and a copy of
ngpc_rt.h. The C is instruction-faithful: CPU registers are globals (macros over a register-file
struct), memory goes through rd8/wr8..., flags are computed only where a later instruction can
read them.
"""
import re
import shutil
from collections import deque
from pathlib import Path

from ..ngpc.rom import Rom
from ..ngpc.trace import trace, discover, annotate_bios, VECTORS
from ..ngpc import hwnames
from ..cpu.decode import Ref
from ..cpu.regs import REG8, REG16, REG32, CC

HERE = Path(__file__).resolve().parent
IO = {a: n for a, n, _, k, *_ in hwnames.NAMES if k != "vector"}

# ---------------------------------------------------------------- registers
_PAIRS = ["WA", "BC", "DE", "HL"]
_LOHI = {"WA": ("A", "W"), "BC": ("C", "B"), "DE": ("E", "D"), "HL": ("L", "H")}
_IDX = ["IX", "IY", "IZ", "SP"]


def _reg_table():
    """name -> (C lvalue, size in bytes)."""
    t = {}

    def part(base, size, off):
        if size == 4:
            return base
        return f"R{size * 8}({base},{off // size})"

    for i, p in enumerate(_PAIRS):
        lo, hi = _LOHI[p]
        for bank_tag, base in [("", f"cpu.b[cpu.rfp][{i}]"),
                               ("_P", f"cpu.b[(cpu.rfp-1)&3][{i}]")] + \
                [(str(n), f"cpu.b[{n}][{i}]") for n in range(4)]:
            numeric = bank_tag.isdigit()
            pre = "R" if numeric else ""
            t["X" + p + bank_tag] = (base, 4)
            t[(pre + p if numeric else p) + bank_tag] = (part(base, 2, 0), 2)
            t["Q" + p + bank_tag] = (part(base, 2, 2), 2)
            t[pre + lo + bank_tag] = (part(base, 1, 0), 1)
            t[pre + hi + bank_tag] = (part(base, 1, 1), 1)
            t["Q" + lo + bank_tag] = (part(base, 1, 2), 1)
            t["Q" + hi + bank_tag] = (part(base, 1, 3), 1)
    for i, r in enumerate(_IDX):
        base = f"cpu.x[{i}]"
        t["X" + r] = (base, 4)
        t[r] = (part(base, 2, 0), 2)
        t["Q" + r] = (part(base, 2, 2), 2)
        t[r + "L"] = (part(base, 1, 0), 1)
        t[r + "H"] = (part(base, 1, 1), 1)
        t["Q" + r + "L"] = (part(base, 1, 2), 1)
        t["Q" + r + "H"] = (part(base, 1, 3), 1)
    return t


REGS = _reg_table()


def cname(reg):
    """C identifier the generated code uses for a register."""
    return reg.replace("'", "_P")


def reg(name):
    return REGS.get(cname(name))


# ------------------------------------------------------------ operand parse
NUM = re.compile(r"^-?(0x[0-9A-Fa-f]+|\d+)$")


def num(s):
    return int(s, 0)


def is_num(s):
    return bool(NUM.match(str(s)))


class Mem:
    """A parsed memory operand."""
    def __init__(self, text, size):
        self.text, self.size = text, size
        self.pre = self.post = None
        body = text[1:-1] if text.startswith("(") else text
        step = None
        if ":" in body:
            body, step = body.split(":")
            step = int(step)
        if body.startswith("-") and reg(body[1:]):
            self.base, self.pre = body[1:], step or size or 1
            self.addr = cname(self.base)
        elif body.endswith("+") and reg(body[:-1]):
            self.base, self.post = body[:-1], step or size or 1
            self.addr = cname(self.base)
        elif is_num(body):
            self.addr = sym_addr(num(body))
        else:
            m = re.match(r"^([A-Z0-9']+)([+-])(.+)$", body)
            if m and reg(m.group(1)):
                b, sign, off = m.groups()
                if is_num(off):
                    self.addr = f"{cname(b)} {sign} {off}"
                else:   # (r32+r8) / (r32+r16): signed index register
                    isz = reg(off)[1]
                    self.addr = f"{cname(b)} + (int{isz * 8}_t){cname(off)}"
            elif reg(body):
                self.addr = cname(body)
            else:
                raise ValueError(f"cannot parse memory operand {text!r}")

    def setup(self):
        """Statements to run before the access (pre-decrement)."""
        return [f"{cname(self.base)} -= {self.pre};"] if self.pre else []

    def teardown(self):
        return [f"{cname(self.base)} += {self.post};"] if self.post else []


def sym_addr(a):
    if a in IO:
        return IO[a]
    if a in VECTORS:
        return VECTORS[a]
    return f"0x{a:X}"


def is_mem(op):
    return isinstance(op, str) and op.startswith("(")


def opsize(ins):
    """Operation size in bytes from register operands or mnemonic suffix."""
    for o in ins.ops:
        if not is_mem(o) and reg(o):
            return reg(o)[1]
    m = ins.mnem
    if m[-1] in "BWL" and m not in ("SUB", "SBC", "DJNZ", "ADC", "LDCF", "STCF"):
        return {"B": 1, "W": 2, "L": 4}[m[-1]]
    return None


def base_mnem(m):
    if len(m) > 2 and m[-1] in "BWL" and m not in (
            "SUB", "SBC", "DJNZ", "ADC", "LDCF", "STCF", "ANDCF", "ORCF", "XORCF",
            "RL", "SLL", "SRL", "CPL", "PUSHW", "POPW", "BS1B"):
        if m[:-1] in ("LD", "ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP",
                      "INC", "DEC", "PUSH", "POP", "RLC", "RRC", "RL", "RR", "SLA",
                      "SRA", "SLL", "SRL", "LDIR", "LDDR", "LDI", "LDD", "CPIR",
                      "CPDR", "CPI", "CPD"):
            return m[:-1]
    if m in ("PUSHW", "POPW"):
        return m[:-1]
    return m


# ------------------------------------------------------------------- flags
ALU = {"ADD", "ADC", "SUB", "SBC", "AND", "OR", "XOR", "CP"}
SHIFTS = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"}
FULL_WRITERS = ALU | SHIFTS | {"NEG", "DAA"}
READS_C = {"ADC", "SBC", "RL", "RR", "CCF", "STCF", "ANDCF", "ORCF", "XORCF", "DAA"}


def reads_flags(ins):
    m = base_mnem(ins.mnem)
    if m in ("JR", "JRL", "JP", "CALL", "RET") and ins.conditional:
        return True
    if m in ("SCC", "ZCF") or m in READS_C:
        return True
    if m == "PUSH" and ins.ops and ins.ops[0] in ("F", "SR"):
        return True
    if m == "EX" and ins.ops == ["F", "F'"]:
        return True
    return False


def writes_all_flags(ins):
    m = base_mnem(ins.mnem)
    if m in FULL_WRITERS and not (m in ("ADC", "SBC", "RL", "RR", "DAA")):
        return True
    if m == "POP" and ins.ops and ins.ops[0] in ("F", "SR"):
        return True
    return False


class Flow:
    def __init__(self, insns):
        self.insns = insns

    def succ(self, ins):
        out = []
        m = base_mnem(ins.mnem)
        if m in ("CALL", "CALR", "RET", "RETI", "RETD") and not ins.conditional:
            return out  # assume flags are dead across calls/returns (cc900)
        if ins.target is not None and not ins.is_call and ins.target in self.insns:
            out.append(ins.target)
        if not ins.ends_block:
            nxt = ins.addr + ins.size
            if nxt in self.insns:
                out.append(nxt)
        return out

    def flags_live_after(self, ins, depth=10):
        seen = set()
        stack = [(s, depth) for s in self.succ(ins)]
        if not stack and not ins.ends_block and base_mnem(ins.mnem) not in (
                "CALL", "CALR"):
            return True  # flows into untraced code: be conservative
        while stack:
            a, d = stack.pop()
            if a in seen:
                continue
            seen.add(a)
            i = self.insns[a]
            if reads_flags(i):
                return True
            if writes_all_flags(i):
                continue
            if d == 0:
                return True
            nx = self.succ(i)
            if not nx and not i.ends_block and base_mnem(i.mnem) not in ("CALL", "CALR"):
                return True
            stack.extend((s, d - 1) for s in nx)
        return False


# --------------------------------------------------------------- the lifter
U = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t"}
S = {1: "int8_t", 2: "int16_t", 4: "int32_t"}

CC_EXPR = {"F": "0", "T": "1", "Z": "cpu.fz", "NZ": "!cpu.fz", "C": "cpu.fc",
           "NC": "!cpu.fc", "MI": "cpu.fs", "PL": "!cpu.fs", "OV": "cpu.fv",
           "NOV": "!cpu.fv", "LT": "(cpu.fs ^ cpu.fv)", "GE": "!(cpu.fs ^ cpu.fv)",
           "LE": "(cpu.fz | (cpu.fs ^ cpu.fv))", "GT": "!(cpu.fz | (cpu.fs ^ cpu.fv))",
           "ULE": "(cpu.fc | cpu.fz)", "UGT": "!(cpu.fc | cpu.fz)"}


def fused_cmp(cc, a, b, size):
    """Condition of 'CP a,b ; Jcc' as a plain C comparison, or None."""
    u, s = U[size], S[size]
    ua, ub, sa, sb = f"({u})({a})", f"({u})({b})", f"({s})({a})", f"({s})({b})"
    return {"Z": f"{ua} == {ub}", "NZ": f"{ua} != {ub}",
            "C": f"{ua} < {ub}", "NC": f"{ua} >= {ub}",
            "ULE": f"{ua} <= {ub}", "UGT": f"{ua} > {ub}",
            "LT": f"{sa} < {sb}", "GE": f"{sa} >= {sb}",
            "LE": f"{sa} <= {sb}", "GT": f"{sa} > {sb}",
            "MI": f"({s})({ua} - {ub}) < 0", "PL": f"({s})({ua} - {ub}) >= 0",
            "T": "1", "F": "0"}.get(cc)


class Lifter:
    def __init__(self, rom, insns, labels, funcs):
        self.rom, self.insns, self.labels, self.funcs = rom, insns, labels, funcs
        self.flow = Flow(insns)
        self.unimpl = {}
        self.fused = set()     # addresses of branches whose condition was fused

    # --- operand helpers
    def rd(self, op, size, pre):
        """C rvalue for operand; appends setup statements to `pre`."""
        if is_num(op):
            return op
        if is_mem(op):
            m = Mem(op, size)
            pre.extend(m.setup())
            self._post.extend(m.teardown())
            return f"rd{size * 8}({m.addr})"
        if reg(op):
            return cname(op)
        raise ValueError(op)

    def wr(self, op, size, val, pre):
        if is_mem(op):
            m = Mem(op, size)
            pre.extend(m.setup())
            self._post.extend(m.teardown())
            return [f"wr{size * 8}({m.addr}, {val});"]
        return [f"{cname(op)} = {val};"]

    def rmw(self, op, size, fn):
        """Read-modify-write: fn(rvalue) -> C expression."""
        if is_mem(op):
            m = Mem(op, size)
            out = m.setup() + [f"{{ uint32_t ea = {m.addr}; "
                               f"wr{size * 8}(ea, {fn(f'rd{size * 8}(ea)')}); }}"]
            return out + m.teardown()
        r = cname(op)
        return [f"{r} = {fn(r)};"]

    def label(self, a):
        return f"L_{a:06X}"

    def fname(self, a):
        return self.labels.get(a) if self.labels.get(a, "").startswith(("sub_", "irq_", "dyn_", "start", "entry_")) \
            else f"sub_{a:06X}"

    def call(self, target, ret_addr, fn_here):
        if target in self.funcs:
            return [f"push32(0x{ret_addr:06X}); {self.funcs[target]}();"]
        return [f"push32(0x{ret_addr:06X}); call_ind(0x{target:06X});"]

    # --- one instruction
    def lift(self, ins, fentry):
        self._post = []
        try:
            out = self._lift(ins, fentry)
        except Exception as e:  # noqa: BLE001
            self.unimpl[base_mnem(ins.mnem)] = self.unimpl.get(base_mnem(ins.mnem), 0) + 1
            out = [f"UNIMPLEMENTED(\"{ins.text()}\");  /* {type(e).__name__}: {e} */"]
        return out + self._post

    def _branch_cond(self, cc, ins):
        if ins.addr in self.fused:
            return self.fused_conds[ins.addr]
        return CC_EXPR[cc]

    def goto_or_tail(self, target, fentry):
        if target in self.funcs and target != fentry:
            return f"{{ {self.funcs[target]}(); return; }}"  # tail call
        return f"goto {self.label(target)};"

    def _lift(self, ins, fentry):
        m, ops = base_mnem(ins.mnem), ins.ops
        nxt = ins.addr + ins.size
        sz = opsize(ins)
        live = self.flow.flags_live_after(ins) if (m in FULL_WRITERS or m in (
            "INC", "DEC", "BIT", "CPL", "NEG")) else False
        pre = []

        # ---------- control flow
        if m in ("JR", "JRL", "JP"):
            cc = ops[0] if len(ops) == 2 else "T"
            tgt = ops[-1]
            if ins.target is not None:
                stmt = self.goto_or_tail(ins.target, fentry)
            else:  # JP mem: jump to the effective address (not its contents)
                stmt = f"{{ jump_ind({Mem(tgt, 4).addr}); return; }}"
            if cc == "T":
                return pre + [stmt]
            return pre + [f"if ({self._branch_cond(cc, ins)}) {stmt}"]
        if m in ("CALL", "CALR"):
            cc = ops[0] if len(ops) == 2 else "T"
            tgt = ops[-1]
            if ins.target is not None:
                body = self.call(ins.target, nxt, fentry)
            else:
                body = [f"push32(0x{nxt:06X}); call_ind({Mem(tgt, 4).addr});"]
            if cc == "T":
                return body
            return [f"if ({self._branch_cond(cc, ins)}) {{ {' '.join(body)} }}"]
        if m == "RET":
            if ops:
                return [f"if ({self._branch_cond(ops[0], ins)}) {{ XSP += 4; return; }}"]
            return ["XSP += 4; return;"]
        if m == "RETD":
            return [f"XSP += 4 + ({ops[0]}); return;"]
        if m == "RETI":
            return ["reti(); return;"]
        if m == "DJNZ":
            r = cname(ops[0])
            return [f"if (--{r} != 0) {self.goto_or_tail(ins.target, fentry)}"]
        if m == "SWI":
            if ops == ["1"]:
                c = getattr(ins, "comment", "")
                return [f"bios_call();  /* {c} */" if c else "bios_call();"]
            return [f"swi({ops[0]});"]

        # ---------- loads / moves
        if m == "LD":
            d, s = ops
            size = sz or (reg(d) or reg(s) or (None, 1))[1]
            if m == "LD" and ins.mnem in ("LDB", "LDW") and is_mem(s) and is_mem(d):
                size = 1 if ins.mnem == "LDB" else 2
            if ins.mnem in ("LDB", "LDW", "LDL"):
                size = {"B": 1, "W": 2, "L": 4}[ins.mnem[-1]]
            v = self.rd(s, size, pre)
            return pre + self.wr(d, size, v, pre)
        if m == "LDX":
            return [f"wr8({Mem(ops[0], 1).addr}, {ops[1]});"]
        if m in ("LDA", "LDAR"):
            d, s = ops
            addr = Mem("(" + s + ")", 4).addr if not is_num(s) else s
            return [f"{cname(d)} = ({U[reg(d)[1]]})({addr});"]
        if m == "EX":
            if ops == ["F", "F'"]:
                return ["ex_f();"]
            a, b = ops
            size = sz
            va = self.rd(a, size, pre)
            vb = self.rd(b, size, pre)
            return pre + [f"{{ {U[size]} t = {va};"] + self.wr(a, size, vb, pre) + \
                self.wr(b, size, "t", pre) + ["}"]
        if m == "PUSH":
            o = ops[0]
            if o == "SR":
                return ["push16(get_sr());"]
            if o == "F":
                return ["push8(get_f());"]
            if o == "A":
                return ["push8(A);"]
            if is_num(o):
                size = 2 if ins.mnem == "PUSHW" else 1
                return [f"push{size * 8}({o});"]
            size = sz or (2 if ins.mnem == "PUSHW" else 1)
            v = self.rd(o, size, pre)
            return pre + [f"push{size * 8}({v});"]
        if m == "POP":
            o = ops[0]
            if o == "SR":
                return ["set_sr(pop16());"]
            if o == "F":
                return ["set_f(pop8());"]
            size = sz or (2 if ins.mnem == "POPW" else 1)
            return self.wr(o, size, f"pop{size * 8}()", pre)
        if m in ("LDIR", "LDDR", "LDI", "LDD"):
            size = 2 if ins.mnem.endswith("W") else 1
            dst, src = ops[0][1:-2], ops[1][1:-2]
            step = f"{'+' if m in ('LDIR', 'LDI') else '-'}= {size}"
            body = (f"wr{size * 8}({dst}, rd{size * 8}({src})); {dst} {step}; "
                    f"{src} {step}; BC--;")
            if m in ("LDIR", "LDDR"):
                return [f"do {{ {body} }} while (BC != 0);", "cpu.fh = cpu.fn = 0; cpu.fv = 0;"]
            return [body, "cpu.fh = cpu.fn = 0; cpu.fv = (BC != 0);"]
        if m in ("CPIR", "CPDR", "CPI", "CPD"):
            size = 2 if ins.mnem.endswith("W") else 1
            acc, ptr = cname(ops[0]), ops[1][1:-2]
            step = f"{'+' if m in ('CPIR', 'CPI') else '-'}= {size}"
            one = (f"alu_cp({acc}, rd{size * 8}({ptr}), {size * 8}); {ptr} {step}; BC--; "
                   f"cpu.fv = (BC != 0);")
            if m in ("CPIR", "CPDR"):
                return [f"do {{ {one} }} while (BC != 0 && !cpu.fz);"]
            return [one]

        # ---------- ALU
        if m in ALU:
            d, s = ops
            size = sz
            vs = self.rd(s, size, pre)
            if m == "CP":
                vd = self.rd(d, size, pre)
                # Fuse with a following conditional branch when flags die there.
                n = self.insns.get(nxt)
                if n is not None and n.conditional and base_mnem(n.mnem) in (
                        "JR", "JRL", "JP", "RET", "CALL") and not self.flow.flags_live_after(n):
                    cond = fused_cmp(n.ops[0], vd, vs, size)
                    if cond:
                        self.fused.add(nxt)
                        self.fused_conds[nxt] = cond
                        return pre + [f"/* CP {ins.ops[0]},{ins.ops[1]} */"]
                if not live:
                    return pre + ["/* CP: flags unused */"]
                return pre + [f"alu_cp({vd}, {vs}, {size * 8});"]
            op_c = {"ADD": "+", "SUB": "-", "AND": "&", "OR": "|", "XOR": "^"}.get(m)
            n = self.insns.get(nxt)
            fuse_zero = (m in ("AND", "OR", "XOR") and n is not None and n.conditional
                         and base_mnem(n.mnem) in ("JR", "JRL", "JP", "RET")
                         and n.ops[0] in ("Z", "NZ") and not self.flow.flags_live_after(n))
            if (not live or fuse_zero) and op_c:
                if fuse_zero:
                    self.fused.add(nxt)
                    if is_mem(d):
                        dv = None
                    else:
                        dv = cname(d)
                    if dv:
                        self.fused_conds[nxt] = f"({U[size]}){dv} {'==' if n.ops[0] == 'Z' else '!='} 0"
                    else:
                        self.fused.discard(nxt)
                        return self.rmw(d, size, lambda x: f"alu_{m.lower()}({x}, {vs}, {size * 8})")
                if not is_mem(d) and d == s and m in ("AND", "OR"):
                    return pre + [f"/* {m} {d},{d}: test */"]
                return pre + self.rmw(d, size, lambda x: f"({U[size]})({x} {op_c} {vs})")
            if not live and m in ("ADC", "SBC"):
                pass
            return pre + self.rmw(d, size, lambda x: f"alu_{m.lower()}({x}, {vs}, {size * 8})")
        if m in ("INC", "DEC"):
            n_, d = ops
            size = sz
            flags = size == 1 or is_mem(d)
            op_c = "+" if m == "INC" else "-"
            if not flags or not live:
                return self.rmw(d, size, lambda x: f"({U[size]})({x} {op_c} {n_})")
            return self.rmw(d, size, lambda x: f"alu_{m.lower()}({x}, {n_}, {size * 8})")
        if m == "NEG":
            size = sz
            return self.rmw(ops[0], size, (lambda x: f"alu_sub(0, {x}, {size * 8})") if live
                            else (lambda x: f"({U[size]})(-{x})"))
        if m == "CPL":
            size = sz
            return self.rmw(ops[0], size, lambda x: f"({U[size]})~{x}") + ["cpu.fh = cpu.fn = 1;"]
        if m in SHIFTS:
            if len(ops) == 1:
                cnt, d = "1", ops[0]
            else:
                cnt, d = ops
                if cnt == "A":
                    cnt = "((A & 15) ? (A & 15) : 16)"
            size = sz or 1
            if not live and m in ("SLL", "SLA", "SRL", "SRA") and is_num(cnt):
                c = int(cnt)
                if m in ("SLL", "SLA"):
                    f = lambda x: f"({U[size]})({x} << {c})"  # noqa: E731
                elif m == "SRL":
                    f = lambda x: f"({U[size]})({x} >> {c})"  # noqa: E731
                else:
                    f = lambda x: f"({U[size]})(({S[size]}){x} >> {c})"  # noqa: E731
                return self.rmw(d, size, f)
            return self.rmw(d, size, lambda x: f"shift_{m.lower()}({x}, {cnt}, {size * 8})")
        if m == "BIT":
            b, d = ops
            size = sz or 1
            v = self.rd(d, size, pre)
            n = self.insns.get(nxt)
            if n is not None and n.conditional and n.ops and n.ops[0] in ("Z", "NZ") \
                    and not self.flow.flags_live_after(n):
                self.fused.add(nxt)
                t = f"(({v} >> {b}) & 1)"
                self.fused_conds[nxt] = f"!{t}" if n.ops[0] == "Z" else t
                return pre + [f"/* BIT {b},{d} */"]
            return pre + [f"cpu.fz = !(({v} >> {b}) & 1); cpu.fh = 1; cpu.fn = 0;"]
        if m in ("SET", "RES", "CHG", "TSET"):
            b, d = ops
            size = sz or 1
            f = {"SET": lambda x: f"({U[size]})({x} | (1u << {b}))",
                 "RES": lambda x: f"({U[size]})({x} & ~(1u << {b}))",
                 "CHG": lambda x: f"({U[size]})({x} ^ (1u << {b}))",
                 "TSET": lambda x: f"tset({x}, {b})"}[m]
            return self.rmw(d, size, f)
        if m in ("ANDCF", "ORCF", "XORCF", "LDCF", "STCF"):
            b, d = ops
            size = sz or 1
            bit = "(A & 15)" if b == "A" else b
            if m == "STCF":
                return self.rmw(d, size, lambda x: f"({U[size]})(({x} & ~(1u << {bit})) | ((uint32_t)cpu.fc << {bit}))")
            v = self.rd(d, size, pre)
            e = f"(({v} >> {bit}) & 1)"
            op = {"ANDCF": "&=", "ORCF": "|=", "XORCF": "^=", "LDCF": "="}[m]
            return pre + [f"cpu.fc {op} {e};"]
        if m in ("RCF", "SCF", "CCF", "ZCF"):
            return [{"RCF": "cpu.fc = 0; cpu.fh = cpu.fn = 0;",
                     "SCF": "cpu.fc = 1; cpu.fh = cpu.fn = 0;",
                     "CCF": "cpu.fh = cpu.fc; cpu.fc = !cpu.fc; cpu.fn = 0;",
                     "ZCF": "cpu.fc = !cpu.fz; cpu.fn = 0;"}[m]]
        if m == "SCC":
            cc, d = ops
            return [f"{cname(d)} = {CC_EXPR[cc]} ? 1 : 0;"]
        if m in ("MUL", "MULS", "DIV", "DIVS"):
            rr, s = ops
            big = reg(rr)[1]
            half = big // 2
            vs = self.rd(s, half, pre)
            fn = {"MUL": "mulu", "MULS": "muls", "DIV": "divu", "DIVS": "divs"}[m]
            return pre + [f"{cname(rr)} = {fn}{half * 8}({cname(rr)}, {vs});"]
        if m in ("EXTZ", "EXTS"):
            r = cname(ops[0])
            size = reg(ops[0])[1]
            h = size // 2
            if m == "EXTZ":
                return [f"{r} = ({U[size]})({U[h]}){r};"]
            return [f"{r} = ({U[size]})({S[h]}){r};"]
        if m == "PAA":
            r = cname(ops[0])
            return [f"if ({r} & 1) {r}++;"]
        if m == "MIRR":
            return [f"{cname(ops[0])} = mirr16({cname(ops[0])});"]
        if m == "DAA":
            return [f"{cname(ops[0])} = daa({cname(ops[0])});"]
        if m in ("MINC1", "MINC2", "MINC4", "MDEC1", "MDEC2", "MDEC4"):
            n_, d = ops
            k = m[-1]
            r = cname(d)
            if m.startswith("MINC"):
                return [f"{r} = (({r} % {n_}) == {n_} - {k}) ? ({r} - ({n_} - {k})) : ({r} + {k});"]
            return [f"{r} = (({r} % {n_}) == 0) ? ({r} + ({n_} - {k})) : ({r} - {k});"]
        if m == "LINK":
            r, dd = ops
            return [f"push32({cname(r)}); {cname(r)} = XSP; XSP += ({dd});"]
        if m == "UNLK":
            r = cname(ops[0])
            return [f"XSP = {r}; {r} = pop32();"]
        if m == "LDF":
            return [f"cpu.rfp = {ops[0]} & 3;"]
        if m == "INCF":
            return ["cpu.rfp = (cpu.rfp + 1) & 3;"]
        if m == "DECF":
            return ["cpu.rfp = (cpu.rfp - 1) & 3;"]
        if m == "LDC":
            d, s = ops
            if reg(d):
                return [f"{cname(d)} = ldc_read(\"{s}\");"]
            return [f"ldc_write(\"{d}\", {cname(s)});"]
        if m == "DI":
            return ["cpu.iff = 7;"]
        if m == "EI":
            return [f"cpu.iff = {ops[0]};"]
        if m == "NOP":
            return [";"]
        if m == "HALT":
            return ["halt();"]
        if m in ("RLD", "RRD"):
            mm = Mem(ops[1], 1)
            if m == "RLD":
                body = ("wr8(ea, (uint8_t)((t << 4) | (A & 15))); A = (uint8_t)((A & 0xF0) | (t >> 4));")
            else:
                body = ("wr8(ea, (uint8_t)((A << 4) | (t >> 4))); A = (uint8_t)((A & 0xF0) | (t & 15));")
            return mm.setup() + [f"{{ uint32_t ea = {mm.addr}; uint8_t t = rd8(ea); {body} "
                                 "szflags(A, 8); cpu.fv = parity(A); cpu.fh = cpu.fn = 0; }"] + mm.teardown()
        if m in ("BS1F", "BS1B"):
            return [f"A = bs1{m[-1].lower()}({cname(ops[1])});"]
        raise NotImplementedError(m)

    # --- whole function
    def function(self, entry, name):
        body = func_body(self.insns, entry, self.funcs)
        targets = set()
        for a in body:
            i = self.insns[a]
            if i.target is not None and not i.is_call and i.target in body:
                if not (i.target in self.funcs and i.target != entry):
                    targets.add(i.target)
        self.fused_conds = getattr(self, "fused_conds", {})
        lines = [f"void {name}(void)", "{"]
        order = sorted(body)
        # put the entry first; everything else in address order
        prev = None
        if order[0] != entry:
            lines.append(f"    goto {self.label(entry)};")
        for a in order:
            i = self.insns[a]
            if prev is not None and prev + self.insns[prev].size != a and \
                    not self.insns[prev].ends_block:
                lines.append(f"    goto {self.label(prev + self.insns[prev].size)};")
            if a in targets or a == entry and order[0] != entry:
                lines.append(f"{self.label(a)}:")
            stmts = self.lift(i, entry)
            lines.append(f"    {' '.join(stmts):<60} /* {a:06X} {i.text()} */")
            prev = a
        last = self.insns[order[-1]]
        if not last.ends_block:
            nxt = last.addr + last.size
            lines.append(f"    {self.goto_or_tail(nxt, entry) if nxt in self.funcs else 'fallthrough_unknown(0x%06X); return;' % nxt}")
        lines.append("}")
        # labels referenced but placed? ensure every goto target exists
        return lines


def func_body(insns, entry, funcs):
    """Intraprocedural reachability from entry (stops at other functions' entries)."""
    body, work = set(), deque([entry])
    while work:
        a = work.popleft()
        if a in body or a not in insns:
            continue
        if a != entry and a in funcs:
            continue  # tail into another function
        body.add(a)
        i = insns[a]
        if i.target is not None and not i.is_call:
            work.append(i.target)
        if not i.ends_block:
            work.append(a + i.size)
    return body


def run(rom_path, outdir, executed=None, symbols=None):
    rom = Rom(Path(rom_path).read_bytes())
    h = rom.header()
    insns, labels, _ = discover(rom, executed, do_sweep=True)
    # BIOS-call annotation needs a linear pass in address order per block
    regs = {}
    for addr in sorted(insns):
        annotate_bios(insns[addr], regs)
        if insns[addr].ends_block:
            regs = {}
    # Function entries: start, IRQ handlers, call targets, dynamic heads that no
    # other function reaches by local flow.
    funcs = {}
    funcs[h["entry"]] = "start"
    for addr, i in insns.items():
        if i.is_call and i.target is not None and i.target in insns:
            funcs.setdefault(i.target, f"sub_{i.target:06X}")
    for addr, n in labels.items():
        if n.startswith("irq_") and addr in insns:
            funcs[addr] = n
    reached = set()
    for e in list(funcs):
        reached |= func_body(insns, e, funcs)
    for addr, n in sorted(labels.items()):
        if n.startswith("dyn_") and addr in insns and addr not in reached:
            funcs[addr] = f"fn_{addr:06X}"
            reached |= func_body(insns, addr, funcs)
    if symbols:                                    # readable names for known functions
        taken = set(funcs.values())
        for raw in Path(symbols).read_text().splitlines():
            w = raw.split("#", 1)[0].split(";", 1)[0].split()
            if len(w) >= 2 and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", w[1]):
                ad = int(w[0], 16)
                if ad in funcs and w[1] not in taken and funcs[ad] != "start":
                    taken.discard(funcs[ad])
                    funcs[ad] = w[1]
                    taken.add(w[1])
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy(HERE / "ngpc_rt.h", out / "ngpc_rt.h")
    regs_h = ["/* generated by unitron lift: register names as in the disassembly */",
              "#ifndef NGPC_REGS_H", "#define NGPC_REGS_H"]
    regs_h += [f"#define {n} ({e})" for n, (e, _) in sorted(REGS.items())]
    regs_h += ["", "/* hardware addresses (unitron/ngpc/hwnames.py) */"]
    regs_h += [f"#define {n} 0x{a:X}" for a, n in sorted({**IO, **VECTORS}.items())]
    (out / "ngpc_regs.h").write_text("\n".join(regs_h + ["#endif", ""]))
    lf = Lifter(rom, insns, labels, funcs)
    lf.fused_conds = {}
    src = [f"/* {h['title']} -- lifted from the ROM by unitron lift. Do not edit by hand;",
           " * regenerate instead. One C function per discovered TLCS-900/H function. */",
           '#include "ngpc_rt.h"', ""]
    src += [f"void {n}(void);" for _, n in sorted(funcs.items())] + [""]
    total = 0
    for e, n in sorted(funcs.items()):
        src += lf.function(e, n) + [""]
        total += 1
    src += ["void call_ind(uint32_t a)", "{", "    switch (a & 0xFFFFFF) {"]
    src += [f"    case 0x{e:06X}: {n}(); return;" for e, n in sorted(funcs.items())]
    src += ["    default: unknown_code(a); return;", "    }", "}", ""]
    src += ["void jump_ind(uint32_t a) { call_ind(a); }", ""]
    (out / "game.c").write_text("\n".join(src) + "\n")
    return {"functions": total, "instructions": len(insns), "unimplemented": sum(lf.unimpl.values())}


def cli(a):
    st = run(a.rom, a.output, a.executed, a.symbols)
    print(f"functions={st['functions']} instructions={st['instructions']} unimplemented={st['unimplemented']}")
    return 0


def add_parser(sp, fn):
    p = sp.add_parser("lift", help="lift traced code to C (experimental)")
    p.add_argument("rom")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--executed", help="file of hex code addresses (extra trace entry points)")
    p.add_argument("--symbols", help="ADDRESS NAME lines: function names")
    p.set_defaults(fn=fn)
