$maximum
; code, data and romdata sections; relocatable or abs=
c	section	code abs=0x200000
	ld	A,(var)
	ld	XWA,k
r	section	romdata abs=0x200010
k	db	1,2,3
v	section	data abs=0x4000
var	dsb	1
	end
