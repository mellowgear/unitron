# Toshiba's sample programs SAMPLE1.ASM and SAMPLE2.ASM (samples/, copied unchanged from
# Toshiba's TLCS-900 toolchain, T900/SAMPLE/ASM900): linked together at the addresses below, with a
# symbol file, then each assembled alone without linking.
asm SAMPLE1.ASM SAMPLE2.ASM -o out.bin --origin 0x8000 --size 0x500 --place MAIN=0x8100 --place TIME30=0x8200 --place subrout=0x8300 --place ROM_DAT=0x8400 --sym out.sym
asm SAMPLE1.ASM --no-link
asm SAMPLE2.ASM --no-link
