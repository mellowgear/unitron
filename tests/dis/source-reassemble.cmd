asm source-reassemble.asm -o out.ngp --origin 0x200000 --size 0x200 --no-mpp
dis out.ngp -o out
asm out/main.asm -o out.back.ngp --origin 0x200000 --size 0x200
