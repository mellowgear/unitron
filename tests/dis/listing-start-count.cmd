asm listing-start-count.asm -o out.ngp --origin 0x200000 --size 0x200 --no-mpp
dis out.ngp --start 0x200100 --count 8
