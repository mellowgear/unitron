"""TLCS-900/H register names and codes, shared by the decoder and the encoder.

Register codes follow the datasheet's extended register map ("r" codes, 900/H maximum mode).
"""

REG8 = ["W", "A", "B", "C", "D", "E", "H", "L"]
REG16 = ["WA", "BC", "DE", "HL", "IX", "IY", "IZ", "SP"]
REG32 = ["XWA", "XBC", "XDE", "XHL", "XIX", "XIY", "XIZ", "XSP"]
SHORT = {1: {n: i for i, n in enumerate(REG8)},
         2: {n: i for i, n in enumerate(REG16)},
         4: {n: i for i, n in enumerate(REG32)}}

# Condition codes; 8 (T, always) is printed without a condition.
CC = ["F", "LT", "LE", "ULE", "OV", "MI", "Z", "C",
      "T", "GE", "GT", "UGT", "NOV", "PL", "NZ", "NC"]
CC_ALIAS = {"EQ": 6, "NE": 14, "ULT": 7, "UGE": 15, "M": 5, "P": 13, "PE": 4, "PO": 12}
CC_CODE = {n: i for i, n in enumerate(CC)}
CC_CODE.update(CC_ALIAS)

ALU = ["ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"]
SHIFT = ["RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"]
SFX = {1: "B", 2: "W", 4: "L"}

# Control registers for LDC: (code, size) -> name. 0x3C word is INTNEST on 900/H.
CR = {}
for _i in range(4):
    CR[(0x00 + 4 * _i, 4)] = f"DMAS{_i}"
    CR[(0x10 + 4 * _i, 4)] = f"DMAD{_i}"
    CR[(0x20 + 4 * _i, 2)] = f"DMAC{_i}"
    CR[(0x22 + 4 * _i, 1)] = f"DMAM{_i}"
CR[(0x3C, 2)] = "INTNEST"
CR[(0x3C, 4)] = "XNSP"
CR_CODE = {n: (c, s) for (c, s), n in CR.items()}

_PAIR = ["WA", "BC", "DE", "HL"]
_LO_HI = [("A", "W"), ("C", "B"), ("E", "D"), ("L", "H")]
_IDX = ["IX", "IY", "IZ", "SP"]


def ext_reg(code, size):
    """Name of an extended register code at a size, or None if the code has no register."""
    off = code & 3
    grp = (code >> 2) & 3
    if code < 0x40 or 0xD0 <= code < 0xF0:
        lo, hi = _LO_HI[grp]
        pair = _PAIR[grp]
        if code < 0x40:
            tag, pre = str(code >> 4), "R"
        elif code < 0xE0:
            tag, pre = "'", ""
        else:
            tag, pre = "", ""
        if size == 1:
            base = [pre + lo, pre + hi, "Q" + lo, "Q" + hi][off]
        elif size == 2:
            base = (pre + pair) if off < 2 else ("Q" + pair)
        else:
            base = "X" + pair
        return base + tag
    if code >= 0xF0:
        r = _IDX[grp]
        if size == 1:
            return [r + "L", r + "H", "Q" + r + "L", "Q" + r + "H"][off]
        if size == 2:
            return r if off < 2 else "Q" + r
        return "X" + r
    return None


def canonical_code(code, size):
    """True if `code` is the code the encoder uses for its register at this size."""
    return (size != 2 or code & 1 == 0) and (size != 4 or code & 3 == 0)


# name -> (size, code); the first (canonical) code for each name.
REG = {}
for _size in (1, 2, 4):
    for _code in range(256):
        if not canonical_code(_code, _size):
            continue
        _n = ext_reg(_code, _size)
        if _n is not None:
            REG.setdefault(_n, (_size, _code))
