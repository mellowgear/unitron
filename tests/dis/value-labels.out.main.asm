; 'VAL': asm900 source written by `unitron dis` from the ROM.
; Build: unitron asm main.asm -o out.ngp --origin 0x200000 --size 0x100
$maximum
	module	game
$include "hw.inc"


d_200000	section	romdata abs=0x200000
; the cartridge header (SNK SysPro, SOFTWARE CASSETTE RECOGNITION HEADER INFORMATION FORMAT)
	db	" LICENSED BY SNK CORPORATION"	; recognition code
	dl	start	; startup address
dat_200020:
	dw	0x1234	; ID code (BCD)
	db	0x01	; ID sub-code (version)
	db	0x10	; compatible system (0x00 monochrome, 0x10 color)
	db	0x56,0x41,0x4C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; title
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; reserved (0)

c_200040	section	code abs=0x200040
start:
	ld	XSP,0x6C00
	ld	XHL,irq_vblank
	ld	(VEC_VBLANK),XHL
	ld	XIX,dat_200092
	add	XIX,dat_200094
	ld	A,(dat_200093)
	lda	XIZ,dat_200095
	ld	XHL,dat_200020
	ld	XHL,0x200021
	ld	XWA,0x200041
	ld	XIY,SPRITE_PAL
	ld	XDE,SCROLL1_VRAM
	ld	XBC,0x20
	ld	WA,0x8200
	cp	XDE,0x9000
	call	sub_200091
	jr	start
irq_vblank:
	reti
sub_200091:
	ret

d_200092	section	romdata abs=0x200092
dat_200092:
	db	0x01
dat_200093:
	db	0x02
dat_200094:
	db	0x03
dat_200095:
	db	0x04,0x05,0x06
	dfb	104,0xFF

	end
