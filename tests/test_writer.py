"""dis -> asm on a small hand-made ROM (no game ROM needed)."""
from unitron.asm.assembler import Assembler, build_image
from unitron.ngpc.rom import Rom
from unitron.ngpc.writer import disassemble_rom
from unitron.ngpc import hwnames
from unitron.asm.assembler import is_reserved
from helpers import asm

PROGRAM = r"""$maximum
	module	tiny
h	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"
	dl	start
	dw	0
	db	0,0x10
	db	"TINY        "
	dfb	16,0
c	section	code abs=0x200100
start:
	di
	ld	XSP,0x6C00
	ld	XHL,irq
	ld	(0x6FCC),XHL
	ld	A,(0x6F82)
	ld	(0x62AA:24),A
	ld	BC,2:16
	ld	C,(XHL+0xF:16)
	cp	E,0:8
	ld	WA,BC
	mul	WA,5
	cp	XHL,0
	ld	(XIX+),DE
	ld	WA,(-XHL:1)
	lda	XHL,XHL+DE
	calr	subr
	call	subr
	jp	C,XIX+2
loop:	djnz	B,loop
	jr	loop
subr:	push	WA
	pop	WA
	ret
irq:	reti
	db	1,2,3
	end
"""


def test_tiny_roundtrip(tmp_path):
    a, img = asm(PROGRAM, origin=0x200000, size=0x200)
    assert a.d.errors == []
    rom = Rom(img)
    st = disassemble_rom(rom, tmp_path / "src")
    b = Assembler()
    b.add_module(tmp_path / "src/main.asm")
    chunks = b.assemble()
    assert b.d.errors == []
    out, _, _ = build_image(chunks, 0x200000, len(img))
    assert out == img
    text = (tmp_path / "src/main.asm").read_text()
    assert "(0x62AA:24)" in text and "BC,2:16" in text and "E,0:8" in text
    assert "(VEC_VBLANK)" in text and "(SYS_LEVER)" in text
    assert "jp\tC,XIX+2" in text and "(-XHL:1)" in text
    assert st["width_marked"] == 4 and st["db_fallback"] == 0


def test_hw_names_not_reserved():
    bad = [n for _, n, *_ in hwnames.NAMES if is_reserved(n)]
    assert bad == []


def test_hw_inc_assembles():
    from pathlib import Path
    inc = Path(__file__).resolve().parents[1] / "unitron/data/ngpc_hw.inc"
    a, img = asm(f'$maximum\n$include "{inc}"\nc\tsection\tcode abs=0x200000\n\tld\t(WBA_H),A\n\tend\n',
                 origin=0x200000)
    assert a.d.errors == [] and img == bytes.fromhex("f1 02 80 41")
