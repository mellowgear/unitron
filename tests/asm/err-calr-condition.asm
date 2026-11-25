$maximum
c	section	code abs=0x200000
; CALR has no condition code (TR, DS: CALR $+3+d16 only)
lab:	calr	Z,lab
	end
