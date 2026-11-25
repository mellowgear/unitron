$maximum
c	section	code abs=0x200000
	ei	A
	swi	A
	ldf	A
	retd	A
	ldc	A
	link	XWA
	scc	Z,(XHL)
	extz	(XHL)
	djnz	A,B,C
	end
