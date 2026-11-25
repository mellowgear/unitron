; SYNTAX TEST "source.asm900" "asm900 constructs"
$maximum
; <------- keyword.control.directive.asm900
$charmap "text/charmap.txt"	; the game's codes
; <-------- keyword.control.directive.asm900
;        ^^^^^^^^^^^^^^^^^^ string.quoted.other.include.asm900
;                           ^^^^^^^^^^^^^^^^^^ comment.line.semicolon.asm900
$include "sound.db"
; <-------- keyword.control.directive.asm900
#include "constants/hardware.inc"
; <-------- keyword.control.directive.include.asm900
;        ^^^^^^^^^^^^^^^^^^^^^^^^ string.quoted.other.include.asm900
#define TASK_FUNC 4
; <------- keyword.control.directive.define.asm900
;       ^^^^^^^^^ entity.name.function.preprocessor.asm900
	module	game
;^^^^^^ storage.type.directive.asm900
TASK_SIZE	equ	0x20
; <--------- entity.name.constant.asm900
;        	^^^ storage.type.directive.asm900
;        	   	^^^^ constant.numeric.hex.asm900
c_200100	section	code abs=0x200100
; <-------- entity.name.constant.asm900
;       	^^^^^^^ storage.type.directive.asm900
;       	        ^^^^ storage.modifier.asm900
;       	             ^^^ storage.modifier.asm900
;       	                 ^^^^^^^^ constant.numeric.hex.asm900
?macro set_priority value
; <------ keyword.control.macro.asm900
;      ^^^^^^^^^^^^ entity.name.function.macro.asm900
	ldb	(PRIORITY),?value
;^^^ keyword.operator.mnemonic.asm900
;   	           ^^^^^^ variable.parameter.macro.asm900
?endm
; <----- keyword.control.macro.asm900
Reset:
; <----- entity.name.function.label.asm900
;    ^ punctuation.separator.label.asm900
Reset.loop:	ld	XWA,(XIZ+TASK_FUNC)
; <---------- entity.name.function.label.asm900
;          	^^ keyword.operator.mnemonic.asm900
;          	  	^^^ variable.language.register.asm900
;          	  	     ^^^ variable.language.register.asm900
;          	  	         ^^^^^^^^^ variable.other.symbol.asm900
.ch0:	ex	WA,WA'
; <---- entity.name.function.label.asm900
;    	  	^^ variable.language.register.asm900
;    	  	   ^^^ variable.language.register.asm900
	ldw	(XIZ+TASK_FUNC),Reset & 0xFFFF
;^^^ keyword.operator.mnemonic.asm900
;   	                      ^ keyword.operator.asm900
;   	                        ^^^^^^ constant.numeric.hex.asm900
	ld	(wQueue:24),A
;  	       ^ keyword.operator.width.asm900
;  	        ^^ constant.numeric.width.asm900
	jr	NZ,Reset.loop
;  	^^ support.constant.condition.asm900
;  	   ^^^^^^^^^^ variable.other.symbol.asm900
	ldirw	(XDE+),(XHL+)
;^^^^^ keyword.operator.mnemonic.asm900
	ld	A,0FFH
;  	  ^^^^ constant.numeric.suffixed.asm900
	ld	A,0y1010
;  	  ^^^^^^ constant.numeric.binary.asm900
	ldc	DMAS0,XWA
;   	^^^^^ variable.language.control-register.asm900
	jp	$+4
;  	^ variable.language.location.asm900
	db	"It""s", 'x'
;^^ storage.type.directive.asm900
;  	^^^^^^^ string.quoted.double.asm900
;  	   ^^ constant.character.escape.asm900
;  	         ^^^ string.quoted.single.asm900
	text	"Hello, <PLAYER>."	; a string
;^^^^ keyword.other.text.asm900
;    	        ^^^^^^^^ constant.character.escape.asm900
    line ""
;   ^^^^ keyword.other.text.asm900
	done
;^^^^ keyword.other.text.asm900
	slot	7 FF
;^^^^ keyword.other.text.asm900
	dw	sizeof(c_200100), startof(c_200100)
;  	^^^^^^ storage.modifier.asm900
;  	                  ^^^^^^^ storage.modifier.asm900
/* a block
   comment */
; <-------------- comment.block.asm900
	nop	// a line comment
;   	^^^^^^^^^^^^^^^^^ comment.line.double-slash.asm900
	end
;^^^ storage.type.directive.asm900
	set_priority	0x80, 2:16
;^^^^^^^^^^^^ entity.name.function.macro.call.asm900
;            	    ^ punctuation.separator.comma.asm900
;            	       ^ keyword.operator.width.asm900
	db	"unterminated
	nop
;^^^ keyword.operator.mnemonic.asm900
