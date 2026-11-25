$maximum
c	section	code abs=0x200000
; each operator once (values are 32-bit signed)
	dl	-5,~0,+7
	dl	6*7,45/7,45%7,-45/7,-45%7
	dl	3+4,3-4
	dl	1<<4,0x100>>4,-16>>2
	dl	0xF0&0x3C,0xF0^0x3C,0xF0|0x0F
	dl	sizeof(c),startof(c)
	end
