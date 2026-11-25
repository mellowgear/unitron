$maximum
	module	mini
h	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"
	dl	start
	dw	0
	db	0,0x10
	db	"MINI        "
	dfb	16,0
c	section	code abs=0x200040
start:	ld	XSP,0x6C00
	ld	XHL,vbl
	ld	(0x6FCC),XHL
	ld	XIX,table
loop:	ld	XHL,(XIX)
	call	XHL
	djnz	B,loop
	jr	start
vbl:	reti
table:	dl	hidden
	db	0xFF,0xFF,0xFF,0xFF
hidden:	ld	A,(0x6F82)
	ret
	db	0x55,0x66,0x77
spare:	ld	B,1
	ret
	end
