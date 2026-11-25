; 'MINI': asm900 source written by `unitron dis` from the ROM.
; Build: unitron asm main.asm -o out.ngp --origin 0x200000 --size 0x80
$maximum
	module	game
$include "hw.inc"


d_200000	section	romdata abs=0x200000
; the cartridge header (SNK SysPro, SOFTWARE CASSETTE RECOGNITION HEADER INFORMATION FORMAT)
	db	" LICENSED BY SNK CORPORATION"	; recognition code
	dl	start	; startup address
	dw	0x0000	; ID code (BCD)
	db	0x00	; ID sub-code (version)
	db	0x10	; compatible system (0x00 monochrome, 0x10 color)
	db	"MINI        "	; title
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; reserved (0)

c_200040	section	code abs=0x200040
start:
	ld	XSP,0x6C00
	ld	XHL,irq_vblank
	ld	(VEC_VBLANK),XHL
	ld	XIX,dat_20005D
start.1:
	ld	XHL,(XIX)
	call	XHL
	djnz	B,start.1
	jr	start
irq_vblank:
	reti

d_20005D	section	romdata abs=0x20005D
dat_20005D:
	db	0x65,0x00,0x20,0x00,0xFF,0xFF,0xFF,0xFF

c_200065	section	code abs=0x200065
dyn_200065:
	ld	A,(SYS_LEVER)
	ret

d_20006A	section	romdata abs=0x20006A
	db	0x55,0x66,0x77

c_20006D	section	code abs=0x20006D
dyn_20006D:
	ld	B,1
	ret

d_200070	section	romdata abs=0x200070
	dfb	16,0xFF

	end
