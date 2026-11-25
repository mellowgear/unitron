$maximum
c	section	code abs=0x200000
	ld	(XHL),(XDE)
	add	A,(XHL),1
	inc	A
	end
