# a bad address in the --executed file: 'FILE:LINE: bad hex address', as in a hints file
asm mini.asm -o out.ngp --origin 0x200000 --size 0x80 --no-mpp
dis out.ngp -o out --executed x.txt
