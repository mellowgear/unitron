; 'NAMES': asm900 source written by `unitron dis` from the ROM.
; Build: unitron asm main.asm -o out.ngp --origin 0x200000 --size 0x100
$maximum
	module	game
$include "hw.inc"
$include "names.inc"


d_200000	section	romdata abs=0x200000
; the cartridge header (SNK SysPro, SOFTWARE CASSETTE RECOGNITION HEADER INFORMATION FORMAT)
	db	" LICENSED BY SNK CORPORATION"	; recognition code
	dl	Boot	; startup address
	dw	0x0000	; ID code (BCD)
	db	0x00	; ID sub-code (version)
	db	0x10	; compatible system (0x00 monochrome, 0x10 color)
	db	"NAMES       "	; title
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; reserved (0)

c_200040	section	code abs=0x200040
Boot:
Second:
	ld	XSP,0x6C00
	ldb	(Count),0
Boot.loop:
	ld	A,(Count)
	inc	1,A
	ld	(Count),A
	cp	A,0x10
	jr	C,Boot.loop
	ld	XHL,Count
	ld	XIX,Table
	call	Mid
	jr	Boot
Mid:
	ld	A,1
	ret

d_20006C	section	romdata abs=0x20006C
Table:
	db	0x01,0x02,0x03
	dfb	145,0xFF

	end
