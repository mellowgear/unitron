$maximum
	module	t
c	section	code abs=0x200000
VEC	equ	0x6FCC
X	equ	0x6F82
	ld	(VEC),XHL
	ldb	(X),0x10
	ld	A,(XIZ+TASK)
	jp	XHL
	call	C,XIX+2
	lda	XHL,XHL+DE
TASK	equ	8
	ld	A,(fwd)
	ld	XWA,fwd
	jr	fwd
	jrl	fwd
	jp	fwd
	call	fwd
	calr	fwd
	djnz	fwd
	djnz	WA,fwd
	j	fwd
	j	Z,fwd
	cal	fwd
	cal	NZ,fwd
	j	0x10
	j	0x300000
	cal	0x1234
	ldar	XWA,fwd
	ldar	HL,fwd
fwd:	ret
	end
