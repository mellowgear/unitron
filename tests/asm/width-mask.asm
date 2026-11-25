$maximum
c	section	code abs=0x200000
; expr:n masks the value to n bits
	db	0x1FF:8,0x1FF:4,-1:3
	dw	0x12345:16
	end
