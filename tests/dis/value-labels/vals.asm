$maximum
	module	vals
h	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"
	dl	start
	dw	0x1234
	db	1,0x10
	db	"VAL",0,0,0,0,0,0,0,0,0	; not plain text: written as db bytes
	dfb	16,0
c	section	code abs=0x200040
start:	ld	XSP,0x6C00
	ld	XHL,vbl			; code: its label
	ld	(0x6FCC),XHL
	ld	XIX,table		; data: a dat_ label
	add	XIX,table+2		; inside a data block: a dat_ label there
	ld	A,(table+1)		; a memory operand in the ROM: a dat_ label
	lda	XIZ,table+3
	ld	XHL,0x200020		; a header field's start: a dat_ label
	ld	XHL,0x200021		; inside a header field: a number
	ld	XWA,start+1		; inside an instruction: a number
	ld	XIY,0x8200		; SPRITE_PAL
	ld	XDE,0x9000		; SCROLL1_VRAM
	ld	XBC,0x20		; an I/O address (TRUN): a number
	ld	WA,0x8200		; a 16-bit register: a number
	cp	XDE,0x9000		; a hardware name only for ld: a number
	call	fn
	jr	start
vbl:	reti
fn:	ret
table:	db	1,2,3,4,5,6
	end
