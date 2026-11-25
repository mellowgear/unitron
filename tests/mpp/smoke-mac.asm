$maximum
#define V 7
c	section	code abs=0x200000
	ld	A,V
	ld	(XHL),A
	end
