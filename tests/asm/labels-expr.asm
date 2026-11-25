$maximum
	module	t
c	section	code abs=0x200000
Start:
	dw	Later-Start,(1+2)*3,1+2*3,1<<4|1,6&3^1,0x100>>4,-2%3,~0&0xFF
Later	db	$&0xFF
Routine.loop:	jr	Routine.loop
COUNT	equ	Later+0x50
	dl	COUNT
	dl	-7/2,7/-2,-7%2,7%-2,1<<40,-1>>3,0x80000000>>4,0xFFFFFFFF+2,-0x80000000-1
	dl	0x7FFFFFFF*3,+5,- -5,~~5,'\x01\x02',(0x12345678:16),0x1FF:8
	dl	1<<31,1<<32,1<<63,1<<64,8>>64,-8>>100
	dw	sizeof(c),startof(c)&0xFFFF
	end
