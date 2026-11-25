"""Decoder against hand-assembled encodings and the datasheet's printed object codes."""
from unitron.cpu.decode import decode_bytes
from unitron.cpu.regs import ext_reg

BASE = 0x200000

CASES = [
    ("00", "nop"),
    ("21 12", "ld A,0x12"),
    ("33 34 12", "ld HL,0x1234"),
    ("3B", "push XHL"),
    ("44 00 00 20 00", "ld XIX,0x200000"),
    ("1B 56 34 12", "jp 0x123456"),
    ("1D 00 10 20", "call 0x201000"),
    ("66 FE", "jr Z,0x200000"),
    ("68 00", "jr 0x200002"),
    ("C9 03 7F", "ld A,0x7F"),
    ("DB 88", "ld WA,HL"),
    ("E9 C8 01 00 00 00", "add XBC,1"),
    ("83 21", "ld A,(XHL)"),
    ("8B 05 21", "ld A,(XHL+5)"),
    ("8B FB 21", "ld A,(XHL-5)"),
    ("8B 00 21", "ld A,(XHL+0)"),
    ("B3 41", "ld (XHL),A"),
    ("F1 34 12 50", "ld (0x1234),WA"),
    ("F2 00 40 00 61", "ld (0x4000),XBC"),
    ("C3 F1 10 00 21", "ld A,(XIX+0x10)"),
    ("C5 EC 21", "ld A,(XHL+)"),
    ("D4 EC 20", "ld WA,(-XHL:1)"),
    ("D4 ED 20", "ld WA,(-XHL)"),
    ("F5 EC 50", "ld (XHL+:1),WA"),
    ("F5 ED 50", "ld (XHL+),WA"),
    ("B0 F6", "ret Z"),
    ("B0 F8", "ret"),
    ("C7 E0 03 05", "ld A,5"),
    ("C7 E1 03 05", "ld W,5"),
    ("C7 10 03 05", "ld RA1,5"),
    ("F9", "swi 1"),
    ("C9 41", "mul WA,A"),
    ("CA 47", "mul HL,B"),
    ("DA 48", "muls XWA,DE"),
    ("83 11", "ldir (XDE+),(XHL+)"),
    ("95 11", "ldirw (XIX+),(XIY+)"),
    ("94 15", "cpirw WA,(XIX+)"),
    ("F5 EE 60", "ld (XHL+),XWA"),
    ("F5 F1 52", "ld (XIX+),DE"),
    ("BB 10 14 80 52", "ldb (XHL+0x10),(0x5280)"),
    ("CA 1C FD", "djnz B,0x200000"),
    ("CA 33 03", "bit 3,B"),
    ("B0 CB", "bit 3,(XWA)"),
    ("F0 82 B9", "set 1,(0x82)"),
    ("C1 34 12 3F 55", "cpb (0x1234),0x55"),
    ("D9 E8 04", "rlc 4,BC"),
    ("06 07", "di"),
    ("06 00", "ei 0"),
    ("07", "reti"),
    ("0E", "ret"),
    ("08 82 10", "ldb (0x82),0x10"),
    ("F7 00 20 00 01 00", "ldx (0x20),1"),
    ("B4 DE", "jp NZ,XIX"),
    ("B3 D8", "jp XHL"),
    ("BC 02 D7", "jp C,XIX+2"),
    ("B3 E8", "call XHL"),
    ("F3 07 E0 E4 33", "lda XHL,XWA+BC"),
    ("F3 03 E0 E4 33", "lda XHL,XWA+C"),
    ("F2 00 10 20 E8", "call 0x201000"),
    ("E9 2E 3C", "ldc XNSP,XBC"),
    ("D9 61", "inc 1,BC"),
    ("D9 68", "dec 8,BC"),
    ("C9 A8", "ld A,0"),
    ("C9 77", "scc C,A"),
    ("D8 39 FE 00", "minc2 0x100,WA"),
    ("16", "ex F,F'"),
    ("E8 0C F8 FF", "link XWA,-8"),
]

# Object codes printed in the TLCS-900 datasheet examples.
DATASHEET = [
    ("68 76", 0x2000, "jr 0x2078"),                # JR 2078H at 2000H
    ("F3 13 41 13 34", 0x200000, "ldar XIX,0x201345"),  # LDAR XIX,$+1345H
]


def dis(hexstr, addr=BASE):
    return decode_bytes(bytes.fromhex(hexstr), addr)


def norm(t):
    return " ".join(t.split())


def test_cases():
    for h, want in CASES:
        ins = dis(h)
        assert norm(ins.text()) == want, h
        assert ins.size == len(bytes.fromhex(h)), h
        assert not ins.invalid and not ins.illegal, h


def test_datasheet_examples():
    for h, addr, want in DATASHEET:
        assert norm(dis(h, addr).text()) == want


def test_flow():
    assert dis("68 00").ends_block
    assert not dis("66 00").ends_block
    i = dis("1D 00 10 20")
    assert i.is_call and i.target == 0x201000 and not i.ends_block


def test_ext_reg():
    assert ext_reg(0x00, 1) == "RA0"
    assert ext_reg(0x03, 1) == "QW0"
    assert ext_reg(0x3C, 4) == "XHL3"
    assert ext_reg(0xD0, 2) == "WA'"
    assert ext_reg(0xF2, 2) == "QIX"
    assert ext_reg(0xFD, 1) == "SPH"


def test_undefined_and_illegal():
    i = dis("50")
    assert i.invalid and i.size == 1
    assert dis("EB DB").illegal          # CP XHL,3 (#3 form is byte/word only)
    assert dis("E8 06").illegal          # CPL XWA
    assert dis("04").illegal             # MAX: 900 only
    assert dis("C9 E8 14").illegal       # shift count byte above 15
