$maximum
	module	t
c	section	code abs=0x200000
	db	1,2,3,4
d	section	romdata abs=0x200002
	db	9
e	section	romdata abs=0x200001
	dw	5
	end
