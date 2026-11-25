; 'BIOS': asm900 source written by `unitron dis` from the ROM.
; Build: unitron asm main.asm -o out.ngp --origin 0x200000 --size 0x100
$maximum
	module	game
$include "hw.inc"


d_200000	section	romdata abs=0x200000
; the cartridge header (SNK SysPro, SOFTWARE CASSETTE RECOGNITION HEADER INFORMATION FORMAT)
	db	" LICENSED BY SNK CORPORATION"	; @200000 recognition code
	dl	start	; @20001C startup address
	dw	0x0000	; @200020 ID code (BCD)
	db	0x00	; @200022 ID sub-code (version)
	db	0x10	; @200023 compatible system (0x00 monochrome, 0x10 color)
	db	"BIOS        "	; @200024 title
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; @200030 reserved (0)

c_200040	section	code abs=0x200040
start:
	ld	XSP,0x6C00	; @200040
	ld	RW3,5	; @200045
	call	sub_200060	; @200048 BIOS call 0x05 (by subroutine)
	ld	RW3,6	; @20004C
	call	sub_200069	; @20004F
	ld	XHL,start.1	; @200053
	jp	XHL	; @200058
start.1:
	ld	RW3,7	; @20005A
	swi	1	; @20005D BIOS call 0x07
	jr	start.1	; @20005E
sub_200060:
	push	XIX	; @200060
	ld	XIX,(0x4000)	; @200061
	call	XIX	; @200065
	pop	XIX	; @200067
	ret	; @200068
sub_200069:
	inc	1,A	; @200069
	ret	; @20006B

d_20006C	section	romdata abs=0x20006C
	dfb	148,0xFF	; @20006C

	end
