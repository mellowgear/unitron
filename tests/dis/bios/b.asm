$maximum
	module	b
h	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"
	dl	start
	dw	0
	db	0,0x10
	db	"BIOS        "
	dfb	16,0
c	section	code abs=0x200040
start:	ld	XSP,0x6C00
	ld	RW3,5
	call	syscall		; through a subroutine: commented
	ld	RW3,6
	call	plain		; not the shape: no comment
	ld	XHL,hidden
	jp	XHL		; hidden is found only by the sweep
hidden:	ld	RW3,7
	swi	1		; in swept code: commented
	jr	hidden
syscall:			; SNK SysCall's SYSTEM_CALL shape
	push	XIX
	ld	XIX,(0x4000)
	call	XIX
	pop	XIX
	ret
plain:	inc	1,A
	ret
	end
