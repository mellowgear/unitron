$maximum
	extern	small	counter
	extern	sub1
c	section	code abs=0x300000
	ld	A,(counter)
	ld	A,(sub1)
	end
