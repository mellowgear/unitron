# the hand-off into the main asm900 build: --equ writes the Z80 labels at their main-CPU address
# (Z80 address + 0x7000, or --equ-base), --db the image as bin2db's db lines; a main-CPU file
# includes both
z80 asm equ-base.z80 --equ out.inc --equ-base 0x8000
