asm roundtrip-workdir.asm -o out.ngp --origin 0x200000 --size 0x200 --no-mpp
roundtrip out.ngp --workdir out
