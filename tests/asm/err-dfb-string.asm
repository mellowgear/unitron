$maximum
c	section	code abs=0x200000
; BUG: the value is a 2-byte string, which should be E231 "dfb takes one value";
; unitron reports an internal error instead
	dfb	1,"ab"
	end
