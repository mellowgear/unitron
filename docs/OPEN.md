# OPEN points

What is not settled, with what unitron does meanwhile. "Plan" = ASM900_PLAN.md (O1-O10 are its
numbers); "STATIC" = asm900-analysis/ASM900_STATIC.md (asm900's tables read, never run, D9).

## Language and encoding

| # | Point | Status | unitron does |
|---|---|---|---|
| O1 | Which asm900 built the reference ROM | OPEN. STATIC argued "not V2.2d/2.2k" from the `ld r,r` rule, but that rested on the plan's misread rule (below); the ROM agrees with asm900's `ld` | nothing version-specific beyond the 32-char warning |
| O2 | asm900's own encoding choices | settled by STATIC for `ld r,r`, `ld R,#`, MUL/DIV, CP, widths, `+0`, steps; not checked by running ASM900 (D9 forbids it) | docs/encoding.md |
| O3 | `jp XHL` vs `jp (XHL)` | settled: bare only; `jp (x)` is an error [STATIC 3.1] | same |
| O4 | `$list...`, word operators | settled: listing only; no `MOD NOT AND OR XOR` [STATIC 3.5] | ignores `$list...`; operators `+ - * / % & \| ^ ~ << >> :` |
| O5 | `1234H` numbers | settled: only with asm900 `-H` [STATIC 3.2] | error by default; `unitron asm -H` accepts them |
| O6 | `push n` / `pushb n` | settled: the same, byte [STATIC 3.3] | same |
| O7 | TUMPL 1.0j vs tumpp V2.02 | settled in the plan's direction: the documented `?macro` is tumpp's [STATIC 6] | tumpp subset (docs/language.md) |
| O8 | `extern large` naming another module's abs-section symbol | LIKELY allowed [STATIC 3.6]; tulink not read | allowed; references take the extern width |
| O9 | the 6 misc long forms as "couldn't size in pass 1" | false for asm900 [STATIC 2.5]; they need `:n` | `:n` |
| O10 | identifier significance | 1024 (V2.2k) vs 32 (V2.2d) [STATIC 3.4] | 1024, with a warning on 32-char collisions |
| - | `0b` binary | TR p.8 documents `0b`/`0y`; asm900's lexer rejects `0b` (E202) [STATIC 3.2] | follows asm900: `0b` is an error |
| - | `ldar` with a label | the datasheet shows only `$+d16` | `ldar XWA,label`, offset from the instruction + 4 |
| - | `ld RA3,5` (non-standard byte register) | STATIC 2.4 says form 4 (`C7 xx 03 #`) is the path for non-standard registers but does not say whether `LD r,#3` is tried first | `LD r,#3` when the value fits 3 bits (shortest) |
| - | omitted operands | `EI` and `SWI` with no operand; `DJNZ` with no register | `ei` = `ei 0`, `swi` = `swi 7`, `djnz label` = `djnz B,label` |
| - | `ldf` range | 900/H has 4 banks; the field is #3 | accepts 0-7 |
| - | missing `end` | TR p.18 says error, the error list (506) says warning | warning W506 |
| - | `$include` limits (8 deep, 99 per unit) [TR p.24, 32] | | enforced only with `--strict` (8 deep) |
| - | relocatable placement | Toshiba uses tulink and an `.lcf` file, not implemented | `--place`, else first fit (docs/language.md "Linking") |
| - | `j`/`cal` | asm900 relaxes only with `-O1` | always relaxes, absolute targets only |
| - | initialized data in `data` sections | TR doesn't say what the image holds | bytes outside the image are dropped with a warning; `ds*` reserve only |
| - | `#define` and `Routine.loop` | tumpp identifiers have no `.`, so a define named `loop` also replaces the tail of `Routine.loop` | documented tumpp behavior |
| - | `?if ?repeat ?while ?variable ?eval` and string macros | documented by TR, not needed yet | error "not supported" |

## `ld r,r` and the ROM

The plan said the ROM puts the register with the higher code in the prefix (417/417) and that
asm900's source-in-prefix rule would match only 194. STATIC proved asm900's rule (prefix = source
when the destination is standard) and repeated the plan's count. Measured here: the ROM uses the
source-in-prefix form in all 417 standard-register `ld r,r` of the decomp source (505 with Q and
banked registers) and in all 404 that unitron's trace finds. The old assembler's default put the
lower code in the prefix, so its 194 `{n}` markers were exactly the cases where the source code was
higher. Result: no the reference ROM `ld r,r` needs anything special.

User decision for the case that does come up (another ROM, or a form with no asm900 spelling):
unitron implements asm900's real rule only, with no unitron-only marker. A game-specific encoding
belongs in the decomp, pret-style, as a macro in its own macros file (e.g. `ld_alt B,A` expanding
to the original bytes) or as `db` with the instruction in a comment; the writer and the converter
emit `db`. If other NGPC games show the same rule, it may become a unitron option later.

## NGPC facts (hardware names, docs/hardware-names.md)

- That the NGPC CPU's on-chip I/O matches Toshiba's TMP95C061F header (IO900H.H). SNK's timer,
  DMA and watchdog pages give the same addresses; nothing confirms the rest.
- BIOS call numbers: SNK SysCall points to SNK's SYSTEM.INC, which we don't have; no vector
  numbers are named. (Plan 4: the reference ROM's reset call RW3=1 could be CLOCKGEARSET or INTLVSET.)
- Z80 interface 0xB8/0xBA/0xBC: no SNK document; unnamed until the Z80 driver is read.
- K2GE 0x8000, 0x8006 (frame rate), 0x8400, 0x87E0, 0x87E2 (mode), 0x87FE: no SNK address.
- K2GE fields BLNK, OOWC, BGON/BGC: no bit positions in our SNK text.
- Sys_Lever bit 7 (probably the NEO GEO controller's D button; the SNK diagram is cropped).
- 0x6FEC vector slot: reserved in SNK SysPro; unnamed.

## Tools

- Positioned tile-sheet layout (`unitron gfx --map`): a sheet tile n is character code
  `base + n + 1` (`--base`); code 0 is an empty cell. Map files are `wh8` (width byte, height
  byte, entries; the reference ROM's form) or `raw` with `--map-width`. Other games' map headers may differ.
- The trace's gap sweep is a heuristic (data may decode as code); the writer still reproduces the
  bytes, but labels in swept code are guesses.
