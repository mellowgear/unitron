$maximum
c	section	code abs=0x200000
; 8, 16, 24, 32 and 32 bits, little-endian; db takes strings (no 0 added)
	db	1,-1,"ab"
	dw	0x1234,-2
	dp	0x123456
	dd	0x12345678
	dl	0x9ABCDEF0
	end
