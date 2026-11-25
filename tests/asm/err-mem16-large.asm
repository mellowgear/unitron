$maximum
l	section	data large
big	dsw	1
c	section	code abs=0x200000
	ldw	(big),(XHL)
	ldw	(XHL),(big)
	end
