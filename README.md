# unitron

A toolchain for Neo Geo Pocket Color (TLCS-900/H) decompilation and development, written in C++17.
Its source language is Toshiba's asm900 syntax, so the same source reads like the original
developers' and assembles with unitron byte for byte.

- `unitron asm`: asm900 assembler and linker (sections, `org`, `equ`, `db/dw/dp/dl`, `dfb..`,
  `dsb..`, `public/extern`, `$include`, the `:n` width operator) with Toshiba's macro
  preprocessor subset (`#include`, `#define`, `#if`, `?macro ... ?endm`) and a subset of
  Toshiba's link command files (`--lcf`). Output: a raw image.
- `unitron dis`: TLCS-900/H disassembler. `-o DIR` traces a ROM and writes asm900 source that
  assembles back to the same bytes, with a placeholder label (`sub_XXXXXX`, `loc_XXXXXX`, ...) on
  every jump or call target in its code; without `-o` it lists.
- `unitron roundtrip ROM`: dis then asm, and compare.
- `unitron mac`: the macro preprocessor alone.
- `unitron gfx topng|frompng|palette`: 2bpp tiles and palettes to indexed PNG and back, as a
  plain sheet or laid out by a tile map (`--map`); a PNG's pixels as bytes (`--indices`); a PNG's
  palette as 0x0BGR words. See "Graphics and text" below.
- `unitron bin2db`: binary file to `db` lines for `$include`; `unitron text`: text with a
  character map to `db` lines (asm900 has no charmap), optionally padded to fixed-size slots.
- `unitron z80 dis|asm|roundtrip`: the same for the Z80 sound CPU's program, in Zilog's own
  assembly language (docs/z80.md). `dis` traces from the Z80's reset and interrupt addresses and
  writes source that assembles back to the same bytes; `asm` builds a raw image at Z80 0000H,
  and with `--equ`/`--db` the two files the main CPU's asm900 source includes.

## Build and install

Needs CMake 3.24 or later and a C++17 compiler. The first configure downloads zlib 1.4 and
libpng 1.6.59 (pinned by SHA256, built static; `cmake/deps.cmake`), so it needs internet. If the
libpng download fails its TLS check (the SourceForge mirror does this sometimes), run the
configure again. `-DUNITRON_SYSTEM_DEPS=ON` uses installed copies instead.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
(cd build && ctest)                   # rom and z80rom skip themselves without the ROMs
cmake --install build --prefix ~/.local    # puts unitron in ~/.local/bin
```

The result is one binary, `build/unitron`, with no runtime dependencies. It has been built and
tested on Linux only so far; the code is meant to be portable (macOS, Windows MSVC and MinGW).

## Use

```
unitron asm main.asm -o game.ngp --origin 0x200000 --size 0x100000
unitron asm crt.asm main.asm -o game.ngp --lcf game.lcf
unitron dis game.ngp -o src/        # src/main.asm + src/hw.inc
unitron dis game.ngp -o src/ --hints hints.txt
unitron roundtrip game.ngp
unitron z80 dis driver.bin           # Z80 listing; -o DIR writes DIR/main.z80
unitron z80 asm main.z80 -o driver.bin --equ driver.inc --db driver.db
unitron z80 roundtrip driver.bin
```

`unitron asm` options: `-I DIR`, `-D NAME=VAL`, `--origin`, `--size`, `--fill` (0-255),
`--place SECTION=ADDR`, `--lcf FILE` (docs/lcf.md), `--data-base`, `--code-base`, `--no-link`
(check one module), `--no-mpp`, `--strict` (asm900's include limits are errors), `--sym FILE`,
`-H` (asm900's suffixed numbers), `-O0`/`-O1` (how `j`/`cal` choose, as asm900's `-O`; default
`-O1`). Errors look like `file:line: error E217: ...` with Toshiba's error numbers where one fits;
no output is written when there are errors. A 1 MB ROM round trip (dis and asm) takes under a
second.

Make dependencies, as another assembler's `-M`: `-M FILE` (`-` for the screen) writes one line
`TARGET: FILE` for every file the assembly reads (the sources, `#include` and `$include` files,
the `--lcf` file). The target is the `-o` file, or `-MT TARGET` (may be given more than once).
`-MG` lists an include file that doesn't exist yet (one the build will make) instead of stopping
with an error; if one is missing, nothing is assembled. `-MP` adds an empty rule `FILE:` for each
include file, so make doesn't fail when one is deleted. Without `-o`, `-M` only reads what decides
the includes (the preprocessor, `$include` lines, `end`) and writes nothing else; with no target
either, it lists the files alone, one per line, which a Makefile can read while it starts:

```
PRODUCTS := $(filter build/%,$(shell unitron asm src/main.asm -I . -M - -MG))
game.ngp: src/main.asm
	unitron asm src/main.asm -I . -o $@ -M build/game.d -MP
-include build/game.d
```

`dis` and `roundtrip` take `--executed FILE` (extra code entry points, hex addresses)
and `--hints FILE`, which makes the gap sweep reproducible (docs/language.md, "Trace hints"):

```
# comments start with # or ;
code 200185 hidden_fn     ; trace from here (name optional, default code_200185)
data 20018C 200191        ; no swept instruction touches [start, end)
```

The gap sweep covers the whole image after the header; outside the span of traced code
it starts only from call targets and from pointer tables that also point at known
code (docs/language.md, "Trace hints").

## The language

docs/language.md is the reference as implemented; docs/encoding.md says which encoding unitron
picks when several are legal; docs/lcf.md covers link command files; docs/OPEN.md lists what is
not settled. In short:

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

## Graphics and text

The NGPC formats these commands write are SNK's (K2GETechRef 4-3-5 tiles, 4-4-4 map entries,
4-15 colour palettes). A game's own formats (compression, image headers) belong in the game's
repository.

```
unitron gfx topng tiles.bin sheet.png [--width 16] [--palette pal.bin --pal N] [--map map.bin]
unitron gfx frompng sheet.png tiles.bin [--count N] [--trim] [--map map.bin --count N]
unitron gfx frompng image.png pixels.bin --indices
unitron gfx palette image.png pal.bin [--colors 4]
unitron text charmap.txt strings.txt -o strings.asm
```

- **Tiles** are 8x8 pixels, 2 bits per pixel, 16 bytes each. In the PNG a pixel is a palette
  index; the tile keeps `index & 3` (the shade). Index 4p + s is shade s in palette p (what
  `topng --palette` writes), so colouring a PNG never changes its tiles.
- **`frompng`** reads the cells of the sheet left to right, top to bottom: every cell, or the
  first `--count N`. A PNG written by `topng --map` carries its layout, and then every tile in it
  is read. With `--map`, `--count` is needed.
- **`--trim`** drops blank cells from the end of the last row of cells; that row's first cell
  always stays. A cell is blank when every pixel is palette index 0: a cell drawn only in index 4
  is kept, though its tile bytes are all zero. `--count` wins over `--trim`. Plain sheets only.
- **`--indices`** writes each pixel's palette index as one byte, left to right, top to bottom,
  with no tiles and no `& 3`; any width and height.
- **`palette`** writes the PNG's first N palette colours (default 4) as little-endian 16-bit words
  0x0BGR, 4 bits per channel. Each channel becomes the nearest of the 16 levels (round(c / 17));
  c / 17 is never exactly halfway, so there are no ties. Fewer colours than N is an error.

`unitron text` turns lines of a text file into `db` lines, with a character map: `char "a" 01`
lines give each character's byte, `cmd NAME XX [string]` lines name commands (`cmd text - string`,
`cmd done FF`). In the text file, `{XX}` is a byte with no character, `Name:` lines are labels and
`;` lines are comments. A **`slot WIDTH PAD`** line (`slot 7 FF`: decimal width, hex byte) makes
every string after it exactly WIDTH bytes, for a table of fixed-size names:

```
PartNames:
slot 7 FF
	text "Body1"
	done
	text "Body10"	; 6 characters + done = 7 bytes: no padding
	done
slot 0
```

A string starts at a `text` line and ends before the next `text` line, label, `slot` line or the
end of the file. It is padded with PAD up to WIDTH (a `dfb` line); a longer one is an error with
its line, byte count and width. A string that fills the slot without `done` has no end byte.
`slot 0` ends slot mode.

## Tests

`(cd build && ctest)` runs them all; tests/README.md says how to run one, add a case and rewrite
expected outputs (`--write`).

| Test | Checks |
|---|---|
| `cases-asm`, `cases-mpp`, `cases-link`, `cases-lcf`, `cases-dis`, `cases-gfx`, `cases-text`, `cases-bin2db`, `cases-z80`, `cases-cli` | about 500 small cases in the style of another toolchain's tests: `tests/asm/NAME.asm` with `NAME.flags` or `NAME.cmd`, and the exact expected stdout (`NAME.out`), stderr (`NAME.err`) and output files (`NAME.out.bin`, ...) |
| `datasheet` | encodings from Toshiba's datasheet and Assembler Reference and from Zilog's UM0080, with their pages (`tests/datasheet/`) |
| `opspace`, `lines` | every TLCS-900 opcode decoded and re-assembled; 346,448 instruction lines (vectors frozen from the original Python implementation) |
| `z80opspace`, `z80shift` | every documented Z80 form; one inserted `NOP` moves every labeled reference by one |
| `lcf` | the link command file parser and placement |
| `hwinc`, `hwnames` | `data/ngpc_hw.inc` against `data/hwnames.tsv`; the table itself (unique, sorted, cited, spelled by the naming rule) |
| `cart` | our own test cartridge (`tests/cart/`, an asm900 program with a Z80 program, also an example): `dis`, `roundtrip`, `z80 dis`, `z80 roundtrip`, compared with `tests/cart/expected/` |
| `rom`, `z80rom` | the reference ROM's round trip and the Z80 drivers of the reference ROM and the second ROM by hash; skipped without the ROMs |

No test needs a ROM except `rom` and `z80rom`. tests/README.md "Known issues" lists the cases
that freeze behavior that looks wrong.

## What is ours and what is cited

All code here is ours (MIT, see LICENSE), except zlib and libpng, which the build downloads and
which keep their own licenses. It cites, and never contains, these sources:

- Toshiba *TLCS-900 Assembler Reference* tde122-01_96 (the language, the macro preprocessor, the
  linker's command files, the error numbers), the TLCS-900 CPU datasheet instruction lists (forms,
  codes, size columns), the sample programs SAMPLE1.ASM/SAMPLE2.ASM (tests check them by hash
  when present), and the TMP95C061F register header IO900H.H (on-chip I/O names).
- A static reading of asm900's own tables (asm900-analysis/ASM900_STATIC.md; asm900 was never run)
  for the points TR leaves open.
- SNK's NGPC developer documents (K2GETechRef, K2GEres, SysWork, SysPro, SysCall, SysLib, ...):
  K2GE registers, system RAM, interrupt vectors, tile, map and palette formats, the cartridge
  header. `data/hwnames.tsv` is the single table of hardware names, with a source for each;
  docs/hardware-names.md explains it and lists where homebrew headers disagree.
- Zilog's *Z80 CPU User Manual* UM0080 (the Z80 instructions and their codes) and Zilog's *Z80
  Assembly Language Programming Manual* (1978; the Z80 assembler language and its error numbers),
  for the `z80` commands (docs/z80.md).

No emulator code, no emulator-derived names, no Toshiba or SNK files, and no ROM data are in this
repository.

The original Python implementation was removed on 2026-11-25; its behavior lives on in the frozen
test vectors, and its code is in git history (before the commit that removed `legacy/python/`).
