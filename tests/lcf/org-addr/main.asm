$maximum
	module	main
f_code	section	code
start:	ld	XWA,konst
	ret
f_const	section	romdata
konst:	dw	0x1234
f_data	section	data
var:	dsw	2
f_init	section	data
ival:	dw	5,6
	extern	__InitROM,__InitRAM,__InitLen
f_const	section	romdata
	dl	__InitROM,__InitRAM,__InitLen
	end
