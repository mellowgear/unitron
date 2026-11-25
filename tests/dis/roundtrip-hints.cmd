asm tiny.asm -o out.ngp --origin 0x200000 --size 0x200 --no-mpp
roundtrip out.ngp --hints hints.txt
