"""Enumerate the TLCS-900/H opcode space as byte strings (shared by tests)."""
import itertools

TAILS = [bytes(8), bytes([0xFF] * 8), bytes([0x7F, 0x80, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06]),
         bytes([0x05, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC]), bytes([0x80, 0xFF, 0x7F, 0x00, 1, 2, 3, 4]),
         bytes([0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])]


def mem_heads():
    """Representative memory-operand prefixes for every group and addressing mode."""
    out = []
    for grp in (0x80, 0x90, 0xA0, 0xB0):
        for r in range(8):
            out.append(bytes([grp | r]))
            for d in (0x00, 0x05, 0x7F, 0x80, 0xFB):
                out.append(bytes([grp | 8 | r, d]))
        hi = grp + 0x40
        for a in (b"\x00", b"\x82", b"\xFF"):
            out.append(bytes([hi]) + a)
        for a in (b"\x00\x00", b"\x34\x12", b"\x80\x00", b"\xFF\xFF"):
            out.append(bytes([hi | 1]) + a)
        for a in (b"\x00\x00\x00", b"\x56\x34\x12", b"\x00\x40\x00", b"\xFF\xFF\xFF"):
            out.append(bytes([hi | 2]) + a)
        for code in range(256):
            m = code & 3
            if m == 0:
                out.append(bytes([hi | 3, code]))
            elif m == 1:
                for d in (b"\x05\x00", b"\x00\x01", b"\xFB\xFF", b"\x00\x80"):
                    out.append(bytes([hi | 3, code]) + d)
            out.append(bytes([hi | 4, code]))
            out.append(bytes([hi | 5, code]))
        for r in (0x00, 0xE0, 0xE4, 0xF0, 0xFC, 0x3C, 0x01):
            for x in (0x00, 0xE0, 0xE1, 0xE4, 0xF0, 0x3D, 0xE2):
                out.append(bytes([hi | 3, 0x03, r, x]))
                out.append(bytes([hi | 3, 0x07, r, x]))
    return out


def reg_heads():
    out = []
    for base in (0xC8, 0xD8, 0xE8):
        for r in range(8):
            out.append(bytes([base | r]))
        for code in range(256):
            out.append(bytes([base - 1, code]))
    return out


def space():
    """Yield byte strings covering every decodable shape."""
    for p in range(256):
        for t in TAILS:
            yield bytes([p]) + t
    for p in range(256):
        for b in range(256):
            yield bytes([p, b]) + TAILS[0]
    for h in mem_heads() + reg_heads():
        for op in range(256):
            for t in TAILS[:4]:
                yield h + bytes([op]) + t
