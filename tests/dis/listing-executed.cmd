# --executed addresses get their dyn_ labels in a listing too
asm mini.asm -o out.ngp --origin 0x200000 --size 0x80 --no-mpp
dis out.ngp --executed x.txt --start 0x200065 --count 2
