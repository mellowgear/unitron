# The language unitron accepts

This is the asm900 language for the TLCS-900/H as `unitron asm` reads it and `unitron dis` writes
it: lines, expressions, directives, operand spellings, the macro preprocessor, linking, and the
disassembler's trace hints and labels. Which bytes an instruction becomes is in `encoding.md`;
link command files are in `lcf.md`; unsettled points are in `OPEN.md`.

Sources, cited in brackets:

- **TR**: Toshiba *TLCS-900 Assembler Reference* (tde122-01_96), printed page numbers.
- **DS**: the instruction lists of the Toshiba TLCS-900/L1 CPU datasheet.
- Toshiba's sample programs SAMPLE1.ASM and SAMPLE2.ASM (copies in `tests/toshiba/samples/`).
- **asm900**: Toshiba's asm900.exe (V2.2k, compared with V2.2d), read statically from its tables
  and code, never run. Where the manual is silent or disagrees, unitron follows asm900.

## Lines

- `[label[:]] mnemonic|directive [operands] [; comment]`; one label per line. A label at column 0
  may omit the colon (`counter dsw 2`, SAMPLE1 (24)); an indented name is a label only with a
  colon, or before `equ`/`section`/`d*`/`df*`/`ds*` [TR p.10-11].
- Comments start with `;` outside `"strings"` and `'char constants'` [TR p.11]. A `'` right after
  an identifier character belongs to the identifier (`WA'`, `F'`) [asm900].
- Reserved words are case-insensitive and cannot be symbols: mnemonics with or without a B/W/L
  suffix, registers, conditions, control registers, directives, and asm900's other keywords `F`,
  `F'`, `SR`, `CODE`, `DATA`, `ROMDATA`, `SIZEOF`, `STARTOF`, `INCLUDE`, `MAX`, `MIN`, `LIST`,
  `LI`, `TITLE`, `TT`, `SA`, `RS`, `EJ`, `NOR`, `SYS`, `BANK`, `LOC0`, `COM0`, `SMALL`, `MEDIUM`,
  `LARGE`, `ABS` (asm900 looks keywords up before the symbol table).
- Identifiers are case-sensitive: letters, digits, `_`, `.`, `'`, not starting with a digit
  [TR p.11]. Names that agree in their first 32 characters get a warning: asm900 V2.2k keeps 1024
  characters, V2.2d keeps 32 [asm900].
- Local labels are plain identifiers: `Routine.loop`.

## Expressions [TR p.12-16]

Numbers: `0x1F` hex, `0y101` binary, leading `0` octal, else decimal; `'AX'` char constants (up to
4 bytes, big-endian, TR p.10 escapes). `0b101` and `1234H` are errors, as in asm900 (TR p.8
documents `0b`, but asm900's lexer rejects it, E202). `unitron asm -H` accepts `1234H`, `101B`,
`17O`/`17Q`, `99D`, like asm900's `-H` [asm900]. `$` is the location counter.

Precedence, high to low: unary `- ~ +`, `sizeof(sect)` `startof(sect)`; `* / %`; `+ -`; `>> <<`;
`&`; `^`; `|`; then the width operator `expr:n`. The width operator is in asm900's grammar (rule
78), not in TR: the value is masked to n bits and encoded at least n bits wide [asm900]. There are
no word operators (`MOD NOT AND OR XOR` are not source operators in asm900). Values are 32-bit
signed.

## Directives [TR p.17-25]

| Directive | Notes |
|---|---|
| `$maximum` | required first statement |
| `module name` | once per file |
| `name section code\|data\|romdata [small\|medium\|large] [abs=expr] [align=e1[,e2]]` | relocatable (default `large`) or absolute; `small` only for data; a repeated name continues the section |
| `org expr` | absolute address in an abs section, offset in a relocatable one; never backwards (E229) |
| `align expr` | rounds the location counter up; no forward reference (E223) |
| `name equ expr` | absolute; no redefinition |
| `public a,b` / `extern [small\|medium\|large] a,b` | inter-module names; the extern width fixes the address width |
| `db dw dp dd dl` | 8/16/24/32/32-bit values; `db` also takes strings (no `\0` added). In a `data` section (RAM) they give a warning: the bytes are not in the image; use `ds*` to reserve RAM, a `romdata` section for constants, or an `.lcf` `org=` in ROM (`lcf.md`) |
| `dfb dfw dfp dfd dfl n,v` | n copies of v; `dfb n,"str"` writes the string n times (TR p.21; asm900 V2.2k's dfb handler, at 0x41bbf0, sizes it as length × n and its pass 3 repeats the string's bytes, listing "(repeat %ld)"). n is absolute with no forward reference: a symbol defined on a later line is error E223 (TR p.22; asm900 V2.2k checks this for ds*, codes 0x149-0x14c at 0x406bad, and for df*, `equ` and `org`, codes 0x143, 0x145-0x147, 0x132, 0x102 at 0x409408). The same `data`-section warning as `db` |
| `dsb dsw dsp dsd dsl n` | n units reserved (0xFF, or `--fill`, in the image; `--fill` takes a byte, 0-255, and another value is an error); n as for `dfb` |
| `$include "file"` | assembler-level include: source dir, `-I`, `$THOME900/include`. asm900's limits, 8 levels deep and 99 per translation unit [TR p.24, 32], give a warning; with `--strict` they are error E234. Nesting deeper than 64 (a recursive include) is always E234 |
| `$list $nolist $title $eject $save $restore` (+ short forms) | listing only; ignored |
| `end` | ends the module; later text is ignored with a warning. A missing `end` is warning W506 (TR p.18 calls it an error; TR's error list makes it warning 506, "treated as if the directive exists") |

`equ` and `org` still accept forward references. Not supported: `fdatas/d/x`, `file`, `line`,
`symbol_*` (debug output of the C compiler), `$minimum`.

## Instructions

unitron writes lowercase mnemonics and uppercase registers; any case is accepted. Operand
spellings:

| Form | Example |
|---|---|
| register | `A`, `WA`, `XWA`, `RA3`, `QW`, `XIX'`, `QIX` |
| immediate | `5`, `Label & 0xFFFF`, `2:16` |
| memory | `(XHL)`, `(XHL+4)`, `(XHL-4)`, `(XHL+0)`, `(XHL+DE)`, `(XHL+A)`, `(0x6F82)`, `(label:24)` |
| pre-decrement / post-increment | `(-XSP)`, `(XHL+)`, step written when it differs from the operand size: `(XHL+:1)`, `(-XSP:4)` |
| block transfer | `ldir (XDE+),(XHL+)`, `lddr (XDE-),(XHL-)`, `cpir A,(XHL+)` |
| JP/CALL/LDA address (no parentheses) | `jp XHL`, `jp C,XIX+2`, `call label`, `lda XHL,XHL+DE`; `jp (XHL)` is an error (asm900 accepts only the bare form) |
| relative | `jr NZ,label`, `jrl label`, `calr label`, `djnz BC,label`; `ldar XWA,label` and `ldar XWA,$+0x10` both work (offset from the instruction + 4; DS shows only `ldar R,$+4+d16`) |
| size suffix | `ldb (mem),n`, `ldw`, `cpb`, `addw`, `incb 1,(mem)`, `pushw n`, `ldirw`; a suffix must agree with any register operand; with no register the size is byte (`push 5` = `pushb 5`, as in asm900) |
| counts | `inc 1,C` (count required), `sll 1,HL`, `sll A,HL`, memory shifts take no count: `sllw (mem)` |
| omitted operands | `ei` = `ei 0` and `swi` = `swi 7` (DS notes); `djnz label` = `djnz B,label` (DS names no default register) |
| `ldf n` | 0-7, as asm900's #3 field allows (the 900/H has 4 banks) |
| conditions | `F LT LE ULE OV MI Z C T GE GT UGT NOV PL NZ NC`, also `EQ NE ULT UGE M P PE PO` |
| pseudo | `j [cc,]addr`, `cal [cc,]addr` [TR p.16-17, 29]: with `-O1` (the default) the shortest of jr/jrl/jp and calr/call, for an absolute target or a label (± a constant) in the same section; other relocatable or extern targets take jp/call. With `-O0` always jp/call. `encoding.md` rule 9 |

## Macro preprocessor (`#include`, `?macro`) [TR p.57-76]

`unitron asm` runs it first (`--no-mpp` turns it off); `unitron mac` runs it alone. It is a subset
of the tumpp V2.02 preprocessor that TR documents in Part 4 (asm900's 1998 kit used the older
TUMPL V1.0j, which has no `?macro`).

- `#include "f"` / `<f>`, `#define` (object and function-like, `#` and `##`), `#undef`,
  `#if #ifdef #ifndef #elif #else #endif` (C operators, `defined`), `#error`, `#line` (ignored).
  `a ## b` joins two tokens as in C (`CAT(1,2)` is `12`); an argument next to `##` is used as
  written, not macro-expanded. `##` at the start or end of a body is an error (TR p.107, errors
  282 and 283).
- Identifiers for `#define` are `[A-Za-z_][A-Za-z0-9_]*`. A dotted name (`Routine.loop`, `.loop`)
  is one asm900 identifier and is copied whole: a `#define` of `loop` does not change it.
- `;` comments are kept, `//` and `/* */` are removed, `\` at line end joins lines.
- Macros:

  ```
  ; task_wait (macro): expands to "R: ld XWA,R / ... / ret NZ", R = this resume point
  ?macro task_wait
  labels resume
  ?resume:
  	ld	XWA,?resume
  	...
  ?endm
  ```

  A call is the macro name in the mnemonic position (an optional `label:` before it), arguments
  separated by commas. `?param` and `?label` are replaced in the body; each `labels` name gets a
  3-digit count per expansion (`resume000`, `resume001`, ... at most 1000) [TR p.65-66].
- Only `?macro`/`?endm` are implemented. Every other `?` function outside a macro body (`?if
  ?repeat ?while ?variable ?eval ?exit`, the string functions of TR chapter 15, ...) is an error:
  `?repeat is not supported (only ?macro/?endm)`.

Convention: hand-written sources join with `#include` (so macros work everywhere); generated data
files (`bin2db` output) join with `$include`.

## Linking

Every file on the command line is one module. Absolute sections stay at `abs=`; relocatable
sections with the same name are joined in module order and placed in one of two ways:

- `--lcf FILE`: a Toshiba link command file, as for tulink [TR ch. 8] (a subset; `lcf.md`). It can
  also store an initialized `data` section in ROM (`org=`) and run it from RAM (`addr=`).
- Otherwise unitron places them: `--place NAME=ADDR`, else first fit from `--data-base` (data,
  default 0x4000) or `--code-base` (code/romdata, default 0x200000), or from 0x100 if that would
  not fit the section's small/medium/large range.

`extern large X` may name a symbol in another module's abs section; references take the extern
width (asm900 allows it; tulink's view is unknown, `OPEN.md`). Overlapping sections are error
E217, which names both source lines. `--no-link` checks one module with unresolved externs.

## Trace hints (`--hints FILE` on `dis`, `roundtrip`)

The trace follows code from the cartridge header's entry point, then from the `--executed`
addresses (hex, separated by white space) and the hints' `code` lines; then a gap sweep decodes
what is left. The sweep is a heuristic, so a hints file pins its result down:

```
# comments start with # or ;
code 200185 hidden_fn     ; a trace entry point (name optional: code_200185)
data 20018C 200191        ; no swept instruction touches [start, end)
```

Addresses are hex, with or without `0x`. A bad line is an error with its line number. Without
`--hints` the output is unchanged. A `dis` listing (no `-o`) is linear from the entry point or
`--start`, but it reads the hints too: a `code` line's name labels its address, and a `data` range
is listed as `db` lines of up to 4 bytes instead of being decoded (`--executed` addresses get
their `dyn_` labels).

The sweep covers the whole image that can hold code: from the end of the 0x40-byte header to the
end of the image (on a 4 MB cartridge, to the end of the first 2 MB chip, the part `dis -o` writes
as code). Between the first and the last traced instruction it tries every candidate: call
targets, the bytes after a block ends, and 4-byte values that point there. Outside that range data
is far more common than code, so it only starts from stronger evidence:

- a call target in code the sweep has accepted, or
- a pointer in a table: a run of 4-byte values in the image range, at least one of which points
  at a labelled routine found already, the value itself not inside code.

The bytes after a block ends are not tried there, and a block found there may branch anywhere in
the image. On the reference ROM, trying every candidate outside the range would add 400 blocks,
all of them data; these rules add none. Labels in swept code are guesses, though the bytes are
always reproduced.

### Placeholder labels in `dis -o` output

The disassembler can't know names, so it writes placeholders built from the address (6 hex
digits):

- `start`: the header's entry point; `irq_NAME`: a handler the code installs in a vector;
  hint `code` lines: the hint's name, or `code_XXXXXX`; `--executed` entries: `dyn_XXXXXX`.
- `sub_XXXXXX`: a call target; `loc_XXXXXX`: any other jump target.
- `swp_XXXXXX`: where a block the gap sweep found starts (unless code just before it runs into it).

Every jump or call target that is an instruction start in the written source has a label: one
with none of the above gets `sub_` (called) or `loc_` (only jumped to), so no branch into the code
is a bare number and the code can be moved. A target in bytes written as data, or in the middle of
an instruction, keeps its number (on the reference ROM, 55 calls and jumps from swept code into
bytes the sweep rejected as code).
