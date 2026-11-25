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
	extern	hex,bin,oct,dec,big
	dl	hex,bin,oct,dec,big
	end
