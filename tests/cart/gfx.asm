; gfx.asm: unitron's test cartridge, module gfx (the screen, the effects, the tiles and strings).
; See main.asm for the build. Two of its files are generated:
; - tiles.db: `unitron bin2db tiles.bin -o tiles.db`. tiles.bin is TILE_COUNT 8x8 tiles of 16
;   bytes, 2 bits per pixel (SNK K2GETechRef 4-3-5); the cart test writes it: byte k of tile n
;   is (n * 37 + k * 11) & 0xFF, and tile 0 (the space) is all zeros. A pattern, not a font.
; - strings.db: `unitron text charmap.txt strings.txt -o strings.db`: the strings as tile
;   numbers (charmap.txt), each ending in 0xFF.
$maximum
	module	gfx
#include "macros.inc"
$include "ngpc_hw.inc"

#define TILE_COUNT	37
#define TILE_BYTES	16

	public	VideoInit,DrawText,TitleText,PauseText,OverText
	public	FxNone,FxFlash,FxScroll
	extern	medium	Frame

f_code	section	code
; VideoInit: the window, the palettes and the tiles
VideoInit:
	; the window covers the whole 160x152 screen (SysPro l.290; K2GETechRef 4-5)
	ldb	(WBA_H),0
	ldb	(WBA_V),0
	ldb	(WSI_H),160
	ldb	(WSI_V),152
	copy_words	SCROLL1_PAL,Palette,4	; four 0x0BGR colors (K2GETechRef 4-15)
	copy_words	BG_PAL,Palette,1
	copy_bytes	CHARACTER_RAM,Tiles,TILE_COUNT*TILE_BYTES
	; clear scroll plane 1: 32 x 32 entries of one word (K2GETechRef 4-4-4)
	ld	XIZ,SCROLL1_VRAM
	ld	BC,32*32
	ld	WA,0
VideoInit.clear:
	ld	(XIZ+),WA
	dec	1,BC
	cp	BC,0
	jr	NZ,VideoInit.clear
	ret

; The effects, one per frame, called through main.asm's FxTable. They sit before DrawText, which
; is traced (main.asm calls it), so they are between traced code, where the gap sweep tries every
; candidate. (Past the last traced instruction it takes only call targets and pointers in a table
; that also points at known code: the golden case sweep_late tests that.)
FxNone:
	ret

; FxFlash: the background color steps through the blues with the frame count
FxFlash:
	ld	A,(Frame)
	srl	4,A
	and	A,0x0F
	ld	W,A
	ld	A,0
	ld	(BG_PAL),WA		; 0x0B00: blue only
	ret

; FxScroll: scroll plane 2 moves one pixel every second frame, using the table below
FxScroll:
	ld	A,(Frame)
	and	A,0x0E
	srl	1,A
	ld	XIX,ScrollSteps
	ld	C,(XIX+A)		; (register + 8-bit register) addressing
	ld	(S2SO_H),C
	ld	A,(XIX+8)		; (register + displacement): the vertical step
	ld	(S2SO_V),A
	ret

; DrawText: the string at XIX (tile numbers, 0xFF at the end) into the scroll plane at XIY
DrawText:
	ld	W,0
DrawText.next:
	ld	A,(XIX+)
	cp	A,0xFF
	ret	Z
	ld	(XIY+),WA		; map entry: the tile number, palette 0, no flip
	jr	DrawText.next

f_const	section	romdata
ScrollSteps:
	db	0,1,2,3,4,5,6,7		; horizontal
	db	0			; vertical
Palette:
	dw	0x0000,0x0FFF,0x0AAA,0x0555	; transparent/black, white, light and dark grey
Tiles:
$include "tiles.db"
$include "strings.db"
	end
