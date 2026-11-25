$maximum
c	section	code abs=0x200000
; BUG: docs/language.md says the dfb count takes no forward reference; a later equ is accepted
	dfb	N,0
N	equ	2
	end
