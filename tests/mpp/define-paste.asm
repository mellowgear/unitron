; BUG: a ## b should paste the two tokens (12, lab_3); unitron writes a #"b" instead
#define CAT(a,b) a ## b
#define LBL(n) lab_ ## n
	db	CAT(1,2)
LBL(3):	nop
	jr	LBL(3)
