$maximum
c	section	code abs=0x200000
	ld	A,(0x1000000)
	ld	A,(XHL+0x8000)
	ld	A,(0x1:32)
	end
