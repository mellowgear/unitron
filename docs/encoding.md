# Encoding policy

Where the datasheet allows several encodings for one instruction text, unitron picks one. The
rules follow asm900 as read statically from its tables (asm900-analysis/ASM900_STATIC.md, "STATIC";
asm900 was never run, D9), the datasheet size columns ("DS"), and the the reference ROM ROM where it
confirms them. The `opspace` test (tests/cpp/vectors.cpp, tests/vectors/opspace*) checks every
decodable opcode form against them.

1. **Legal forms only.** A form outside the datasheet size columns is never emitted, and the
   decoder flags it (`Insn.illegal`): `CP r,#3` and `CPL/NEG` are byte/word only, so `cp XHL,0`
   is `EB CF 00 00 00 00` [DS CP; STATIC 2.3].
2. **Shortest, then lowest bytes**, except as below.
3. **`ld r,r`: the prefix holds the source** (`LD R,r`, `C8+zz+r : 88+R`); `LD r,R` (98+R) only
   when the destination is not one of the 8 current-bank registers [STATIC 2.1]. the reference ROM agrees
   everywhere: the decomp source's 417 standard-register `ld r,r` (505 counting Q and banked
   registers) all assemble to the ROM's bytes with this rule, and so do the 404 that unitron's own
   trace finds. (The plan's "higher code in the prefix" was a misreading: the old assembler's
   default put the lower code in the prefix, and its 194 `{n}` markers were the cases where the
   source had the higher code.)
4. **Byte `ld R,#n` is `20+R`**, even for n = 0..7; `ld A,2:3` asks for `LD r,#3` (`C9 AA`). Word and
   long registers use `LD r,#3` when the value fits 3 bits (`ld WA,5` = `D8 AD`) [STATIC 2.4].
5. **Byte MUL/MULS/DIV/DIVS `rr,#`: the odd (low-byte) prefix code**: `mul WA,5` = `C9 08 05`
   [STATIC 2.2; the reference ROM 85/85].
6. **Absolute addresses** are 8/16/24 bits by unsigned value. A label in a relocatable section takes
   its section's width (small 8, medium 16, large 24), an extern its declared width, `expr:n` at
   least n bits [STATIC 2.5, 3.6, 3.7].
7. **Displacements**: `(r+d)` is 8-bit signed when it fits and the base is XWA..XSP, else 16-bit;
   `(r+0)` keeps its zero byte; `(r)` has none; `(XHL+0xF:16)` forces 16 bits [STATIC 2.6].
8. **Steps**: `(r+)`/`(-r)` step = operand size unless `:n` is written [STATIC 2.6].
9. **Branches** are encoded as written; only `j`/`cal` choose [TR p.16-17, p.29]. `jp label` is
   `1A` below 0x10000, else `1B`; `jp cc,label` uses the `B0+mem : D0+cc` form [STATIC 3.1].
   - `-O1` (the default, asm900's `-O1`): the shortest of jr/jrl/jp and of calr/call, when the
     distance is known before linking: an absolute target, or a label (± a constant) in the same
     section as the instruction, even a relocatable one, since its distance does not depend on
     where the section goes. Any other target (another relocatable section, an extern) takes
     jp/call.
   - `-O0`: always jp/call.
10. **Sizes iterate** to a fixpoint, as asm900's pass 2 does [STATIC 0].

Equal-length alternatives the opcode test accepts as policy, not error: the `LD r,R` form when
the destination is standard, the even MUL/DIV code, `ld R,#3` for a byte register, the two
memory-to-memory LD forms, and `jp/call (#8)` vs `#16`.

## Writing source from bytes

`unitron dis -o` (src/writer.cpp) writes each traced instruction with the operand spelling
above and checks that it assembles back to the same bytes. If not, it tries asm900's `:n` widths
(`ld (0x62AA:24),A`, `ld BC,2:16`, `cp E,0:8`, `ld C,(XHL+0xF:16)`). An encoding with no asm900
spelling at all, or an illegal form, is written as `db` with the instruction in the comment. For
the reference ROM: 29720 instructions, 45 with a width, no `db`.
