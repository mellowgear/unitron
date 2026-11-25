$maximum
	module	ptr
h	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"
	dl	start
	dw	0
	db	0,0x10
	db	"POINTERS    "
	dfb	16,0
c	section	code abs=0x200040
start:	ld	XSP,0x6C00
	ld	XIX,table
	ld	XHL,(XIX+4)
	call	XHL			; the trace can't follow this
	jr	start
table:	dl	text,fa,fb		; a data address and two routines
	dl	0x6F82			; SYS_LEVER
	dl	0x123456		; outside the ROM: a number
fa:	ld	A,1
	ret
fb:	ld	A,2
	ret
text:	db	"HI",0
	end
