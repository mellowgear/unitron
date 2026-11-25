$maximum
	module	t
c	section	code abs=0x200000
	db	1,0x10,-1,255,"AB\n",'C'
	dw	0x1234,'AX'
	dp	0x123456
	dl	0x12345678,-1
	dfb	3,0xEE
	dfw	2,0x5883
	dsb	2
	db	0y101,0y11,010,0
	dd	7
	dfp	2,0x010203
	dfd	1,-2
	dfl	1,5
	dsw	1
	dsp	1
	dsd	1
	dsl	1
	db	"\x41\101\0\b\t\"\?\a\f\r\v\'\\"
	db	"é"
	dw	'ABCDE'
	end
