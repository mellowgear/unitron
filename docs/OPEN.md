# OPEN points

What is not settled, with what unitron does meanwhile, then what was settled and why. "Plan" =
ASM900_PLAN.md (O1-O10 are its numbers); "STATIC" = asm900-analysis/ASM900_STATIC.md (asm900's
tables read, never run, D9); "TR" = Toshiba's Assembler Reference; "DS" = the CPU datasheet
instruction lists.

## Still open

| # | Point | Status | unitron does |
|---|---|---|---|
| O1 | Which asm900 built the reference ROM | OPEN. STATIC argued "not V2.2d/2.2k" from the `ld r,r` rule, but that rested on the plan's misread rule (below); the ROM agrees with asm900's `ld` | nothing version-specific beyond the 32-char warning |
| O2 | asm900's own encoding choices | settled by STATIC for `ld r,r`, `ld R,#`, MUL/DIV, CP, widths, `+0`, steps; never checked by running ASM900 (D9 forbids it) | docs/encoding.md |
| O8 | `extern large` naming another module's abs-section symbol | LIKELY allowed [STATIC 3.6]; tulink was not read | allowed; references take the extern width |
| O10 | identifier significance | 1024 (V2.2k) vs 32 (V2.2d) [STATIC 3.4]; depends on O1 | 1024, with a warning on 32-char collisions |
| - | tulink beyond docs/lcf.md | libraries, incremental links (`-r`), the `I` attribute, `-F` fill, padding in the image, assembler symbols in lcf expressions | rejected or ignored as docs/lcf.md "Not supported" says |
| - | BIOS call numbers | no SNK source (below) | no names |
| - | NGPC facts | below | unnamed where no source |

## Settled

| # | Point | Evidence | unitron does |
|---|---|---|---|
| O3 | `jp XHL` vs `jp (XHL)` | bare only [STATIC 3.1] | `jp (x)` is an error |
| O4 | `$list...`, word operators | listing only; no `MOD NOT AND OR XOR` [STATIC 3.5] | ignores `$list...`; operators `+ - * / % & \| ^ ~ << >> :` |
| O5 | `1234H` numbers | only with asm900 `-H` [STATIC 3.2] | error by default; `unitron asm -H` accepts them |
| O6 | `push n` / `pushb n` | the same, byte [STATIC 3.3] | same |
| O7 | TUMPL 1.0j vs tumpp V2.02 | the documented `?macro` is tumpp's [STATIC 6] | tumpp subset (docs/language.md) |
| O9 | the 6 misc long forms as "couldn't size in pass 1" | false for asm900 [STATIC 2.5] | they need `:n` |
| - | `0b` binary | TR p.8 documents `0b`/`0y`, but asm900's lexer rejects `0b` (E202) [STATIC 3.2] | follows asm900: `0b` is an error |
| - | `ld RA3,5` (a non-standard register, small value) | STATIC 2.4 names form 4 (`C7 rr 03 #`) for non-standard registers but not whether `LD r,#3` is tried first. The ROM: all 83 `ld` of 0-7 into a non-standard register in unitron's the reference ROM trace (74 byte: `QA..QD`, `RA3..RC3`; 9 word/long: `QW`, `RW3`, `RBC3`) use `LD r,#3`, none needs a `:n` width | `LD r,#3` when the value fits 3 bits (shortest) |
| - | omitted operands | DS notes: "When operand #3 coding in the EI instruction is omitted, 0 is used", the same for SWI with 7. DS writes DJNZ as `[r, ]$+3/4+d8` and names no default register | `ei` = `ei 0`, `swi` = `swi 7`, `djnz label` = `djnz B,label` |
| - | `ldf` range | DS: `LDF #3`, and asm900 encodes a #3 field (operand role 7, STATIC "Operand roles"); the 900/H has 4 banks | accepts 0-7, as asm900 does |
| - | `ldar` with a label | DS shows only `ldar R,$+4+d16` | both work: `ldar XWA,label` and `ldar XWA,$+0x10` give the same offset (from the instruction + 4) |
| - | missing `end` | TR p.18 calls it an error, but TR's error list says warning 506: "It is treated as if the directive exists" | warning W506 |
| - | `$include` limits | TR p.24, 32: 8 levels deep, 99 per translation unit | a warning by default; with `--strict` error E234. Nesting deeper than 64 (a recursive include) is always E234 |
| - | relocatable placement | Toshiba uses tulink with an `.lcf` file [TR ch. 8] | `--lcf FILE` (a subset, docs/lcf.md); without it `--place`, else first fit (docs/language.md "Linking") |
| - | `j`/`cal` | TR p.16-17, p.29: asm900 relaxes with `-O1`; `-O0` keeps jp/call | `-O1` (default) relaxes for absolute targets and same-section labels; `-O0` never (docs/encoding.md rule 9) |
| - | initialized data in `data` sections | TR doesn't say what the image holds; `data` sections are RAM | the bytes are not in the image, with a warning naming `ds*`, `romdata`, or an `.lcf` `org=` in ROM |
| - | `#define` and `Routine.loop` | tumpp identifiers have no `.`; asm900's do [STATIC 3.4] | a dotted name is one token, never rewritten by a `#define` |
| - | `?if ?repeat ?while ?variable ?eval` and string macros | documented by TR ch. 15, not needed yet | error `?repeat is not supported (only ?macro/?endm)` |
| - | `--fill` value | a byte | outside 0-255 is an error |
| - | trace gap sweep | a heuristic; results depended on tuning | `--hints FILE` (`code ADDR [NAME]`, `data START END`) pins it down; without it the output is unchanged |

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
- Z80 interface 0xB8/0xBA/0xBC: no SNK document; unnamed. The drivers have been read since (rows
  under "Z80 sound CPU" below): what the code does with them is known, but no source names them.
- K2GE 0x8000, 0x8006 (frame rate), 0x8400, 0x87E0, 0x87E2 (mode), 0x87FE: no SNK address.
- K2GE fields BLNK, OOWC, BGON/BGC: no bit positions in our SNK text.
- Sys_Lever bit 7 (probably the NEO GEO controller's D button; the SNK diagram is cropped).
- 0x6FEC vector slot: reserved in SNK SysPro; unnamed.

## Z80 sound CPU (docs/z80.md, docs/Z80_PLAN.md)

| Point | Status | unitron does |
|---|---|---|
| 0xB8/0xB9 | the main CPU writes the word 0xAAAA to stop the Z80 and 0x5555 to start it (Z80_PLAN.md 1.3, code evidence only); what each byte does alone is unknown; no SNK document | numeric |
| 0xBA | every command write is followed by a write here, and the Z80's NMI reads the command; whether a read also raises the NMI is unknown | numeric |
| 0xBC = Z80 8000H | the shared command byte (the Z80 writes back its complement); no source names it | numeric on both sides |
| Z80 4000H/4001H | the sound chip, written in the SN76489-style latch format, apparently two outputs; its name, registers and clock are in no source (SNK SoundMan is empty in our copy) | numeric; a game's own include file may name them |
| Z80 port 0FFH, Z80 0C000H | written by both drivers; purpose unknown (perhaps the "Interrupt From Z80" path, SysPro l.219) | numeric |
| the tick rate | which TO3 edge interrupts the Z80, and fT1: SNK 8Bit gives 20.83 µs, a homebrew comment 1.302 µs (Z80_PLAN.md 1.3) | nothing depends on it |
| the Z80 memory map | 4 KB of RAM (SysPro l.108); mirrors and unmapped areas unknown | `z80 asm` rejects bytes past 0FFFH; anything outside the RAM is an `EQU` |
| `IM` detection | not done: the tracer always starts from 0000H, 0038H and 0066H | traces all three |
| immediates inside the image | `LD HL,00C0H` may be an address or a count | stays a number; a source that moves code labels it by hand |
| `DriverEnd` reach | the drivers hard-code their end and up to 6 bytes past it; 15 is a margin, and a data word equal to the image size could be a coincidence | `DriverEnd+0` .. `+15` for memory operands and `LD rr,nn`; `DEFW DriverEnd` for a data word equal to the size (docs/z80.md, "Labels") |
| 694 vs 696 forms | ZM's listing has two `LD` statements fewer than unitron's table; which two wasn't found (noisy text extraction); all 696 agree with AS | all 696 (docs/z80.md, "Instructions") |
| the one-character `.OR.` | unreadable in our copy of ZM | only `.OR.` |
| macros | ZM chapter III, not needed yet | error "macros are not supported yet" |
| `--equ` names | Z80 labels may clash with main-CPU names; there is no prefix option | labels as they are; `?` and asm900 reserved words are errors |
| who wrote the driver | likely SNK (the second ROM carries a version), unproven (Z80_PLAN.md 9) | nothing driver-specific in unitron |
| the music data | stream encoding, voice fields and tempo unknown (Z80_PLAN.md 6) | no tool yet (step 13) |

## Tools

- Positioned tile-sheet layout (`unitron gfx --map`): a sheet tile n is character code
  `base + n + 1` (`--base`); code 0 is an empty cell. Map files are `wh8` (width byte, height
  byte, entries; the reference ROM's form) or `raw` with `--map-width`. Other games' map headers may differ.
- The trace's gap sweep is a heuristic (data may decode as code); the writer still reproduces the
  bytes, but labels in swept code are guesses. `--hints` fixes the result where it matters.
