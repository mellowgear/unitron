#define YES
#ifdef YES
	db	"ifdef yes"
#endif
#ifdef NO
	db	"ifdef no"
#endif
#ifndef NO
	db	"ifndef no"
#endif
#if defined(YES) && !defined NO
	db	"defined"
#endif
