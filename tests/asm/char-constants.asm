$maximum
c	section	code abs=0x200000
; 'x' constants: up to 4 bytes, big-endian; the TR p.10 escapes
	dl	'A','AB','ABC','ABCD'
	db	'\n','\t','\\','\'','\x7F','\101','\0'
	end
