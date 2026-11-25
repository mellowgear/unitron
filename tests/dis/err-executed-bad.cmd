# BUG: a bad address in the --executed file is reported as 'error: stoll' (a C++ library
# message) with no file name or line; the hints file says 'FILE:LINE: bad hex address'
asm mini.asm -o out.ngp --origin 0x200000 --size 0x80 --no-mpp
dis out.ngp -o out --executed x.txt
