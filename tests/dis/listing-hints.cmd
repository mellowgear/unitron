asm mini.asm -o out.ngp --origin 0x200000 --size 0x80 --no-mpp
dis out.ngp --hints h.txt --start 0x20005C --count 8
