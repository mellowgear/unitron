z80 asm p.z80 -o out.bin
z80 dis out.bin -o out
z80 asm out/main.z80 -o out.back.bin
