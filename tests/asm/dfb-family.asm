$maximum
c	section	code abs=0x200000
; n copies of a value
	dfb	3,0xAA
	dfw	2,0x1234
	dfp	2,0x123456
	dfd	1,0x12345678
	dfl	1,-1
	end
