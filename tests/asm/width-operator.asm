$maximum
	module	t
c	section	code abs=0x200000
	ld	A,(0x62AA:24)
	ld	C,(XHL+0xF:16)
	ld	BC,2:16
	cp	E,0:8
	ld	A,2:3
	ld	A,5
	ld	A,(XHL-2:16)
	jp	0x1234:24
	db	0x1FF:8
	ld	(0x20:16),A
	ldb	(0x20:16),1
	ld	XWA,5:32
	push	5:8
	ld	A,(XHL+0:8)
	jp	Z,0x10:24
	call	0x10:16
	ld	A,(lab:24)
lab:	nop
	end
