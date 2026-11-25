; main.asm: unitron's test cartridge, module main (header, start-up, main loop, interrupts).
; The cartridge is ours, written for the tests (tests/cpp/cart.cpp) and as an example program;
; hardware facts come from SNK's documents (cited) and the names from unitron's data/ngpc_hw.inc.
; It has never run on hardware or an emulator: it only has to be real code for unitron's tools.
;
; Build (what the cart test runs, in a directory with the generated files):
;   unitron z80 asm sound.z80 -o sound.bin --equ sound.inc --db sound.db
;   unitron bin2db tiles.bin -o tiles.db          (tiles.bin: see gfx.asm)
;   unitron text charmap.txt strings.txt -o strings.db
;   unitron asm main.asm sound.asm gfx.asm --lcf cart.lcf -I DATA -o cart.ngp
;               --origin 0x200000 --size 0x8000
; DATA is unitron's data/ directory (ngpc_hw.inc). cart.lcf places the relocatable sections.
;
; What the program does: a title screen, a "play" state that moves a value with the joypad and
; runs one of three effects each frame, a pause and a game-over state; each state change sends a
; command to the Z80 program (sound.z80).
$maximum
	module	main
#include "macros.inc"
$include "ngpc_hw.inc"

	public	Frame,LastFrame
	extern	large	SoundInit,SoundCommand,SoundSetNoise
	extern	large	VideoInit,DrawText,TitleText,PauseText,OverText
	extern	large	FxNone,FxFlash,FxScroll

; ---------------------------------------------------------------- cartridge header
; The software cassette recognition header, 0x200000-0x20003F (SNK SysPro, SOFTWARE CASSETTE
; RECOGNITION HEADER INFORMATION FORMAT)
f_head	section	romdata abs=0x200000
	db	" LICENSED BY SNK CORPORATION"	; recognition code, third-party use: 28 bytes
	dl	Start				; startup address, little endian
	dw	0x0000				; cassette ID, BCD; 0 = provisional development ID
	db	0x00				; ID sub-code (version)
	db	0x10				; compatible system: color
	db	"UNITRON CART"			; title, 12 ASCII bytes
	dfb	16,0				; reserved, write 0

; ---------------------------------------------------------------- RAM
; The user work RAM is 0x4000-0x6BFF (SysPro, WORK RAM AREA). f_vars is a medium section (16-bit
; addresses); f_buf is large (the default), so its addresses are written with 24 bits even
; though they are below 0x10000: `unitron dis` has to write them back as (0x....:24).
f_vars	section	data medium
Frame:		dsb	1	; counted by VBlank
LastFrame:	dsb	1	; the frame the main loop last saw
State:		dsb	1	; 0 title, 1 play, 2 pause, 3 game over
Keys:		dsb	1	; SYS_LEVER this frame
Pressed:	dsb	1	; keys that went down this frame
Fx:		dsb	1	; the current effect, 0-2
PosX:		dsw	1	; moved by left/right
Score:		dsw	1	; BCD
Saved:		dsl	2	; what Swi3 saw in XWA and XWA3

f_buf	section	data
Shadow:		dsw	32	; a copy of one row of the scroll plane

; ---------------------------------------------------------------- start-up
; SysPro, STEPS AFTER DEBUGER STSARTUP: the program starts with interrupts disabled; set the vectors (0x6FB8-), then
; enable them with EI 0.
f_code	section	code
Start:
	ld	XSP,STACK_TOP
	watchdog
	cal	ClearRam
	; XWA of register banks 0-2 starts at zero; bank 3 is the system's (SysPro, REGISTER USE RESTRICTIONS)
	ld	XWA,0
	ld	XWA0,XWA
	ld	XWA1,XWA
	ld	XWA2,XWA
	ld	XHL,VBlank
	ld	(VEC_VBLANK),XHL	; SysPro, SYSTEM INFORMATION; the trace follows this install
	ld	XWA,Swi3
	ld	(VEC_SWI3),XWA		; SysPro, SYSTEM INFORMATION
	ei	0
	cal	SoundInit		; another module: cal becomes call (24-bit)
	cal	VideoInit
	ld	XIX,TitleText
	ld	XIY,SCROLL1_VRAM+MAP_AT(5,8)
	call	DrawText
	ldb	(State),0

; ---------------------------------------------------------------- main loop
; One pass per frame. The state handlers are reached only through the jump table, by jp XHL:
; the trace can't follow that, so the gap sweep (or hints.txt) finds them.
Main:
	wait_frame
	watchdog
	calr	ReadKeys
	ld	A,(State)
	and	A,3
	extz	WA
	sll	2,WA			; 4 bytes per jp
	ld	XHL,JumpTable
	lda	XHL,XHL+WA
	jp	XHL

JumpTable:
	jp	StateTitle
	jp	StatePlay
	jp	StatePause
	jp	StateOver

; ---------------------------------------------------------------- states
StateTitle:
	bit	SYS_LEVER_A,(Pressed)
	jr	Z,StateTitle.done
	ldb	(State),1
	ld	A,0x06			; command 2 (tone), value 1
	call	SoundCommand
StateTitle.done:
	jrl	Main			; a 16-bit relative jump

StatePlay:
	ld	A,(Keys)
	bit	SYS_LEVER_LEFT,A
	jr	Z,StatePlay.right
	ld	HL,(PosX)
	dec	1,HL
	ld	(PosX),HL
StatePlay.right:
	bit	SYS_LEVER_RIGHT,A
	jr	Z,StatePlay.fx
	ld	HL,(PosX)
	inc	1,HL
	ld	(PosX),HL
	cal	AddScore
StatePlay.fx:
	bit	SYS_LEVER_B,(Pressed)	; B: next effect
	jr	Z,StatePlay.run
	ld	A,(Fx)
	inc	1,A
	cp	A,3
	jr	ULT,StatePlay.keep
	ld	A,0
StatePlay.keep:
	ld	(Fx),A
	push	A
	call	SoundSetNoise
	pop	A
StatePlay.run:
	; the effects are reached only through the pointer table FxTable (call XHL)
	ld	A,(Fx)
	extz	WA
	sll	2,WA			; 4 bytes per pointer
	ld	XIX,FxTable
	ld	XHL,(XIX+WA)
	call	XHL
	bit	SYS_LEVER_OPTION,(Pressed)
	j	Z,Main			; j picks the shortest jump
	ldb	(State),2
	ld	A,0x07			; command 3 (noise)
	call	SoundCommand
	ld	XIX,PauseText
	ld	XIY,SCROLL1_VRAM+MAP_AT(7,10)
	call	DrawText
	jp	Main			; a 24-bit jump

StatePause:
	bit	SYS_LEVER_OPTION,(Pressed)
	jr	Z,StatePause.done
	ldb	(State),1
	ld	A,0x05			; command 1 (stop)
	call	SoundCommand
StatePause.done:
	ld	WA,(Score)
	cp	WA,0x9999
	j	NZ,Main
	ldb	(State),3		; a full score ends the game

StateOver:
	; keep the score in register bank 2 while the text is drawn (SysPro, REGISTER USE RESTRICTIONS: bank 3 is the
	; system's), then put it back
	ld	WA,(Score)
	ld	RWA2,WA
	ld	XIX,OverText
	ld	XIY,SCROLL1_VRAM+MAP_AT(5,10)
	call	DrawText
	ld	WA,RWA2
	ld	(Shadow),WA		; Shadow is in a large section: a 24-bit address
	ld	A,(Pressed)
	and	A,1<<SYS_LEVER_A
	j	Z,Main
	jp	Start			; start over

; ---------------------------------------------------------------- helpers
; ReadKeys: Keys = the joypad, Pressed = the keys that were up last frame and are down now
ReadKeys:
	ld	A,(SYS_LEVER)		; SysPro, SYSTEM INFORMATION
	ld	B,(Keys)
	ld	(Keys),A
	cpl	B
	and	A,B
	ld	(Pressed),A
	ret

; AddScore: Score += 1 in BCD, low byte then high byte
AddScore:
	ld	A,(Score)
	add	A,1
	daa	A
	ld	(Score),A
	ret	NC
	ld	A,(Score+1)
	adc	A,0
	daa	A
	ld	(Score+1),A
	ret

; ClearRam: zero the variables (f_vars and f_buf), with a word loop and djnz
ClearRam:
	ld	XHL,startof(f_vars)
	ld	BC,sizeof(f_vars)
	ld	A,0
ClearRam.loop:
	ld	(XHL+),A
	djnz	BC,ClearRam.loop
	ld	XHL,Shadow
	ld	B,32
	ld	WA,0
ClearRam.words:
	ld	(XHL+),WA
	djnz	B,ClearRam.words
	ret

; ---------------------------------------------------------------- interrupts
; VBlank (SysPro, SYSTEM INFORMATION): count frames. incf switches to the next register bank, so the main
; loop's registers stay as they were and need no push/pop; decf switches back.
VBlank:
	incf
	ld	A,(Frame)
	inc	1,A
	ld	(Frame),A
	ld	A,(PosX)
	ld	(S1SO_H),A		; scroll plane 1 follows PosX
	decf
	reti

; Swi3 (SysPro, SYSTEM INFORMATION): a debugging aid, swi 3 from anywhere saves XWA, and the system bank's
; XWA3, which it only reads
Swi3:
	ld	(Saved),XWA
	ld	XWA,XWA3
	ld	(Saved+4),XWA
	reti

; An unreachable data block that happens to decode as code: 21 10 / 22 20 / 23 30 / 0E is
; "ld A,0x10 / ld B,0x20 / ld C,0x30 / ret". Nothing executes it; it follows a reti, so the gap
; sweep takes it for code. hints.txt marks it as data.
StartLevels:
	db	0x21,0x10,0x22,0x20,0x23,0x30,0x0E

; ---------------------------------------------------------------- constant data
; In f_const, which cart.lcf puts between this module's code and the other modules' code.
f_const	section	romdata
FxTable:			; the effect routines (gfx.asm and below), called through call XHL
	dl	FxNone,FxFlash,FxScroll
	end
