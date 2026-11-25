$maximum
	module	main
$include "drv.inc"
c	section	code abs=0x200000
Start:	ld	A,(Tick_seen)
	ld	(Cmd),A
	ld	XHL,Z80Driver
	ld	BC,Stack-Reset
	jr	Start
Z80Driver:
$include "drv.db"
	end
