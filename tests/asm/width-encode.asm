$maximum
c	section	code abs=0x200000
; expr:n also encodes at least n bits wide
	ld	A,(0x20)	; 8-bit address
	ld	A,(0x20:16)	; 16-bit
	ld	A,(0x20:24)	; 24-bit
	ld	A,(XHL+2)	; 8-bit displacement
	ld	A,(XHL+2:16)	; 16-bit
	ld	A,2		; 20+R
	ld	A,2:3		; LD r,#3
	ld	WA,5		; LD r,#3
	ld	WA,5:16		; LD r,#16
	end
