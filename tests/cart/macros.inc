; macros.inc: the cartridge's macros, for Toshiba's macro preprocessor (docs/language.md, "Macro
; preprocessor"). Hand-written sources join with #include, so these work in every module.

; Constants and one-line helpers (#define, like C's)
#define WDT_CLEAR	0x4E			/* the watchdog clear code (SNK SysPro l.312) */
#define STACK_TOP	0x6C00			/* the end of the user work RAM (SysPro l.194) */
#define MAP_ROW(r)	((r) * 64)		/* a scroll plane is 32 entries of 2 bytes per row */
#define MAP_AT(x, y)	(MAP_ROW(y) + (x) * 2)

; Statement macros (?macro ... ?endm). ?name is a parameter; a `labels` name gets a new number
; in each expansion (again000, again001, ...), so a macro can hold a loop.

; kick the watchdog: the system resets the cartridge if this isn't done about every 100 ms
?macro watchdog
	ldb	(WDCR),WDT_CLEAR
?endm

; wait until the VBlank handler has counted a new frame
?macro wait_frame
labels again
	ld	A,(LastFrame)
?again:
	cp	A,(Frame)
	jr	Z,?again
	ld	A,(Frame)
	ld	(LastFrame),A
?endm

; copy count bytes from src to dst (both addresses)
?macro copy_bytes dst,src,count
	ld	XDE,?dst
	ld	XHL,?src
	ld	BC,?count
	ldir	(XDE+),(XHL+)
?endm

; the same for words: count is in words
?macro copy_words dst,src,count
	ld	XDE,?dst
	ld	XHL,?src
	ld	BC,?count
	ldirw	(XDE+),(XHL+)
?endm
