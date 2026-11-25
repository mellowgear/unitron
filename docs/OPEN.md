# Open issues

Known open points: what is unsettled, and what unitron does meanwhile. TR, DS and asm900 are the
sources named in `language.md`; ZM and UM those in `z80.md`.

- **asm900 version of the reference ROM:** unknown; the ROM's `ld r,r` agree with both V2.2d and
  V2.2k. unitron does nothing version-specific beyond the 32-character warning.
- **asm900's encoding choices** (`ld r,r`, `ld R,#`, MUL/DIV, CP, widths, `+0`, steps): read from
  its tables, never checked by running it. unitron follows `encoding.md`.
- **Identifier significance:** 1024 characters in asm900 V2.2k, 32 in V2.2d. unitron uses 1024
  and warns when two names collide in their first 32 characters.
- **`extern large` naming another module's abs-section symbol:** probably allowed (asm900 takes
  the width from the extern; tulink was not read). unitron allows it with the extern's width.
- **tulink beyond `lcf.md`:** libraries, `-r`, the `I` attribute, `-F` fill, image padding,
  assembler symbols in lcf expressions. unitron rejects or ignores them (`lcf.md`, "Not supported").
- **`equ` and `org` forward references:** asm900 gives E223 for a symbol defined on a later line
  here too. unitron still accepts them.
- **`?if ?repeat ?while ?variable ?eval` and the string macros** (TR chapter 15): not needed yet.
  unitron reports `?repeat is not supported (only ?macro/?endm)`.
- **The trace's gap sweep:** a heuristic; data may decode as code, and labels in swept code are
  guesses (the bytes still match). `--hints FILE` pins it down (`language.md`, "Trace hints").
- **`dis --names` and `equ` constants:** a symbol file's constants (sizes, flags) are taken for
  addresses like its labels; unitron can't tell them apart. The project removes them from the
  file (`language.md`, "Names").
- **The BIOS-call subroutine shape** (`language.md`, "Other `dis -o` output"): recognised by its
  form, not by any address; a short wrapper of another kind with an indirect call matches too.
- **`gfx --map` layouts of other games:** their map headers may differ. unitron reads the two
  forms `--map-format` names, `wh8` and `raw` (README, "Graphics").
- **An `ld r,r` encoding another ROM uses that asm900's rule doesn't give:** none seen yet.
  Such code is written as `db` or a project macro; an option only if several NGPC games share it.
- **BIOS call numbers and other NGPC facts with no source** (`hardware-names.md`, "Not named"):
  left unnamed.
- **Z80 ports 0xB8/0xB9:** the main CPU writes the word 0xAAAA to stop the Z80 and 0x5555 to start
  it (code evidence only); what each byte does alone is unknown. unitron keeps them numeric.
- **0xBA:** every command write is followed by a write here, and the Z80's NMI reads the command;
  whether a read also raises the NMI is unknown. Numeric.
- **0xBC = Z80 8000H:** the shared command byte (the Z80 writes back its complement); no source
  names it. Numeric on both sides.
- **Z80 4000H/4001H:** the sound chip, written in the SN76489-style latch format, apparently two
  outputs; name, registers and clock are in no source. Numeric; a program's include may name them.
- **Z80 port 0FFH and Z80 0C000H:** written by both drivers seen, purpose unknown (perhaps the
  "Interrupt From Z80" path, SNK *SysPro*, "SYSTEM INFORMATION"). Numeric.
- **The Z80 tick rate:** which TO3 edge interrupts the Z80, and fT1 (SNK *8Bit*, "1. PRESCALER":
  20.83 µs; a homebrew header's comment: 1.302 µs). Nothing in unitron depends on it.
- **The Z80 memory map:** 4 KB of RAM (SNK *SysPro*, "SOUND CONTROL CPU"); mirrors and unmapped
  areas unknown. `z80 asm` rejects bytes past 0FFFH; anything outside the RAM is an `EQU`.
- **Z80 `IM` detection:** not done. `z80 dis` traces from 0000H, 0038H and 0066H.
- **Z80 immediates inside the image** (`LD HL,00C0H`: an address or a count?): they stay numbers;
  a source that moves code labels them by hand.
- **`DriverEnd` reach:** the drivers use up to 6 bytes past their end; 15 is a margin, and a data
  word equal to the image size could be a coincidence (`z80.md`, "Labels").
- **694 vs 696 Z80 forms:** ZM lists two `LD` statements fewer than unitron's table, which two is
  unknown; all 696 agree with AS. unitron accepts all 696 (`z80.md`, "Instructions").
- **The one-character `.OR.`:** illegible in the available scan of ZM. unitron accepts only `.OR.`.
- **Z80 macros** (ZM chapter III): not needed yet. Error "macros are not supported yet".
- **`--equ` names:** Z80 labels may clash with main-CPU names, and there is no prefix option.
  Labels are written as they are; `?` and asm900 reserved words are errors.
- **Who wrote the NGPC sound driver:** likely SNK (a first-party SNK game carries a version),
  unproven. Nothing in unitron is driver-specific.
- **The sound driver's music data:** stream encoding, voice fields and tempo unknown. No tool yet.
