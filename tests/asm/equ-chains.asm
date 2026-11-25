$maximum
c	section	code abs=0x200000
A1	equ	B1+1
B1	equ	C1*2
C1	equ	lab
S1	equ	S2
S2	equ	S1
	dl	A1
	dl	S1
	dfb	K,0
K	equ	2
	dfb	A1&0,0
	org	lab2
	db	1
lab:	nop
lab2:	nop
	align	X1
X1	equ	4
	end
