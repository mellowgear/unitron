$maximum
; a repeated name continues the section
c	section	code abs=0x200000
	db	1
r	section	code
	db	3
d	section	romdata abs=0x200010
	db	9
c	section	code abs=0x200000
	db	2
r	section	code
	db	4
	end
