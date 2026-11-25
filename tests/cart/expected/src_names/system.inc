; system.inc: system work RAM, interrupt vectors, Z80 RAM (SNK SysPro, SysWork).
SYS_LEVER	equ	0x6F82	; SNK SysPro, SYSTEM INFORMATION; SysWork, SYSTEM WORK DEFINITION (Sys_Lever)
VEC_SWI3	equ	0x6FB8	; SNK SysPro, SYSTEM INFORMATION (Software Interrupt (SWI 3))
VEC_VBLANK	equ	0x6FCC	; SNK SysPro, SYSTEM INFORMATION (Vertical Blanking Interrupt)
