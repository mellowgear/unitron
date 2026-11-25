$maximum
	module	prog
h	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"
	dl	Boot
	dw	0
	db	0,0x10
	db	"NAMES       "
	dfb	16,0
Count	equ	0x4000		; RAM: an equ in names.inc
Limit	equ	0x10		; a small number: not used for immediates
c	section	code abs=0x200040
Boot:	ld	XSP,0x6C00
	ldb	(Count),0
Boot.loop:
	ld	A,(Count)
	inc	1,A
	ld	(Count),A
	cp	A,Limit
	jr	C,Boot.loop
	ld	XHL,Count
	ld	XIX,Table
	call	Mid
	jr	Boot
Mid:	ld	A,1		; the names file also names Mid+1 (inside this instruction)
	ret
Table:	db	1,2,3
	end
