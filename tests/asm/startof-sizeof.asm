$maximum
; sizeof(sect) and startof(sect) in an expression and an equ
c	section	code abs=0x200000
	dw	sizeof(r),startof(r)&0xFFFF
SZ	equ	sizeof(c)
	db	SZ
r	section	romdata abs=0x200010
	db	1,2,3
	end
