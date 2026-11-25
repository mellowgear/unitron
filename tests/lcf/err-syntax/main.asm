$maximum
	module	main
	extern	large	__DataRAM
	public	start
	extern	medium	counter
vec	section	code abs=0x200000
	jp	start
	dl	__DataRAM
f_code	section	code
start:	ld	XWA,init
	ld	(counter),XWA
	ret
f_const	section	romdata
init:	dw	0x1234
f_data	section	data
iv:	dw	5
	end
