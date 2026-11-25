$maximum
r	section	code
top:	nop
	j	near
	j	Z,near
	cal	near
	j	far
	cal	NC,near
	j	other
	cal	other
near:	ret
	dfb	200,0
far:	ret
o	section	code
other:	ret
c	section	code abs=0x210000
	j	0x210010
	cal	0x210010
	j	top
	end
