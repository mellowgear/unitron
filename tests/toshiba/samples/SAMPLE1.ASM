;*****************************************
;   SAMPLE PROGRAM FOR TLCS-900    No.1
;*****************************************
;
$MAXIMUM                            ; (1)
        module  sample1             ; (2)
;
CLEAR   equ     0                   ; (3)
WATCH   equ     0xA2                ; (4)
BIT_SET equ     1                   ; (5)
P23INIT equ     (BIT_SET<<8)+WATCH  ; (6)
STACK_P equ     0x480               ; (7)
;
        public  counter,time_30     ; (8)
        extern  large timer,tclear  ; (9)
;
;  ---------------------------------
PORT    section data abs=0          ; (10)
        dsb     6                   ; (11)
PORT2 	dsb     1                   ; (12)
PORT3 	dsb     1                   ; (13)
P23CR:                              ; (14)
P2CR    dsb     1                   ; (15)
P3CR    dsb     1                   ; (16)
P2FC    dsb     1                   ; (17)
P3FC    dsb     1                   ; (18)
        org     0x30                ; (19)
TREG4   dsw     1                   ; (20)
TREG5   dsw     1                   ; (21)
CAP12   dsl     1                   ; (22)
;
;  ---------------------------------
DATA_SC section data medium         ; (23)
counter dsw     2                   ; (24)
count   dsb     0x10                ; (25)
count_f dsb     1                   ; (26)
	align   2                   ; (27)
cntad   dsl     1                   ; (28)
;
;  ---------------------------------
ROM_DAT section romdata large       ; (29)
rom_dat dw      1024,0x10,'AX'      ; (30)
msg     db      "Error",0           ; (31)
table   dl      initial,tclear,timer; (32)
;
;  ---------------------------------
INT_TBL section code abs=0x8000     ; (33)
swi0:   ldl     xsp,STACK_P         ; (34)
        jp      initial             ; (35)
                                    ; (36)
        org     0x8010              ; (37)
swi1:   jp      initial             ; (38)
        org     0x8020              ; (39)
        reti                        ; (40)
;
;  ---------------------------------
MAIN    section code medium         ; (41)
initial:                            ; (42)
        call    tclear              ; (43)
;
        ldw     (P23CR),P23INIT     ; (44)
;
store:  ldb     a,BIT_SET           ; (45)
read:   ldb     (PORT2),a           ; (46)
        ldb     a,(PORT3)           ; (47)
        ldb     w,0                 ; (48)
        ldw     (TREG4),wa          ; (49)
        ldw     qwa,CLEAR           ; (50)
        ldl     (CAP12),xwa         ; (51)
        call    timer               ; (52)
        jr      read                ; (53)
;
;  ---------------------------------
TIME30  section code large          ; (54)
time_30:                            ; (55)
        ld      bc,0x1fff           ; (56)
        ld      wa,1                ; (57)
loop1:  add     wa,wa               ; (58)
        djnz    bc,loop1            ; (59)
        ret                         ; (60)

        end                         ; (61)
