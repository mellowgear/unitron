$maximum
c	section	code abs=0x200000
	db	1,2
d	section	romdata abs=0x200010
	db	3
v	section	data abs=0x4000
	db	9
w	section	data
	dsb	4
	db	7
	end
