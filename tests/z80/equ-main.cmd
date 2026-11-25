# the hand-off into the main asm900 build: --equ writes the Z80 labels at their main-CPU address
# (Z80 address + 0x7000, or --equ-base), --db the image as bin2db's db lines; a main-CPU file
# includes both
z80 asm drv.z80 -o out.bin --equ drv.inc --db drv.db
asm main.asm -o out.main.bin
