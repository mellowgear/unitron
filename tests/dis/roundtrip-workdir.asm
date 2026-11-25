$maximum
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
	ld	XWA,timer
	ld	(0x6FD4),XWA
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
	ld	RW3,0x12
	swi	1
	swi	1
	ld	(0x8002),A
	ldar	XIY,table
	ld	A,(XHL+A)
	ld	(0x8000),A
	ld	(0x87E2),A
	bit	3,(0x8010)
loop:	djnz	B,loop
	jr	loop
subr:	push	WA
	pop	WA
	ret
irq:	reti
timer:	ld	A,1
	ret	Z
	jr	timer
table:	db	1,2,3
	dl	hidden
	db	0xFF,0xFF
hidden:	ld	A,2
	call	hidden2
	ret
	db	0x55,0x66
hidden2:	ld	B,3
	ret
	db	0x50,0x51
	db	0x1F,0xFF,0xFF,0xFF
	db	0,0,0,0
	end
