$maximum
c	section	code abs=0x200000
	ldw	A,1
	ldl	(XHL),1
	ld	WA,A
	end
