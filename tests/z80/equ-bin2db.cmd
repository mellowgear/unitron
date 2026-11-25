# the hand-off into the main asm900 build: --equ writes the Z80 labels at their main-CPU address
# (Z80 address + 0x7000, or --equ-base), --db the image as bin2db's db lines; a main-CPU file
# includes both
z80 asm equ-bin2db.z80 -o out.bin --equ out.inc --db out.db
bin2db out.bin -o out.drv2.db
