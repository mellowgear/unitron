; 'TINY': asm900 source written by `unitron dis` from the ROM.
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
	db	"TINY        "	; title
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; reserved (0)
	dfb	192,0xFF

c_200100	section	code abs=0x200100
start:
	di
	ld	XSP,0x6C00
	ld	XHL,irq_vblank
	ld	(VEC_VBLANK),XHL
	ld	XWA,irq_timer0
	ld	(VEC_TIMER0),XWA
	ld	A,(SYS_LEVER)
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
	calr	sub_200172
	call	sub_200172
	jp	C,XIX+2
	ld	RW3,0x12
	swi	1	; BIOS call 0x12
	swi	1	; BIOS call 0x12
	ld	(WBA_H),A
	ldar	XIY,dat_20017C
	ld	A,(XHL+A)
	ld	(0x8000),A
	ld	(0x87E2),A
	bit	3,(STATUS_2D)
start.1:
	djnz	B,start.1
	jr	start.1
sub_200172:
	push	WA
	pop	WA
	ret
irq_vblank:
	reti
irq_timer0:
	ld	A,1
	ret	Z
	jr	irq_timer0

d_20017C	section	romdata abs=0x20017C
dat_20017C:
	db	0x01,0x02,0x03,0x85

c_200180	section	code abs=0x200180
dyn_200180:
	db	0x01	; normal (900 minimum/maximum mode switch; undefined on 900/H)
	ld	W,0
	swi	7
	swi	7
	ld	A,2
	call	sub_20018E
	ret

d_20018C	section	romdata abs=0x20018C
	db	0x55,0x66

c_20018E	section	code abs=0x20018E
sub_20018E:
	ld	B,3
	ret

d_200191	section	romdata abs=0x200191
	db	0x50,0x51,0x1F,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF

c_2001A0	section	code abs=0x2001A0
dyn_2001A0:
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7
	swi	7

	end
