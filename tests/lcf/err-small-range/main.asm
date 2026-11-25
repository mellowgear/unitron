$maximum
	module	main
f_code	section	code
start:	ld	XWA,konst
	ret
f_const	section	romdata
konst:	dw	0x1234
f_data	section	data
var:	dsw	2
f_zp	section	data small
zv:	dsb	1
	end
