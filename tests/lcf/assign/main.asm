$maximum
	module	main
f_code	section	code
start:	ld	XWA,konst
	ret
f_const	section	romdata
konst:	dw	0x1234
f_data	section	data
var:	dsw	2
	extern	base,twice,top
f_const	section	romdata
	dl	base,twice,top
	end
