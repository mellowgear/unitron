"""NGPC ROM code discovery: recursive trace from the entry point, plus an optional gap sweep.

`executed` input (optional) is a plain list of hex code addresses from any source (a log, a
coverage file); unitron itself never runs code.
"""
from collections import deque

from ..cpu.decode import decode
from .hwnames import VECTORS as _VEC

VECTORS = {a: n for a, n, *_ in _VEC}


def annotate_bios(ins, regs):
    """Track RW3 loads; comment `swi 1` with the BIOS vector number it selects (SNK SysCall)."""
    if ins.mnem == "LD" and len(ins.ops) == 2 and ins.ops[0] == "RW3":
        try:
            regs["RW3"] = int(ins.ops[1], 0)
        except ValueError:
            regs.pop("RW3", None)
    elif ins.mnem == "SWI" and ins.ops == ["1"]:
        n = regs.get("RW3")
        ins.comment = f"BIOS call 0x{n:02X}" if n is not None else "BIOS call (RW3 not known here)"


def trace(rom, entries, insns=None, labels=None):
    """Recursive-descent disassembly. Returns (insns by addr, labels)."""
    insns = {} if insns is None else insns
    labels = {} if labels is None else labels
    work = deque()
    for e, name in entries:
        labels.setdefault(e, name)
        work.append(e)
    while work:
        pc = work.popleft()
        regs = {}  # last 32-bit immediate per register (for vector installs)
        while rom.contains(pc) and pc not in insns:
            ins = decode(rom, pc)
            insns[pc] = ins
            if ins.imm32 is not None:
                regs[ins.dst_reg] = ins.imm32
            annotate_bios(ins, regs)
            # "LD (vector),XRR" after "LD XRR,handler" installs an IRQ handler
            if ins.mnem == "LD" and len(ins.ops) == 2 and hasattr(ins.ops[0], "addr"):
                v = ins.ops[0].addr
                if v in VECTORS and ins.ops[1] in regs and rom.contains(regs[ins.ops[1]]):
                    h = regs[ins.ops[1]]
                    labels.setdefault(h, "irq_" + VECTORS[v][4:].lower())
                    work.append(h)
            if ins.target is not None and rom.contains(ins.target):
                pre = "sub" if ins.is_call else "loc"
                labels.setdefault(ins.target, f"{pre}_{ins.target:06X}")
                work.append(ins.target)
            if ins.invalid or ins.ends_block:
                break
            pc += ins.size
    return insns, labels


def _covered(insns):
    """Set of every byte address covered by a decoded instruction."""
    cov = set()
    for a, i in insns.items():
        cov.update(range(a, a + i.size))
    return cov


def _plausible(rom, start, insns, cov, lo, hi, max_len=4000):
    """Linear-decode from `start`; True if it looks like a real function.

    Rejects: invalid opcodes, running into known code mid-instruction, SWI 7 /
    erased-flash (0xFF) or NOP sleds, direct branches out of [lo, hi) or into
    the middle of known instructions, and 900-only NORMAL/MAX opcodes.
    """
    if rom(start) in (0xFF, None) or (rom(start) == 0 and rom(start + 1) == 0):
        return False
    pc, n = start, 0
    while n < max_len:
        if pc in insns:
            return n > 0          # ran into known code on a boundary
        if pc in cov:
            return False          # into the middle of a known instruction
        ins = decode(rom, pc)
        if ins.invalid or ins.mnem in ("NORMAL", "MAX", "HALT") or ins.illegal or \
                (ins.mnem == "SWI" and ins.ops == ["7"]):
            return False
        if ins.raw.count(0xFF) == len(ins.raw) and len(ins.raw) > 1:
            return False
        t = ins.target
        if t is not None and (not lo <= t < hi or (t in cov and t not in insns)):
            return False
        n += 1
        if ins.ends_block:
            return True
        pc += ins.size
    return False


def _bad(ins):
    return (ins.invalid or ins.illegal or ins.mnem in ("NORMAL", "MAX", "HALT", "LDX") or
            (ins.mnem == "SWI" and ins.ops != ["1"]) or
            any("r?" in str(o) for o in ins.ops))   # undefined register code


def _component(rom, seed, insns, lo, hi):
    """Trace from seed into a scratch dict, stopping at known code.

    Returns the new instructions, or None if any of them looks like data.
    """
    new, work = {}, deque([seed])
    while work:
        pc = work.popleft()
        nops = 0
        while lo <= pc < hi and pc not in insns and pc not in new:
            ins = decode(rom, pc)
            if _bad(ins):
                return None
            nops = nops + 1 if ins.mnem == "NOP" else 0
            if nops >= 2:
                return None
            new[pc] = ins
            t = ins.target
            if t is not None:
                if not lo <= t < hi:
                    return None
                if not ins.is_call:      # callees are validated on their own
                    work.append(t)
            if ins.ends_block:
                break
            pc += ins.size
        else:
            if not (lo <= pc < hi):
                return None
    # no overlaps with known code or within the component
    cov = {}
    for a, i in list(new.items()) + [(a, insns[a]) for a in ()]:
        for b in range(a, a + i.size):
            if b in cov:
                return None
            cov[b] = a
    for a in new:
        for b in range(a, a + new[a].size):
            if b != a and b in insns:
                return None
    return new


def sweep(rom, insns, labels, rounds=30):
    """Fill gaps in the code region with code that decodes as plausible functions.

    The region is fixed from the code already known (static + executed list). A
    candidate is accepted only if everything reachable from it, up to known
    code, decodes cleanly and does not overlap known instructions.

    Evidence order: call targets (most callers first), then the byte after a
    block end; only once those converge, 32-bit values in the ROM that point
    into the region. Code found from such a pointer is the weakest guess, so a
    call target that collides with it evicts it.
    """
    if not insns:
        return 0
    lo, hi = min(insns), max(a + i.size for a, i in insns.items())
    n0 = len(insns)
    d = rom.data
    ptrs = set()
    for off in range(0, len(d) - 3):
        v = d[off] | d[off + 1] << 8 | d[off + 2] << 16 | d[off + 3] << 24
        if lo <= v < hi:
            ptrs.add(v)
    weak = {}            # addr -> component id, for pointer-seeded code
    comps = {}           # component id -> set of addrs
    cov = {}
    for a, i in insns.items():
        for b in range(a, a + i.size):
            cov[b] = a

    def place(comp, sa, is_weak):
        insns.update(comp)
        for a, i in comp.items():
            for b in range(a, a + i.size):
                cov[b] = a
            if i.target is not None and i.target in comp:
                labels.setdefault(i.target, f"{'sub' if i.is_call else 'loc'}_{i.target:06X}")
        if is_weak:
            comps[sa] = set(comp)
            for a in comp:
                weak[a] = sa

    def evict(cid):
        for a in comps.pop(cid):
            i = insns.pop(a)
            weak.pop(a, None)
            for b in range(a, a + i.size):
                if cov.get(b) == a:
                    del cov[b]
            if labels.get(a, "").startswith(("swp_", "loc_", "sub_")):
                labels.pop(a, None)

    for phase in (0, 1):
        for _ in range(rounds):
            callers = {}
            for i in insns.values():
                if i.is_call and i.target is not None:
                    callers[i.target] = callers.get(i.target, 0) + 1
            heads = {a + i.size for a, i in insns.items() if i.ends_block}
            pool = set(callers) | heads | (ptrs if phase else set())
            seeds = sorted(pool, key=lambda x: (0 if x in callers else 1 if x in heads else 2,
                                                -callers.get(x, 0), x))
            accepted = 0
            for sa in seeds:
                if not lo <= sa < hi or sa in insns:
                    continue
                if rom(sa) == 0xFF or (rom(sa) == 0 and rom(sa + 1) == 0):
                    continue
                strong = sa in callers or sa in heads
                if sa in cov and not (strong and cov[sa] in weak):
                    continue
                # build against a view without the weak code we might evict
                comp = _component(rom, sa, insns, lo, hi)
                if comp is None and strong:
                    hit = {weak[b] for b in range(sa, sa + 16) if b in weak}
                    if hit:
                        saved = {c: {a: insns[a] for a in comps[c]} for c in hit}
                        for c in hit:
                            evict(c)
                        comp = _component(rom, sa, insns, lo, hi)
                        if comp is None:
                            for c, body in saved.items():
                                place(body, c, True)
                            continue
                if not comp:
                    continue
                clash = {cov[b] for a, i in comp.items() for b in range(a, a + i.size) if b in cov}
                if clash:
                    owners = {weak.get(c) for c in clash}
                    if strong and None not in owners:
                        for c in owners:
                            if c in comps:
                                evict(c)
                    else:
                        continue
                place(comp, sa, not strong)
                labels.setdefault(sa, f"{'sub' if sa in callers else 'swp'}_{sa:06X}")
                accepted += 1
            if not accepted:
                break
    return len(insns) - n0


def discover(rom, executed_path=None, do_sweep=False):
    """Static trace + optional executed-address list + optional gap sweep -> (insns, labels, stats)."""
    h = rom.header()
    insns, labels = trace(rom, [(h["entry"], "start")])
    st = {"static": len(insns)}
    if executed_path:
        ex = sorted(int(x, 16) for x in open(executed_path).read().split())
        trace(rom, [(x, f"dyn_{x:06X}") for x in ex if x not in insns], insns, labels)
        st["dynamic"] = len(insns) - st["static"]
        st["misaligned"] = [x for x in ex if x not in insns]
    if do_sweep:
        st["sweep"] = sweep(rom, insns, labels)
    # drop dyn_/swp_ labels that straight-line code already reaches
    for x in [k for k, v in labels.items() if v.startswith(("dyn_", "swp_"))]:
        prev = next((p for p in range(x - 1, x - 12, -1) if p in insns), None)
        if prev is not None and prev + insns[prev].size == x and not insns[prev].ends_block:
            del labels[x]
    return insns, labels, st


