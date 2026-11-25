$maximum
#define loop 1
#define Routine 2
#define tail 3
c	section	code abs=0x200000
Routine.loop:	nop
	jr	Routine.loop
	db	loop,Routine,tail
.tail:	nop
	jr	.tail
	end
