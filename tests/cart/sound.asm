; sound.asm: unitron's test cartridge, module sound (the main CPU's side of the Z80 program).
; See main.asm for the build and sound.z80 for the Z80 side. sound.inc and sound.db are written by
; `unitron z80 asm sound.z80 --equ sound.inc --db sound.db`: the Z80 labels at their main-CPU
; address (0x7000 + n; SNK SysLib l.39), and the Z80 image as db lines.
;
; The Z80 interface addresses 0xB8 (stop/start), 0xBA (NMI) and 0xBC (command byte) are in no
; SNK document (docs/hardware-names.md, "Not named"). This is the sequence the NGPC sound drivers
; we have use (docs/Z80_PLAN.md 1.3), so they stay numbers.
$maximum
	module	sound
$include "ngpc_hw.inc"
$include "sound.inc"

	public	SoundInit,SoundCommand,SoundSetNoise

f_code	section	code
; SoundInit: stop the Z80, copy its program to Z80 RAM, start it
SoundInit:
	ldw	(0xB8),0xAAAA		; stop
	ld	XDE,Z80_RAM		; Z80 address 0000H
	ld	XHL,SoundImage
	; the length: the word at SndSize in the image (sound.z80 keeps its own length there)
	ld	BC,(SoundImage+SndSize-Z80_RAM)
	ldir	(XDE+),(XHL+)
	ldw	(0xB8),0x5555		; start: the Z80 begins at 0000H
	ret

; SoundCommand: A = the command byte (sound.z80, SndMain): write it, then raise the Z80's NMI
SoundCommand:
	ld	(0xBC),A		; an 8-bit address
	ldb	(0xBA),1
	ret

; SoundSetNoise: A = the noise setting, into the parameter block just past the Z80 image
SoundSetNoise:
	ld	(SndParams),A
	ret

f_const	section	romdata
SoundImage:
$include "sound.db"
	end
