$maximum
c	section	code abs=0x200000
; with -H: H hex, B binary, O/Q octal, D decimal suffixes (as asm900's -H)
	db	0FFH,0ffh,1010B,17O,17Q,99D,12d
	db	0x10,0y1,010,10
	end
