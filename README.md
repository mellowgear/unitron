# unitron

A toolchain for Neo Geo Pocket Color (TLCS-900/H) decompilation and development, written in C++17.
Its source language is Toshiba's asm900 syntax, so the same source reads like the original
developers' and assembles with unitron byte for byte.

- `unitron asm`: asm900 assembler and linker (sections, `org`, `equ`, `db/dw/dp/dl`, `dfb..`,
  `dsb..`, `public/extern`, `$include`, the `:n` width operator) with Toshiba's macro
  preprocessor subset (`#include`, `#define`, `#if`, `?macro ... ?endm`) and a subset of
  Toshiba's link command files (`--lcf`). Output: a raw image.
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

## Build and install

Needs CMake 3.24 or later and a C++17 compiler. The first configure downloads zlib 1.4 and
libpng 1.6.59 (pinned by SHA256, built static; `cmake/deps.cmake`), so it needs internet. If the
libpng download fails its TLS check (the SourceForge mirror does this sometimes), run the
configure again. `-DUNITRON_SYSTEM_DEPS=ON` uses installed copies instead.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
(cd build && ctest)                   # the rom test skips itself without the the reference ROM ROM
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
```

`unitron asm` options: `-I DIR`, `-D NAME=VAL`, `--origin`, `--size`, `--fill` (0-255),
`--place SECTION=ADDR`, `--lcf FILE` (docs/lcf.md), `--data-base`, `--code-base`, `--no-link`
(check one module), `--no-mpp`, `--strict` (asm900's include limits are errors), `--sym FILE`,
`-H` (asm900's suffixed numbers), `-O0`/`-O1` (how `j`/`cal` choose, as asm900's `-O`; default
`-O1`). Errors look like `file:line: error E217: ...` with Toshiba's error numbers where one fits;
no output is written when there are errors. A 1 MB ROM round trip (dis and asm) takes under a
second.

`dis`, `roundtrip` and `lift` take `--executed FILE` (extra code entry points, hex addresses)
and `--hints FILE`, which makes the gap sweep reproducible (docs/language.md, "Trace hints"):

```
# comments start with # or ;
code 200185 hidden_fn     ; trace from here (name optional, default code_200185)
data 20018C 200191        ; no swept instruction touches [start, end)
```

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

## Tests

`tests/cpp/` checks the C++ build against vectors frozen from the original Python implementation:
the whole opcode space decoded and re-assembled, 346,448 instruction lines, the CLI golden cases
(`tests/golden/`), the `.lcf` cases, `data/ngpc_hw.inc` against `data/hwnames.tsv`, and the
the reference ROM round trip by hash. `unitron_tests <test> ... --write` rewrites the expected outputs after
a deliberate change; review `git diff tests/` afterwards.

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
- Haruhiko Okumura's LZSS.C (1989, public domain): the compressor's algorithm and parameters.

No emulator code, no emulator-derived names, no Toshiba or SNK files, and no ROM data are in this
repository.

The original Python implementation is kept in `legacy/python/` as the frozen reference
(legacy/python/README.md); it will be deleted once nothing needs it.
