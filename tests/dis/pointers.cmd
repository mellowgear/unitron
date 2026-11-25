asm ptr.asm -o out.ngp --origin 0x200000 --size 0x80 --no-mpp
dis out.ngp -o out --hints h.txt --no-sweep
dis out.ngp --start 0x200040 --count 12 --hints h.txt
roundtrip out.ngp --hints h.txt
