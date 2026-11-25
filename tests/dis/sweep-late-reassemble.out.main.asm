; 'LATE': asm900 source written by `unitron dis` from the ROM.
; Build: unitron asm main.asm -o out.ngp --origin 0x200000 --size 0x200
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
	db	"LATE        "	; title
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; reserved (0)
	dfb	192,0xFF

c_200100	section	code abs=0x200100
start:
	ld	XSP,0x6C00
	ld	XIX,dat_20011B
	ld	XHL,(XIX+4)
	call	XHL
	call	sub_200118
	jr	start
swp_200115:
	ld	A,1
	ret
sub_200118:
	ld	B,2
	ret

d_20011B	section	romdata abs=0x20011B
dat_20011B:
	db	0x15,0x01,0x20,0x00,0x27,0x01,0x20,0x00,0xFF,0xFF,0xFF,0xFF

c_200127	section	code abs=0x200127
swp_200127:
	ld	A,3
	calr	sub_20012D
	ret
sub_20012D:
	inc	1,A
	ret

d_200130	section	romdata abs=0x200130
	db	0x21,0x20,0x0E,0xFF,0xFF,0x3B,0x01,0x20,0x00,0xFF,0xFF,0x21,0x10,0x0E
	dfb	194,0xFF

	end
