$maximum
?macro task_wait
labels resume
?resume:
	ld	XWA,?resume
	ld	(XIZ+8),XWA
	cpb	(XIZ+12),0
	ret	NZ
?endm
?macro put a,b
	ld	?a,?b
?endm
?macro twice x
	put	?x,1
	put	?x,2
?endm
?MACRO upper
	nop
?ENDM
c	section	code abs=0x200000
	task_wait
here:	task_wait
	put A,5 ; c
	put (XHL),B
	put	A
	twice	C
	upper
put	nop
	task_wait	; again
	end
