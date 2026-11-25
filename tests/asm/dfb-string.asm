$maximum
c	section	code abs=0x200000
; dfb with a string writes the string n times (TR p.21; asm900's dfb handler sizes it as
; length * n); a string longer than 511 bytes is still error 209
	dfb	3,"ab"
	dfb	1,"xyz"
	dfb	2,""
	dfb	2,'C'
	end
