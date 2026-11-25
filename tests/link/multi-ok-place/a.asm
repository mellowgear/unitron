$maximum
	module	main
	extern	large	sub1
	extern	medium	counter
	extern	small	tiny
	public	start
c	section	code abs=0x200000
start:
	call	sub1
	ld	A,(counter)
	ld	A,(tiny)
	ld	XWA,sub1
	dl	sub1,counter
	jp	start
	end
