# a listing reads --hints too, so a missing file is an error
asm mini.asm -o out.ngp --origin 0x200000 --size 0x80 --no-mpp
dis out.ngp --hints nosuch.txt --start 0x200065 --count 2
