$maximum
c	section	code abs=0x200000
; org in an abs section: an absolute address
	db	1
	org	0x200008
	db	2
	end
