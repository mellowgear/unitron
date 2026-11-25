$maximum
c	section	code abs=0x200000
; a backslash right before the closing quote escapes it: the string has no end
	db	"ab\"
	db	1
	end
