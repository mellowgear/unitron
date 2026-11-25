$maximum
	module	main
f_code	section	code
start:	ld	XWA,konst
	ret
f_const	section	romdata
konst:	dw	0x1234
f_data	section	data
var:	dsw	2
f_const	section	romdata
	extern	v1,v2,v3,v4,v5,v6,v7,v8,v9
	dl	v1,v2,v3,v4,v5,v6,v7,v8,v9
	end
