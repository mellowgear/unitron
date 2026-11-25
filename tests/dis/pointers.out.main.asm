; 'POINTERS': asm900 source written by `unitron dis` from the ROM.
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
	db	"POINTERS    "	; title
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; reserved (0)

c_200040	section	code abs=0x200040
start:
	ld	XSP,0x6C00
	ld	XIX,dat_200051
	ld	XHL,(XIX+4)
	call	XHL
	jr	start

d_200051	section	romdata abs=0x200051
dat_200051:
	dl	dat_20006B,code_200065,code_200068,SYS_LEVER
	dl	0x00123456

c_200065	section	code abs=0x200065
code_200065:
	ld	A,1
	ret
code_200068:
	ld	A,2
	ret

d_20006B	section	romdata abs=0x20006B
dat_20006B:
	db	0x48,0x49,0x00
	dfb	18,0xFF

	end
