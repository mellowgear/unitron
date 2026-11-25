$maximum
c	section	code abs=0x200000
; a dfb or ds* count may not use a symbol defined later (TR p.21-22, error 223; asm900 checks it
; for the df* and ds* directives)
	dfb	N,0
	dsb	N
K	equ	1
	dfb	K,0
N	equ	2
	end
