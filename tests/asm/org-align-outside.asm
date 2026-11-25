$maximum
	module	t
c	section	code abs=0x200000
	db	1
	align	4
	db	2
	org	0x200010
	db	3
	align	3
	db	4
d	section	romdata abs=0x200020
	db	4
	align	8
	org	0x200030
	dw	5
	end
