# unitron's tests

```
../.venv/bin/cmake -S . -B build && ../.venv/bin/cmake --build build -j8
(cd build && ../../.venv/bin/ctest)                # everything
(cd build && ../../.venv/bin/ctest -R cases-asm)   # one folder
build/unitron_tests cases tests build/unitron asm              # the same, by hand
build/unitron_tests cases tests build/unitron asm/labels-expr  # one case
build/unitron_tests cases tests build/unitron asm --write      # (re)write a folder's expected files
```

## All the tests

| ctest test | Checks | Needs a ROM |
|---|---|---|
| `cases-asm`, `cases-mpp`, `cases-link`, `cases-lcf`, `cases-dis`, `cases-gfx`, `cases-text`, `cases-bin2db`, `cases-z80`, `cases-cli` | the case folders below: one small file per behavior, exact stdout, stderr, exit code and output files | no |
| `opspace`, `lines` | TLCS-900: every opcode decoded and re-assembled; 346,448 instruction lines (frozen vectors in `vectors/`) | no |
| `datasheet` | encodings from the CPU documents (`datasheet/`): 202 TLCS-900 rows from Toshiba's datasheet and Assembler Reference, 184 Z80 rows from Zilog's UM0080 | no |
| `lcf` | the link command file parser and placement (tests/cpp/lcf.cpp) | no |
| `hwinc`, `hwnames` | `data/ngpc_hw.inc` is generated from `data/hwnames.tsv`; the table itself is well formed (below) | no |
| `z80opspace`, `z80shift` | Z80: every documented form; moving code moves every reference | no |
| `cart` | our own test cartridge (`cart/`): dis, asm, roundtrip three ways, the Z80 side | no |
| `rom`, `z80rom` | the reference ROM and the second ROM by hash | skipped without `$UNITRON_ROM` / `$UNITRON_ROM2` |

`unitron_tests TEST ... --write` rewrites a test's expected outputs after a deliberate change
(cases, opspace, lines, hwinc, z80opspace, z80rom, cart); review `git diff tests/` afterwards. The
`rom` hashes and the `datasheet` tables are edited by hand.

## Case folders

One small case per behavior, as in another toolchain's `test/` (the idea, not the code). Each folder is one
ctest test, run by `unitron_tests cases` (tests/cpp/cases.cpp).

| Folder | Cases | What | Default command |
|---|---|---|---|
| `asm/` | 141 | the asm900 assembler: directives, number forms, operators, `:n`, every error and warning, include limits, `-O0/-O1`, `--place/--origin/--size/--fill`, `-M/-MG/-MP/-MT` | `asm NAME.asm -o out.bin` |
| `mpp/` | 74 | the macro preprocessor: `#define` (`#`, `##`), `#if` family, `#include`, `?macro`, every mpp error | `mac NAME.asm` |
| `link/` | 18 | several modules: `public`/`extern`, widths, placement, overlaps (E217) | `asm *.asm -o out.bin` |
| `lcf/` | 44 | link command files (`--lcf`): every feature and check of docs/lcf.md | `asm *.asm --lcf *.lcf -o out.bin` |
| `dis/` | 32 | `dis` and `roundtrip`: listing, `-o` with labels, `--hints`, `--executed`, `--no-sweep`, the sweep's limits | `asm NAME.asm -o out.ngp --no-mpp`, then `dis out.ngp` |
| `gfx/` | 59 | `gfx topng`, `frompng`, `palette`: every option and error (tiny PNGs of our own) | none: every case has a `.cmd` |
| `text/` | 27 | `text`: character maps, commands, `slot`, every error | `text NAME.charmap NAME.txt` |
| `bin2db/` | 9 | `bin2db` | `bin2db NAME.bin` |
| `z80/` | 75 | the Z80 commands (docs/z80.md): every pseudo-op and error number, dis, asm, roundtrip, `--equ`/`--db` | `z80 asm NAME.z80 -o out.bin` |
| `cli/` | 24 | help texts, unknown commands and options, missing arguments | none |

`*.asm` stands for every `.asm` file at the top of the case's directory, sorted by name.

The 208 commands of the old golden test (tests/golden/cases.txt, deleted 2026-11-25) are 206 of
these cases; the commit that deleted it lists which case holds each command.

## The files of a case

A case is called NAME (letters, digits, `-` and `_`; no `.`). Everything about it starts with
`NAME.`:

| File | Meaning |
|---|---|
| `NAME.asm`, `NAME.z80`, `NAME.png`, ... | an input file: any `NAME.EXT` whose EXT has no `.` and isn't one of the names below |
| `NAME/` | a folder of input files, for a case with several files (modules, includes, a hints file); its contents are copied as they are |
| `NAME.flags` | options added to the end of the folder's default command |
| `NAME.cmd` | the commands to run instead of the default, one per line (the arguments after `unitron`); every command but the last must succeed |
| `NAME.out` | the exact stdout (of all the commands, in order) |
| `NAME.err` | the exact stderr |
| `NAME.exit` | the exit code of the last command, when it isn't the default: 1 if `NAME.err` has a line containing `error`, else 0 (usage errors are 2) |
| `NAME.out.X` | a file the commands wrote: `out.X` is compared with `NAME.out.X`, `out/a/b` with `NAME.out.a.b`, any other file F with `NAME.out.F` |

A missing `.out` or `.err` means "nothing"; a written file with no `NAME.out.X`, or a
`NAME.out.X` that wasn't written, is a failure. In `.flags` and `.cmd`, `#` starts a comment
line, arguments are split at white space, and `'...'` or `"..."` quote. Name the files a command
writes `out.*` (`-o out.bin`, `--sym out.sym`, `-o out` for a directory) so that the expected
files are `NAME.out.bin`, `NAME.out.sym`, `NAME.out.main.asm`.

The runner copies the inputs into a new, empty directory, runs the commands there and compares
the results byte for byte (on Windows, CRLF in stdout and stderr is read as LF). Test inputs are
our own small files: no ROM bytes, no third-party text. tests/.gitattributes turns off line-end
conversion so the bytes stay exact.

## Adding a case

1. Write the input, e.g. `tests/asm/my-thing.asm`, and if needed `my-thing.flags` or
   `my-thing.cmd`.
2. `build/unitron_tests cases tests build/unitron asm/my-thing --write` writes its expected files.
3. Read them (`git status tests/`, `git diff tests/`): they are the behavior being frozen.

`--write` on a folder rewrites every case in it and removes `NAME.out.X` files that are no longer
written; after a deliberate behavior change, run it and review `git diff tests/`.

## hwnames

`unitron_tests hwnames data` reads `data/hwnames.tsv` (not the copy built into the binary) and
checks: every record has its fields and a known kind; names (registers and bits together) and
addresses are unique, and entries are sorted by address; every entry cites its source in the
table's form (`IO900H.H l.N` for the CPU's I/O registers, `SNK DOC PLACE[; [DOC] PLACE]` for the
rest); the naming rule of docs/hardware-names.md (uppercase, the official spelling with `.` as
`_`, multi-word SNK names with a leading digit word moved last, `VEC_` vectors, bit names);
bits name a register in the table, within its width; no name is an asm900 reserved word; each
vector is 4 bytes, 4-aligned in 0x6FB8..0x6FFC, and the reserved 0x6FEC has none; the embedded
copy equals the file.

## datasheet

`datasheet/tlcs900.txt` and `datasheet/z80.txt` are our own tables: one line per encoding,
`TEXT <tab> BYTES <tab> SOURCE`, with the document page and the line of the text extract
(docs/third_party/, not in git). The bytes come from the documents, not from unitron: for the
TLCS-900 they were computed by hand from the datasheet's code templates and the CPU manual's code
tables; for the Z80 they are the object code of UM0080's listings and its instruction pages'
examples. A fourth field `differs: XX` records a known disagreement; the check reports it and
fails only if unitron's bytes change. The four now:

- `ldar XHL,...` and `ldar HL,...` (datasheet p.159 l.71): the template read literally gives
  `40+R` (long) and `30+R` (word); unitron writes `30+R` and `20+R`.
- `mul WA,B` (datasheet p.162 l.196): the code of WA is 0 (`CA 40`); unitron writes the odd code
  (`CA 41`), which also means WA (as docs/encoding.md rule 5 does for `mul WA,5`).
- `JR NC,$` at 480 (UM0080 p.270): the manual's table prints `30 00`; its own `e-2` rule gives
  `30 FE`, which unitron writes. A misprint in the manual.

## Known issues

Cases that freeze behavior that looks wrong, as it is now (the case says `BUG` or `BUG?`); none is
fixed here:

- `mpp/define-paste` (also `mpp/basics`): `a ## b` writes `a #"b"` instead of pasting the tokens.
- `asm/err-dfb-string` (also `asm/errors-misc`): `dfb 1,"ab"` reports an internal error "size
  changed from 1 to 0 bytes between passes" instead of E231 "dfb takes one value".
- `asm/ds-count-forward-equ`: a `dfb` count that is an `equ` defined later is accepted;
  docs/language.md says the count takes no forward reference.
- `asm/err-missing-file` (also `cli/asm-option-prefix`): a missing source file also gets the
  warning "nosuch.asm:0: warning W506: no `end` directive".
- `dis/err-executed-bad`: a bad address in the `--executed` file reports "error: stoll" with no
  file or line.
- `text/err-charmap-missing`, `text/err-text-missing`: a missing file is reported in Python's
  words ("[Errno 2] No such file or directory: ..."); `bin2db/err-per-line-zero`: "range() arg 3
  must not be zero"; `cli/place-bad-address`: "invalid _place value" names an internal option name.
- `text/err-quote-in-string` (BUG?; also `text/basic` and `text/output-file`, whose text has
  such a line, so they only check that error): the charmap reads `""""` as a quote character,
  but a doubled quote in a text line is "not a text command".
- `lcf/attr-overlay` (BUG?): docs/lcf.md says OVERLAY sections may overlap; the .lcf check allows
  it, but the assembler writes both sections' bytes and reports E217.
- `gfx/topng-pal-past-file` (BUG?): `--pal N` beyond the palette file is not an error; the sheet
  silently uses palette 0.
- `dis/listing-ignores-hints`: a listing (no `-o`) does not read `--hints` (not even a missing
  file); the hints only shape `-o` output.

Messages no case triggers, because the CLI can't reach them: E231 "dfb takes one value" (see
above), "operand size unknown; add a B/W/L suffix" (a size-less operand is byte), "CALR has no
condition" (the operand check stops first), "string ends in a backslash" (a line-end backslash
joins lines, and an unterminated string is found first), and the messages of `assemble_line`
used only by the tests. The .lcf parser's syntax errors are checked in tests/cpp/lcf.cpp.
