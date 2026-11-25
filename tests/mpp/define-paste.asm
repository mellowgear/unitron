#define CAT(a,b) a ## b
#define LBL(n) lab_ ## n
#define N 5
#define CATN(a) a ## N
#define AB x ## y
	db	CAT(1,2)
LBL(3):	nop
	jr	LBL(3)
	db	CAT(N,1)
	db	CATN(7)
	db	AB
