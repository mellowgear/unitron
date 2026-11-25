$maximum
c	section	code abs=0x200000
; hex 0x, binary 0y, octal (leading 0), decimal; the location counter $
	db	0x1F,0XAB,0y101,0Y11,017,0,9,255
	dw	$&0xFFFF,1234
	end
