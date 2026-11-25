# unitron

A toolchain for the Neo Geo Pocket Color, in C++17. It assembles, links and disassembles programs
for the console's main CPU (Toshiba TLCS-900/H) in Toshiba's own assembler language, asm900, and
for its Z80 sound CPU in Zilog's. A disassembled ROM comes out as source that assembles back to
the same bytes, so it can be the start of a decompilation.

## Build

You need CMake 3.24 or later and a C++17 compiler. The first build downloads zlib and libpng
(pinned versions, built in; see `cmake/deps.cmake`), so it needs internet. If the libpng download
fails its TLS check (the SourceForge mirror sometimes does), run the first command again.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
(cd build && ctest)                          # runs the tests
cmake --install build --prefix ~/.local      # optional: puts unitron in ~/.local/bin
```

The result is one program, `build/unitron`, with no other files needed. It has been built and
tested on Linux; the code is written to be portable to macOS and Windows.

## Quick start

### Turn a ROM into source, and back

```
unitron dis game.ngp -o src/
```

This writes `src/main.asm` (the whole ROM as asm900 source) and `src/hw.inc` (the names of the
hardware registers the code uses). To build a ROM from that source again:

```
unitron asm src/main.asm -o rebuilt.ngp
cmp game.ngp rebuilt.ngp                     # no output: the two files are identical
```

The source holds every byte of the ROM, so no options are needed.

`unitron roundtrip game.ngp` does both steps in one go and says whether the bytes match.

The labels in the source are placeholders built from addresses (`sub_2004A0` for a routine that
is called, `loc_2004B2` for a place that is jumped to, `swp_...` for code found by scanning). A
disassembler can't know what anything is called; renaming them is the decompilation work.

Some code is only reached through tables of addresses, which the disassembler can't always follow.
A hints file tells it where code starts or where data is:

```
# hints.txt: comments start with # or ;
code 200185 hidden_fn     ; code starts here (the name is optional)
data 20018C 200191        ; these bytes are data: never decode them as code
```

```
unitron dis game.ngp -o src/ --hints hints.txt
```

### Write a program

A small program in one file. Every address is written in the source (`abs=`), so no link file is
needed:

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

```
unitron asm main.asm -o game.ngp
```

The image starts at 0x200000, where the cartridge sits in memory, and ends at the program's last
byte; `--size 0x100000` pads it to a full 1 MB cartridge (see "Addresses and image size"). A real cartridge also needs SNK's header at 0x200000 (a recognition string, the start address,
the game's title and so on). `tests/cart/` is a complete example cartridge, written for unitron's
tests, with the header, interrupt handlers, several source files, a link file and a Z80 sound
program; the comment at the top of `tests/cart/main.asm` shows how it is built.

### Several source files and a link file

In a bigger program, sections can leave their address out:

```
code	section	code            ; no abs=: the linker decides where this goes
```

Each source file on the command line is one *module*, and a section without `abs=` is
*relocatable*. Something has to decide where those sections go, and there are three ways:

1. **Nothing:** unitron puts each one in the first free space (code from 0x200000, data from 0x4000).
2. **`--place NAME=ADDR`**, once per section, on the command line.
3. **A link command file** (`.lcf`), Toshiba's format for this. You write it yourself: it lists the
   memory areas (RAM at 0x4000, the cartridge ROM at 0x200000) and which sections go where, in
   order. Start from `tests/cart/cart.lcf` (a complete, commented one for a small cartridge) or
   the example in `docs/lcf.md`, which also lists everything unitron supports.

```
unitron asm crt.asm main.asm -o game.ngp --lcf game.lcf
```

Source that comes out of `unitron dis` gives every section an address, so it never needs a link
file.

### The Z80 sound program

The NGPC's Z80 runs a separate sound program, which the main CPU copies into the Z80's memory.
It has its own commands, in Zilog's assembly language (`docs/z80.md`):

```
unitron z80 dis driver.bin -o zsrc/          # zsrc/main.z80
unitron z80 asm zsrc/main.z80 -o driver.bin  # and back
unitron z80 roundtrip driver.bin
unitron z80 asm sound.z80 -o sound.bin --equ sound.inc --db sound.db
```

`--db` writes the Z80 program as asm900 `db` lines and `--equ` its labels as asm900 `equ` lines,
for the main program to include.

## Addresses and image size

`unitron asm` writes a raw image: the bytes of one range of the main CPU's addresses. `--origin`
is the address of the image's first byte, and `--size` is how many bytes the image holds.

- The NGPC puts the cartridge's first ROM chip at CPU address 0x200000 (SNK's SysPro document,
  "Main CPU memory map", and its software header: "Placement address 0x200000 = software cassette
  ROM address 0x000000"). So byte 0 of a `.ngp` file is address 0x200000. A first chip holds at
  most 2 MB (0x200000-0x3FFFFF).
- `--origin` defaults to 0x200000 when the program is in that chip, else to its lowest address.
  `unitron dis` reads a `.ngp` file the same way, so it needs no option either.
- `--size` defaults to the program's last byte. Give it to pad the image to the cartridge's size;
  unused bytes become 0xFF (`--fill` changes that).
- RAM is at 0x4000-0x6BFF for the program (SysPro); sections placed there are not part of the
  image.

## Other commands

- **`unitron mac FILE`** runs only the macro preprocessor (`#include`, `#define`, `#if`,
  `?macro`) and prints the result.
- **`unitron bin2db FILE -o FILE.asm`** turns a binary file into `db` lines, to `$include` in a
  source (asm900 has no command for including a binary file).
- **`unitron text charmap.txt strings.txt -o strings.asm`** turns text into `db` lines through a
  character map (see "Text" below).
- **`unitron gfx`** converts the NGPC's tiles and palettes to and from PNG (see "Graphics" below).

## The language

`docs/language.md` describes the asm900 language as unitron implements it, `docs/encoding.md`
which encoding unitron picks when several are possible, `docs/lcf.md` link files, `docs/z80.md`
the Z80 side, and `docs/OPEN.md` what is not settled. The style unitron writes: lowercase
mnemonics, uppercase registers, `0x` hex numbers, `Routine.label` for labels local to a routine,
`jp XHL` without parentheses, `(XHL+:1)` for a step that differs from the operand size, and
`ld (wX:24),A` to force an address width.

## Graphics

The tile, map and palette formats are SNK's (K2GETechRef 4-3-5 tiles, 4-4-4 map entries, 4-15
colour palettes). A game's own formats (compression, image headers) belong in that game's
repository, as separate tools run after these.

```
unitron gfx topng tiles.bin sheet.png [--width 16] [--palette pal.bin --pal N] [--map map.bin]
unitron gfx frompng sheet.png tiles.bin [--count N] [--trim] [--map map.bin --count N]
unitron gfx frompng image.png pixels.bin --indices
unitron gfx palette image.png pal.bin [--colors 4]
```

- **Tiles** are 8x8 pixels, 2 bits per pixel, 16 bytes each. In the PNG a pixel is a palette
  index; the tile keeps `index & 3` (the shade). Index 4p + s is shade s in palette p (what
  `topng --palette` writes), so colouring a PNG never changes its tiles. `--pal N` (a plain
  sheet's palette) needs `--palette`; N or a map entry's palette past the end of the palette file
  is an error.
- **`frompng`** reads the cells of the sheet left to right, top to bottom: every cell, or the
  first `--count N`. A PNG written by `topng --map` carries its layout, and then every tile in it
  is read. With `--map`, `--count` is needed.
- **`--map`** lays the sheet out as a tile map shows it: sheet tile n is character code
  `base + n + 1` (`--base`, default 0), and code 0 is an empty cell. `--map-format` gives the map
  file's form: `wh8` (a width byte, a height byte, then the entries; the default, and the reference
  ROM's form) or `raw` (the entries only, with `--map-width`).
- **`--trim`** drops blank cells from the end of the last row of cells; that row's first cell
  always stays. A cell is blank when every pixel is palette index 0: a cell drawn only in index 4
  is kept, though its tile bytes are all zero. `--count` wins over `--trim`. Plain sheets only.
- **`--indices`** writes each pixel's palette index as one byte, left to right, top to bottom,
  with no tiles and no `& 3`; any width and height.
- **`palette`** writes the PNG's first N palette colours (default 4) as little-endian 16-bit words
  0x0BGR, 4 bits per channel. Each channel becomes the nearest of the 16 levels (round(c / 17)).
  Fewer colours than N is an error.

## Text

`unitron text` turns the lines of a text file into `db` lines with a character map. In the map,
`char "a" 01` gives a character's byte, and `cmd NAME XX [string]` names a command (`cmd text -
string`, `cmd done FF`). In both files a doubled quote inside a string is one quote character
(`char """" 22`, `text "say ""hi"""`). In the text file, `{XX}` is a byte with no character,
`Name:` lines are labels and `;` lines are comments. A **`slot WIDTH PAD`** line (`slot 7 FF`:
decimal width, hex byte) makes every string after it exactly WIDTH bytes, for a table of
fixed-size names:

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
end of the file. It is padded with PAD up to WIDTH; a longer one is an error with its line, byte
count and width. `slot 0` ends slot mode.

## Command reference

**`unitron asm`** options:

| Option | Meaning |
|---|---|
| `-o FILE` | the image to write |
| `--origin ADDR`, `--size N`, `--fill BYTE` | the image's first address, its size, and the byte for unused space (0-255, default 0xFF) |
| `-I DIR` | another directory to search for included files |
| `-D NAME=VAL` | define a preprocessor name |
| `--lcf FILE` | place sections with a link command file (`docs/lcf.md`) |
| `--place SECTION=ADDR` | place one section |
| `--data-base ADDR`, `--code-base ADDR` | where automatic placement starts (0x4000 and 0x200000) |
| `--no-link` | check one module alone (externals stay unresolved) |
| `--no-mpp` | skip the macro preprocessor |
| `--strict` | make asm900's include limits (8 deep, 99 per file) errors instead of warnings |
| `--sym FILE` | write a symbol file (address and name per line) |
| `-H` | also accept asm900's suffixed numbers (`1234H`, `101B`, `17O`, `99D`) |
| `-O0`, `-O1` | how `j`/`cal` choose a jump: `-O1` (default) the shortest form, `-O0` always `jp`/`call` |
| `-M FILE`, `-MG`, `-MP`, `-MT TARGET` | Make dependency output (below) |

Errors look like `file:line: error E217: ...`, with Toshiba's error numbers where one fits. No
output is written when there are errors.

**Make dependencies.** `-M FILE` (`-` for the screen) writes one line `TARGET: FILE` for every
file the assembly reads (the sources, `#include` and `$include` files, the `--lcf` file). The
target is the `-o` file, or `-MT TARGET`. `-MG` lists an include file that doesn't exist yet (one
the build will make) instead of stopping with an error; then nothing is assembled. `-MP` adds an
empty rule for each include file, so make doesn't fail when one is deleted. Without `-o`, `-M`
only reads what decides the includes; with no target either, it lists the files alone, one per
line, for a Makefile to read:

```
PRODUCTS := $(filter build/%,$(shell unitron asm src/main.asm -I . -M - -MG))
game.ngp: src/main.asm
	unitron asm src/main.asm -I . -o $@ -M build/game.d -MP
-include build/game.d
```

**`unitron dis` and `unitron roundtrip`** options: `-o DIR` (dis: write source instead of a
listing), `--hints FILE` (above; `docs/language.md`, "Trace hints"), `--executed FILE` (more
addresses where code starts, in hex), `--no-sweep` (only follow the code from its entry points; no
scanning of the rest). The scan covers the whole image after the header; past the code it has
traced it starts only from call targets and from address tables that also point at known code.

## Tests

`(cd build && ctest)` runs them all. `tests/README.md` says how to run one, add a case and rewrite
the expected outputs.

## Sources

All code here is ours (MIT, see LICENSE), except zlib and libpng, which the build downloads and
which keep their own licenses. These documents are cited:

- Toshiba's *TLCS-900 Assembler Reference* (the language, the macro preprocessor, link command
  files, the error numbers), the TLCS-900 CPU datasheet's instruction lists, Toshiba's sample
  programs, and its TMP95C061F register header IO900H.H (the CPU's on-chip I/O names).
- A static reading of Toshiba's assembler program asm900 (its tables, never run) for points the
  manual leaves open.
- SNK's NGPC developer documents (K2GETechRef, K2GEres, SysWork, SysPro, SysCall, SysLib, ...):
  the video chip's registers, system RAM, interrupt vectors, tile, map and palette formats, the
  cartridge header (copies in `docs/snk/`). `data/hwnames.tsv` holds every hardware name with
  its source; `docs/hardware-names.md` explains it.
- Zilog's *Z80 CPU User Manual* (UM0080) and *Z80 Assembly Language Programming Manual* (1978),
  for the `z80` commands (`docs/z80.md`).

Copies of some of these files are included, unchanged, so that the tests and the citations need
nothing outside the repository. They remain their owners' (Toshiba's, SNK's):

- `tests/toshiba/samples/`: Toshiba's sample programs SAMPLE1.ASM and SAMPLE2.ASM, assembled by
  the `cases-toshiba` test.
- `docs/toshiba/IO900H.H`: the register header, cited by line in `data/hwnames.tsv`.
- `docs/snk/`: SNK's developer documents as markdown with their images (for example
  [docs/snk/SysPro/SysPro.md](docs/snk/SysPro/SysPro.md)), cited by section; the `hwnames` test
  checks that every section `data/hwnames.tsv` cites is a heading there.

No emulator code, no emulator-derived names and no ROM data are in this repository.
