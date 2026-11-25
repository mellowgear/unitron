asm b.asm -o out.ngp --origin 0x200000 --size 0x100 --no-mpp
dis out.ngp -o out --addr
roundtrip out.ngp
