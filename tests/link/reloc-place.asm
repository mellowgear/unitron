$maximum
m	section	data medium
v	dsb	1
l	section	data large
vl	dsb	1
s	section	data small
vs	dsb	2
c	section	code abs=0x200000
	ld	A,(v)
	ld	A,(vl)
	ld	A,(vs)
	ld	XWA,v
	ld	XWA,vl
	jp	r1
	call	r2
	jp	Z,r1
	ld	A,(v+1:24)
	dw	sizeof(m),startof(l)&0xFFFF,sizeof(r)
r	section	code medium
r1:	ret
r	section	code
r2:	ret
q	section	romdata align=4
	db	1
q2	section	romdata align=16,2
	db	2
	end
