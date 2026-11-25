$maximum
c	section	code abs=0x200000
; equ: absolute, may use labels and later equ names
TWO	equ	2
FOUR	equ	TWO*2
	db	TWO,FOUR,LATER
LATER	equ	FOUR+1
here:	db	here&0xFF
ADDR	equ	here+1
	dl	ADDR
	end
