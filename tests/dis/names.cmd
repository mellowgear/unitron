asm prog.asm -o out.ngp --origin 0x200000 --size 0x100 --no-mpp --sym out.sym
# a name inside an instruction is not placed; Second is an extra label at Boot
dis out.ngp -o out --names n.txt
roundtrip out.ngp --names n.txt
