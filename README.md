# unitron

A toolchain for Neo Geo Pocket Color (TLCS-900/H) decompilation and development, in Python.
Its source language is Toshiba's asm900 syntax, so the same source reads like the original
developers' and assembles with unitron byte for byte.

- `unitron asm`: asm900 assembler and linker (sections, `org`, `equ`, `db/dw/dp/dl`, `dfb..`,
  `dsb..`, `public/extern`, `$include`, the `:n` width operator) with Toshiba's macro
  preprocessor subset (`#include`, `#define`, `#if`, `?macro ... ?endm`). Output: a raw image.
- `unitron dis`: TLCS-900/H disassembler. `-o DIR` traces a ROM and writes asm900 source that
  assembles back to the same bytes; without `-o` it lists.
- `unitron roundtrip ROM`: dis then asm, and compare.
- `unitron mac`: the macro preprocessor alone.
- `unitron lz c|d`: LZSS with Okumura's LZSS.C parameters.
- `unitron gfx topng|frompng`: 2bpp tiles and palettes to indexed PNG and back, as a plain sheet
  or laid out by a tile map (`--map`).
- `unitron bin2db`: binary file to `db` lines for `$include`; `unitron text`: text with a
  character map to `db` lines (asm900 has no charmap).
- `unitron lift`: traced code to C (experimental).

## Install and use

```
python -m pip install -e .          # into your venv; pillow for gfx
unitron asm main.asm -o game.ngp --origin 0x200000 --size 0x100000
unitron dis game.ngp -o src/        # src/main.asm + src/hw.inc
unitron roundtrip game.ngp
python -I -m pytest tests           # ROM tests run if $UNITRON_ROM (or the the reference ROM path) exists
```

`unitron asm` options: `-I DIR`, `-D NAME=VAL`, `--origin`, `--size`, `--fill`, `--place
SECTION=ADDR`, `--data-base`, `--code-base`, `--no-link` (check one module), `--no-mpp`,
`--strict`, `--sym FILE`, `-H` (asm900's suffixed numbers). Errors look like
`file:line: error E217: ...` with Toshiba's error numbers where one fits; no output is written when
there are errors. A 1 MB ROM assembles in about 3 seconds.

## The language

docs/language.md is the reference as implemented; docs/encoding.md says which encoding unitron
picks when several are legal; docs/OPEN.md lists what is not settled. In short:

```
$maximum
	module	game
VBLANK	equ	0x6FCC
c_200040	section	code abs=0x200040
Start:
	ld	XHL,Vblank
	ld	(VBLANK),XHL
Start.loop:
	ldb	(0x6F82),0x10
	jr	Start.loop
Vblank:	reti
	end
```

Lowercase mnemonics, uppercase registers, `0x` hex, `Routine.label` local labels, `jp XHL`
without parentheses, `(XHL+:1)` for a step that differs from the operand size, `ld (wX:24),A` to
force a width.

## What is ours and what is cited

All code here is ours (MIT, see LICENSE). It cites, and never contains, these sources:

- Toshiba *TLCS-900 Assembler Reference* tde122-01_96 (the language, the macro preprocessor, the
  error numbers), the TLCS-900 CPU datasheet instruction lists (forms, codes, size columns), the
  sample programs SAMPLE1.ASM/SAMPLE2.ASM (tests assemble them when present), and the TMP95C061F
  register header IO900H.H (on-chip I/O names).
- A static reading of asm900's own tables (asm900-analysis/ASM900_STATIC.md; asm900 was never run)
  for the points TR leaves open.
- SNK's NGPC developer documents (K2GETechRef, K2GEres, SysWork, SysPro, SysCall, SysLib, ...):
  K2GE registers, system RAM, interrupt vectors, tile, map and palette formats, the cartridge
  header. docs/hardware-names.md gives the source of every hardware name in
  unitron/data/ngpc_hw.inc and lists where homebrew headers disagree.
- Haruhiko Okumura's LZSS.C (1989, public domain): the compressor's algorithm and parameters.

No emulator code, no emulator-derived names, no Toshiba or SNK files, and no ROM data are in this
repository. tools/convert_decomp.py is a one-off migration helper for the the reference ROM decomp; it reads
that repo and writes only where you tell it (use /tmp).
