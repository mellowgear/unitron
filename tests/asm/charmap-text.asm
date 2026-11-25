$maximum
c	section	romdata abs=0x200000
; $charmap (a unitron extension): the map's commands assemble to character codes
$charmap "charmap-text.map"
Hello:
	text "ab ba"	; a comment
	line "<P>a"
	done
; slot WIDTH PAD: each string padded to WIDTH bytes
	slot	4 EE
	text "a"
	done
	text "ab"
	done
	slot	0
	db	0x11
	end
