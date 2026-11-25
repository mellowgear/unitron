;*********************************************
;   SUBROUTINE PROGRAM FOR TLCS-900    No.2
;*********************************************
;
$MAXIMUM                            ; (62)
;
        module  sample2             ; (63)
;
CLEAR   equ     0                   ; (64)
;
        public  timer,tclear        ; (65)
        extern  medium counter      ; (66)
        extern  large  time_30      ; (67)
;
;  ---------------------------------
WORK    section data large          ; (68)
dma_dat dsb     512                 ; (69)
wrk_dat dsw     0x100*4             ; (70)
;
;  ---------------------------------
DATA_SC section data medium         ; (71)
adr_tbl dsl     0x20                ; (72)
;
;  ---------------------------------
subrout section code large          ; (73)
tclear:                             ; (74)
        ldl     xix,counter         ; (75)
        ldw     de,CLEAR            ; (76)
        ldw     (xix+),de           ; (77)
        ldw     (xix+),de           ; (78)
        ret                         ; (79)
;
timer:                              ; (80)
        call    time_30             ; (81)
timer_counter1:                     ; (82)
        ldl     xix,wrk_dat         ; (83)
loop1:  adcw    (xix+),1            ; (84)
        adcw    (xix+),0            ; (85)
        adcw    (xix+),0            ; (86)
        adcw    (xix+),0            ; (87)
        jr      z,loop1             ; (88)
        ret                         ; (89)
;
        end                         ; (90)

