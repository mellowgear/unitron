$maximum
; a small data section must fit below 0x100
s	section	data small
	dsb	0x200
	end
