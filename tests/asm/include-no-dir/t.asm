$maximum
	module	t
c	section	code abs=0x200000
$include "data.asm"
	db	VAL
$include "sub/deep.asm"
$include <data2.asm>
	end
