$maximum
	module	late
h	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"
	dl	start
	dw	0
	db	0,0x10
	db	"LATE        "
	dfb	16,0
c	section	code abs=0x200100
start:
	ld	XSP,0x6C00
	ld	XIX,table
	ld	XHL,(XIX+4)
	call	XHL
	call	tail
	jr	start
early:	ld	A,1
	ret
tail:	ld	B,2
	ret
table:	dl	early,late
	db	0xFF,0xFF,0xFF,0xFF
late:	ld	A,3
	calr	late2
	ret
late2:	inc	1,A
	ret
	db	0x21,0x20,0x0E
	db	0xFF,0xFF
lone_ptr:	dl	fake
	db	0xFF,0xFF
fake:	db	0x21,0x10,0x0E
	db	0xFF,0xFF,0xFF
	end
