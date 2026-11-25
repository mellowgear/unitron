$maximum
c	section	code abs=0x200000
; n units reserved: 0xFF in the image (or --fill)
	db	1
	dsb	2
	dsw	1
	dsp	1
	dsd	1
	dsl	1
	db	2
	end
