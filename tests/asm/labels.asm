$maximum
c	section	code abs=0x200000
; a label at column 0 may omit the colon; an indented one needs it
first	nop
second:	nop
	third:	nop
	fourth	db	1
Routine.loop:	jr	Routine.loop
	end
