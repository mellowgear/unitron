# unitron's tests

How to run the tests, how a case is written, and how to add one. The list of tests is in
`CMakeLists.txt` and `tests/cpp/main.cpp`; each folder here is one ctest test.

## Running

```
cmake -S . -B build && cmake --build build -j8
(cd build && ctest)                          # everything
(cd build && ctest -R cases-asm)             # one test
build/unitron_tests cases tests build/unitron asm              # the same, by hand
build/unitron_tests cases tests build/unitron asm/labels-expr  # one case
build/unitron_tests cases tests build/unitron asm --write      # (re)write a folder's expected files
```

`rom` and `z80rom` check commercial ROMs by hash only, and skip what is absent: `rom` needs
`$UNITRON_ROM` to name the reference ROM; `z80rom` uses `$UNITRON_ROM` and/or `$UNITRON_ROM2`.
Every other test needs nothing outside the repository.

`unitron_tests TEST ... --write` rewrites a test's expected outputs after a deliberate change
(cases, opspace, lines, hwinc, z80opspace, z80rom, cart); review `git diff tests/` afterwards. The
`rom` hashes and the `datasheet` tables are edited by hand.

## Cases

One small case per behavior, run by
`unitron_tests cases` (tests/cpp/cases.cpp). Each folder's default command:

| Folder | Default command |
|---|---|
| `asm/` | `asm NAME.asm -o out.bin` |
| `mpp/` | `mac NAME.asm` |
| `link/` | `asm *.asm -o out.bin` |
| `lcf/` | `asm *.asm --lcf *.lcf -o out.bin` |
| `dis/` | `asm NAME.asm -o out.ngp --no-mpp`, then `dis out.ngp` |
| `gfx/` | none: every case has a `.cmd` |
| `text/` | `text NAME.charmap NAME.txt` |
| `bin2db/` | `bin2db NAME.bin` |
| `z80/` | `z80 asm NAME.z80 -o out.bin` |
| `toshiba/` | none: every case has a `.cmd` |
| `cli/` | none |
| `lsp/` | none: every case has a `.cmd` (`lsp --script FILE`, docs/editor.md) |

`*.asm` stands for every `.asm` file at the top of the case's directory, sorted by name.

### The files of a case

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
our own small files: no ROM bytes, no third-party text, except `toshiba/`, which runs Toshiba's
sample programs SAMPLE1.ASM and SAMPLE2.ASM (`toshiba/samples/`, copied unchanged).
tests/.gitattributes turns off line-end conversion so the bytes stay exact.

### Adding a case

1. Write the input, e.g. `tests/asm/my-thing.asm`, and if needed `my-thing.flags` or
   `my-thing.cmd`.
2. `build/unitron_tests cases tests build/unitron asm/my-thing --write` writes its expected files.
3. Read them (`git status tests/`, `git diff tests/`): they are the behavior being frozen.

`--write` on a folder rewrites every case in it and removes `NAME.out.X` files that are no longer
written. To freeze behavior that looks wrong, say so in the case's comment with `BUG` (or `BUG?`
when it is a question).
