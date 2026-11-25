$maximum
	module	sub
	public	sub1,counter,tiny,missing
	public	start
d	section	data medium
counter	dsw	1
z	section	data small
tiny	dsb	1
r	section	code
sub1:	ret
start:	nop
	end
