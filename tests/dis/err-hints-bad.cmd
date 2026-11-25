asm tiny.asm -o out.ngp --origin 0x200000 --size 0x200 --no-mpp
dis out.ngp -o out --hints badhints.txt
