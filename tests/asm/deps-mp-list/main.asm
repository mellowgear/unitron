$maximum
#include "macros.inc"
#if 0
#include "never.inc"
#endif
c	section	code abs=0x200000
	ld	A,V
	$include "data.inc"
	$include "build/gen.asm"
	end
	$include "after_end.inc"
