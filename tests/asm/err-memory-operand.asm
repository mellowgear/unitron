$maximum
c	section	code abs=0x200000
	ld	A,()
	ld	A,(XHL*2)
	ld	A,(XHL+XDE)
	ld	A,(BC)
	end
