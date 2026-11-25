# BUG?: a listing (no -o) is linear from the entry point or --start: it does not read --hints,
# not even a missing file
asm mini.asm -o out.ngp --origin 0x200000 --size 0x80 --no-mpp
dis out.ngp --hints nosuch.txt --start 0x200065 --count 2
