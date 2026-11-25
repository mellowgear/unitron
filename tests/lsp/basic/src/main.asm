$maximum
; LIMIT: the largest count
LIMIT	equ	0x10
c	section	code abs=0x200000
#include "src/sub.asm"
Start:
	ld	A,LIMIT
	call	Helper
Start.loop:
	jr	Start.loop
	end
