; system.inc: system work RAM, interrupt vectors, Z80 RAM (SNK SysPro, SysWork).
SYS_LEVER	equ	0x6F82	; SNK SysPro, SYSTEM INFORMATION; SysWork, SYSTEM WORK DEFINITION (Sys_Lever)
VEC_VBLANK	equ	0x6FCC	; SNK SysPro, SYSTEM INFORMATION (Vertical Blanking Interrupt)
VEC_TIMER0	equ	0x6FD4	; SNK SysPro, SYSTEM INFORMATION (Timer Interrupt (8 bit timer 0))
