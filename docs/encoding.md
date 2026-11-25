# Encoding policy

Where the TLCS-900 datasheet allows several encodings for one instruction text, unitron picks one.
This file lists the rules and why. They follow Toshiba's asm900.exe, read statically from its
tables and code (never run), where the manual is silent; the size columns of the TLCS-900/L1 CPU
datasheet's instruction lists ("DS"); and a commercial reference ROM where it confirms them. The
`opspace` test (tests/cpp/vectors.cpp, tests/vectors/opspace*) checks every decodable opcode form
against them, and `tests/datasheet/tlcs900.txt` cites these rule numbers.

1. **Legal forms only.** A form outside the DS size columns is never emitted, and the decoder
   flags it (`Insn.illegal`): `CP r,#3` and `CPL/NEG` are byte/word only, so `cp XHL,0` is
   `EB CF 00 00 00 00` [DS CP; asm900 picks this 6-byte form itself].
2. **Shortest, then lowest bytes**, except as below.
3. **`ld r,r`: the prefix holds the source** (`LD R,r`, `C8+zz+r : 88+R`); `LD r,R` (98+R) only
   when the destination is not one of the 8 current-bank registers [asm900]. The reference ROM
   agrees everywhere: in a full disassembly of it, all 417 standard-register `ld r,r` (505
   counting Q and banked registers) assemble to the ROM's bytes with this rule, and so do the 404
   that unitron's own trace finds.
4. **Byte `ld R,#n` is `20+R`**, even for n = 0..7; `ld A,2:3` asks for `LD r,#3` (`C9 AA`). Word
   and long registers use `LD r,#3` when the value fits 3 bits (`ld WA,5` = `D8 AD`) [asm900].
   A non-standard register also takes `LD r,#3` when the value fits (`ld RA3,5` = `C7 30 AD`):
   asm900 names the long form `C7 rr 03 #` for them without saying whether `LD r,#3` is tried first, and all
   83 `ld` of 0-7 into a non-standard register in the reference ROM (74 byte: `QA..QD`,
   `RA3..RC3`; 9 word/long: `QW`, `RW3`, `RBC3`) use `LD r,#3`.
5. **Byte MUL/MULS/DIV/DIVS `rr,#`: the odd (low-byte) prefix code**: `mul WA,5` = `C9 08 05`
   [asm900; the reference ROM 85/85].
6. **Absolute addresses** are 8/16/24 bits by unsigned value. A label in a relocatable section
   takes its section's width (small 8, medium 16, large 24), an extern its declared width, `expr:n`
   at least n bits [asm900]. A label in an abs section always gets its exact width:
   `section ... large abs=` does not make references 24-bit.
7. **Displacements**: `(r+d)` is 8-bit signed when it fits and the base is XWA..XSP, else 16-bit;
   `(r+0)` keeps its zero byte; `(r)` has none; `(XHL+0xF:16)` forces 16 bits [asm900].
8. **Steps**: `(r+)`/`(-r)` step = operand size unless `:n` is written [asm900].
9. **Branches** are encoded as written; only `j`/`cal` choose [TR p.16-17, p.29]. `jp label` is
   `1A` below 0x10000, else `1B`; `jp cc,label` uses the `B0+mem : D0+cc` form [asm900].
   - `-O1` (the default, asm900's `-O1`): the shortest of jr/jrl/jp and of calr/call, when the
     distance is known before linking: an absolute target, or a label (± a constant) in the same
     section as the instruction, even a relocatable one, since its distance does not depend on
     where the section goes. Any other target (another relocatable section, an extern) takes
     jp/call.
   - `-O0`: always jp/call.
10. **Sizes iterate** to a fixpoint, as asm900's pass 2 does (it re-encodes until no size
    changes). asm900 starts from the shortest sizes and always converges on the shortest width
    for the final value; it never leaves a long form because a value was unknown in pass 1. So a
    longer-than-needed form in a ROM (the reference ROM has six outside its 24-bit addresses, such
    as `ld BC,2:16` and `cp E,0:8`) needs a written `:n`.

TR is the Toshiba *TLCS-900 Assembler Reference* (tde122-01_96), printed pages.

Equal-length alternatives the opcode test accepts as policy, not error: the `LD r,R` form when
the destination is standard, the even MUL/DIV code, `ld R,#3` for a byte register, the two
memory-to-memory LD forms, and `jp/call (#8)` vs `#16`.

An encoding a ROM uses that asm900's rules don't give is not a unitron option: write it as `db`
with the instruction in a comment, or as a macro in the project's own macros file (e.g.
`ld_alt B,A` expanding to the original bytes). The writer emits `db` for it.

## Writing source from bytes

`unitron dis -o` (src/writer.cpp) writes each traced instruction with the operand spelling of
`language.md` and checks that it assembles back to the same bytes. If not, it tries asm900's `:n`
widths (`ld (0x62AA:24),A`, `ld BC,2:16`, `cp E,0:8`, `ld C,(XHL+0xF:16)`). An encoding with no
asm900 spelling at all, or an illegal form, is written as `db` with the instruction in the
comment. For the reference ROM: 29720 instructions, 45 with a width, no `db`.
