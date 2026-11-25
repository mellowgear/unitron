$maximum
v	section	data abs=0x4000
	dsb	2
	db	1,2
	dw	3
w	section	data
	dfb	2,0
c	section	code abs=0x200000
	ld	A,(0x4000)
	end
