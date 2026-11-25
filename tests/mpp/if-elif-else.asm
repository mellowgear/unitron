#define N 2
#if N == 1
	db	"one"
#elif N == 2
	db	"two"
#elif N == 3
	db	"three"
#else
	db	"other"
#endif
#if 0
	db	"no"
#else
	db	"else"
#endif
