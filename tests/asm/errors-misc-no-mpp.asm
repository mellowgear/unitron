$maximum
	module	t
c	section	code abs=0x200000
	ld	A,(nowhere)
foo:
foo:
	ld	(XHL),(XDE)
	add	A,(XHL),1
	db	256
	jr	far
	dfb	200,0
far:
	db	-129
	dw	0x10000
	dp	0x1000000
	dl	0x100000000
	db	"abc
	dw	"ab"
	db
	db	1,,2
	dfb	1
	dfb	1,"ab"
	dfb	x,1
	dsb	y
	dsb	0
	dsb	-3
	equ	5
9bad:
bad-label:
small:
LD:
xwa	equ	1
Xwa	equ	2
	section	code
s2	section	bogus
s3	section	code small
s4	section	data small abs=0x10
s5	section	code foo
s6	section	data abs=nosuch
	align	3
	align	nosuch2
	align	0
	org	0x100
	public	9x
	extern	medium	a,9b
	fdatas	1
	file	"x"
	ld	A,
	ld	A,B,C
	frob	A
	nop	1
	normal
	max
	ld	A,(XHL+)) 
	ld	A,(XHL+:3)
	ld	A,(BC)
	ld	A,(XHL+XDE)
	ld	A,(XHL-A)
	jp	(XHL)
	jp	(0x1234)
	ld	A,0b101
	ld	A,12H
	ld	A,08
	ld	A,'
	ld	A,''
	ld	A,"x"
	ld	A,1:0
	ld	A,1:33
	ld	A,1:x
	ld	A,1/0
	ld	A,1%0
	ld	A,1<<-1
	ld	A,sizeof(nosect)
	ld	A,sizeof(1)
	ld	A,(1
	ld	A,1)
	ld	A,$$
	ld	A,'\q'
	ld	A,'\x'
	ld	A,'\777'
	ld	A,"\
	db	"\400"
	ld	A,0x100
	ld	WA,0x10000
	ld	A,-129
	ld	A,(0x1000000)
	ld	A,(-1)
	ld	A,(XHL+0x8000)
	ld	A,(XHL+0x10000:24)
	ld	A,(0x1234:8)
	ld	A,(0x123456:16)
	ld	A,(0x1:32)
	ldw	A,1
	ldl	(XHL),1
	ld	WA,A
	ld	RA3,QW
	cp	XHL,(XHL+5)
	ldx	(0x100),1
	ldx	A,1
	ldf	8
	ei	8
	swi	8
	retd	0x10000
	link	WA,1
	link	XWA,0x10000
	unlk	WA
	inc	9,A
	inc	0,A
	inc	A
	sll	17,A
	sll	0,A
	bit	16,A
	bit	8,(XHL)
	bit	A,(XHL)
	andcf	A,(XHL+)
	mul	A,B
	mul	WA,XBC
	mul	XWA,(XHL)
	mul	IX,A
	mul	QWA,A
	div	WA,0x100
	minc1	1,A
	minc1	A,1
	scc	Z,XWA
	scc	A,Z
	ldc	DMAS0,WA
	ldc	A,B
	ldc	A,DMAM0
	ex	F,A
	ex	A,WA
	ex	RA3,QW
	ex	(XHL),XWA
	push	XNSP
	pushl	5
	pop	5
	pop	F'
	push	(XHL+)
	lda	A,(XHL)
	lda	WA,XHL
	lda	RA3,XHL
	ldar	XWA,(XHL)
	ldar	A,0
	ldi	(XHL+),(XDE+)
	ldi	(XDE+),(XHL+:1)
	lddr	(XDE+),(XHL+)
	cpi	B,(XHL+)
	cpi	A,(XHL-)
	cpi	A,(QWA+)
	cpd	A,(XHL-)
	cpir
	ldirw
	ldir	1
	rld	B,(XHL)
	rld	A
	bs1f	A,B
	bs1f	B,WA
	daa	WA
	neg	XWA
	extz	A
	ret	1
	ret	Z,1
	calr	Z,0
	djnz	XWA,0
	djnz	A,B,C
	jr	XHL
	jp	Z
	jp	Z,1,2
	call	0x1000000
	j	XHL
	ld	(XHL),RA3
	ld	RA3,(XHL)
	add	RA3,QW
	add	A,WA
	ld	A,(SP)
	ld	A,(XHL+nosym3)
	ldb	(0x10000),(0x20000)
	ldb	(XHL),(XDE)
	ldw	(nosym4),(XHL)
	ldb	(0x1234),(0x5678)
	ldb	(XHL+),(0x5678)
	tset	3,(XHL)
	end
	db	1
	db	2
