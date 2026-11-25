"""Toshiba's SAMPLE1.ASM/SAMPLE2.ASM assemble without errors; bytes checked by hand against the
datasheet instruction lists (Toshiba's own output can't be produced: we never run ASM900)."""
from pathlib import Path

import pytest

from unitron.asm.assembler import Assembler, build_image

SAMPLES = Path(__file__).resolve().parents[2] / "ngpc-project-template/ngpcbins/T900/SAMPLE/ASM900"
needs = pytest.mark.skipif(not (SAMPLES / "SAMPLE1.ASM").is_file(), reason="Toshiba samples not present")


def run(files, **kw):
    a = Assembler()
    for f in files:
        a.add_module(SAMPLES / f)
    chunks = a.assemble(**kw)
    return a, chunks


@needs
@pytest.mark.parametrize("f", ["SAMPLE1.ASM", "SAMPLE2.ASM"])
def test_each_module_checks(f):
    a, _ = run([f], link=False)
    assert a.d.errors == []


@needs
def test_linked():
    a, chunks = run(["SAMPLE1.ASM", "SAMPLE2.ASM"], placements={"MAIN": 0x8100, "TIME30": 0x8200,
                                                               "subrout": 0x8300, "ROM_DAT": 0x8400})
    assert a.d.errors == []
    img, origin, size = build_image(chunks, 0x8000, 0x500)
    s = a.symbols()

    def at(addr, hexstr):
        b = bytes.fromhex(hexstr)
        assert img[addr - origin:addr - origin + len(b)] == b, hex(addr)

    at(0x8000, "47 80 04 00 00 1A 00 81")       # ld XSP,0x480 (LD R,#: 40+R); jp #16
    at(0x8010, "1A 00 81")
    at(0x8020, "07")                             # reti
    tc, tm = s["tclear"], s["timer"]
    at(0x8100, "1D" + tc.to_bytes(3, "little").hex()     # call tclear (extern large: #24)
       + "0A 08 A2 01"                            # ldw (P23CR),P23INIT: LD<W> (#8),#
       + "21 01"                                  # ldb a,BIT_SET: LD R,#
       + "F0 06 41"                               # ldb (PORT2),a: LD (mem),R with (#8)
       + "C0 07 21"                               # ldb a,(PORT3)
       + "20 00"                                  # ldb w,0
       + "F0 30 50"                               # ldw (TREG4),wa
       + "D7 E2 A8"                               # ldw qwa,CLEAR: LD r,#3 with extended QWA
       + "F0 34 60"                               # ldl (CAP12),xwa
       + "1D" + tm.to_bytes(3, "little").hex()
       + "68 E9")                                 # jr read
    at(0x8200, "31 FF 1F D8 A9 D8 80 D9 1C FB 0E")    # ld bc,#16; ld wa,1 (LD r,#3); add; djnz; ret
    at(0x8400, "00 04 10 00 58 41 45 72 72 6F 72 00")   # dw 1024,0x10,'AX'; db "Error",0
    assert s["counter"] < 0x10000              # medium data section placed below 64K
    cnt = s["counter"]
    at(0x8300, "44" + cnt.to_bytes(4, "little").hex() + "DA A8 F5 F1 52 F5 F1 52 0E")
