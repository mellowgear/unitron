# the end of the image and the bytes just past it are written DriverEnd+k (memory operands, 16-bit
# immediates, a data word that holds the image size); a rewritten operand is label+k; other
# immediates and hardware addresses stay numbers
z80 asm end-listing.z80 -o out.bin
z80 dis out.bin
