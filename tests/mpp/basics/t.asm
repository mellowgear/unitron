$maximum
#include "c.inc"
#define TWICE(x) ((x)*2)
#if N > 1 && defined(N)
	db	TWICE(N)
#else
	db	0
#endif
	db	1 // dropped
/* block
 comment */	db	2 ; kept
	ex	F,F' ; it's fine
#define STR(a) #a
#define CAT(a,b) a ## b
	db	STR(hello)
	db	CAT(1,2)
#define LONG 1 + \
 2
	db	LONG
#ifdef N
	db	"ifdef"
#endif
#ifndef N
	db	"ifndef"
#elif 1
	db	"elif"
#endif
#if 0
#if 1
	db	"nested"
#endif
#elif N == 3
	db	"elif2"
#else
	db	"else"
#endif
#undef N
#ifdef N
	db	"still"
#endif
	db	N
#if __900__ && __TOSHIBA__ && !__FALSE__ && __TRUE__
	db	"builtin"
#endif
#if (1+2)*3 == 9 && 7/2 == 3 && 7%3 == 1 && (1<<4) == 16 && (16>>2) == 4 && (5&3) == 1 && (5|2) == 7 && (5^1) == 4 && ~0 == -1 && -1 < 0 && 2 >= 2 && 1 <= 2 && 3 > 2 && 1 != 2 && (0 || 1) && 0x10 == 16 && 010 == 8 && 0y11 == 3 && UNDEF == 0
	db	"cexpr"
#endif
#define F0() 9
	db	F0()
#define NEST(x) TWICE(x)+1
	db	NEST(2)
	db	TWICE (3)
	db	TWICE
	db	"TWICE(1)"
	db	'N'
	db	2N
#line 100
	end
