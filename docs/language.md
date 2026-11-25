# The language unitron accepts

Toshiba's asm900 syntax, from the *TLCS-900 Assembler Reference* tde122-01_96 ("TR", printed page
numbers), the TLCS-900 CPU datasheet instruction lists ("DS"), Toshiba's SAMPLE1.ASM/SAMPLE2.ASM,
and the static reading of asm900's own tables in `asm900-analysis/ASM900_STATIC.md` ("STATIC").
Where these disagree, STATIC wins for asm900's behavior; docs/OPEN.md lists every such point.

## Lines

- `[label[:]] mnemonic|directive [operands] [; comment]`; one label per line. A label at column 0
  may omit the colon (`counter dsw 2`, SAMPLE1 (24)); an indented name is a label only with a
  colon, or before `equ`/`section`/`d*`/`df*`/`ds*` [TR p.10-11].
- Comments start with `;` outside `"strings"` and `'char constants'` [TR p.11]. A `'` right after
  an identifier character belongs to the identifier (`WA'`, `F'`) [STATIC 3.4].
- Reserved words (mnemonics with or without a B/W/L suffix, registers, conditions, directives and
  the extra keywords of STATIC 0) are case-insensitive and cannot be symbols. Identifiers are
  case-sensitive: letters, digits, `_`, `.`, `'`, not starting with a digit [TR p.11]. Names that
  agree in their first 32 characters get a warning (V2.2d keeps 32) [STATIC 3.4].
- Local labels are plain identifiers: `Routine.loop` (D2).

## Expressions [TR p.12-16]

Numbers: `0x1F` hex, `0y101` binary, leading `0` octal, else decimal; `'AX'` char constants (up to
4 bytes, big-endian, TR p.10 escapes). `0b101` and `1234H` are errors, as in asm900; `unitron asm
-H` accepts `1234H`, `101B`, `17O`/`17Q`, `99D` like asm900's `-H` [STATIC 3.2]. `$` is the
location counter.

Precedence, high to low: unary `- ~ +`, `sizeof(sect)` `startof(sect)`; `* / %`; `+ -`; `>> <<`;
`&`; `^`; `|`; then the width operator `expr:n` (asm900 grammar rule 78, not in TR): the value
masked to n bits and encoded at least n bits wide [STATIC 3.7]. Values are 32-bit signed.

## Directives [TR p.17-25]

| Directive | Notes |
|---|---|
| `$maximum` | required first statement |
| `module name` | once per file |
| `name section code\|data\|romdata [small\|medium\|large] [abs=expr] [align=e1[,e2]]` | relocatable (default `large`) or absolute; `small` only for data; a repeated name continues the section |
| `org expr` | absolute address in an abs section, offset in a relocatable one; never backwards (E229) |
| `align expr` | rounds the location counter up |
| `name equ expr` | absolute; no redefinition |
| `public a,b` / `extern [small\|medium\|large] a,b` | inter-module names; the extern width fixes the address width |
| `db dw dp dd dl` | 8/16/24/32/32-bit values; `db` also takes strings (no `\0` added). In a `data` section (RAM) they give a warning: the bytes are not in the image; use `ds*` to reserve RAM, a `romdata` section for constants, or an `.lcf` `org=` in ROM (docs/lcf.md) |
| `dfb dfw dfp dfd dfl n,v` | n copies of v; n absolute, no forward reference; the same `data`-section warning as `db` |
| `dsb dsw dsp dsd dsl n` | n units reserved (0xFF, or `--fill`, in the image) |
| `$include "file"` | assembler-level include: source dir, `-I`, `$THOME900/include`. asm900's limits, 8 levels deep and 99 per translation unit [TR p.24, 32], give a warning; with `--strict` they are error E234. Nesting deeper than 64 (a recursive include) is always E234 |
| `$list $nolist $title $eject $save $restore` (+ short forms) | listing only; ignored |
| `end` | ends the module; later text is ignored with a warning |

Not supported: `fdatas/d/x`, `file`, `line`, `symbol_*` (debug output of the C compiler),
`$minimum`.

## Instructions

Lowercase mnemonics and uppercase registers are the style unitron writes (D6); any case is
accepted. Operand spellings:

| Form | Example |
|---|---|
| register | `A`, `WA`, `XWA`, `RA3`, `QW`, `XIX'`, `QIX` |
| immediate | `5`, `Label & 0xFFFF`, `2:16` |
| memory | `(XHL)`, `(XHL+4)`, `(XHL-4)`, `(XHL+0)`, `(XHL+DE)`, `(XHL+A)`, `(0x6F82)`, `(label:24)` |
| pre-decrement / post-increment | `(-XSP)`, `(XHL+)`, step written when it differs from the operand size: `(XHL+:1)`, `(-XSP:4)` |
| block transfer | `ldir (XDE+),(XHL+)`, `lddr (XDE-),(XHL-)`, `cpir A,(XHL+)` |
| JP/CALL/LDA address (no parentheses) | `jp XHL`, `jp C,XIX+2`, `call label`, `lda XHL,XHL+DE`; `jp (XHL)` is an error |
| relative | `jr NZ,label`, `jrl label`, `calr label`, `djnz BC,label` |
| size suffix | `ldb (mem),n`, `ldw`, `cpb`, `addw`, `incb 1,(mem)`, `pushw n`, `ldirw`; a suffix must agree with any register operand; with no register the size is byte (`push 5` = `pushb 5`) |
| counts | `inc 1,C` (count required), `sll 1,HL`, `sll A,HL`, memory shifts take no count: `sllw (mem)` |
| conditions | `F LT LE ULE OV MI Z C T GE GT UGT NOV PL NZ NC`, also `EQ NE ULT UGE M P PE PO` |
| pseudo | `j [cc,]addr`, `cal [cc,]addr` [TR p.16-17, 29]: with `-O1` (the default) the shortest of jr/jrl/jp and calr/call, for an absolute target or a label (± a constant) in the same section; other relocatable or extern targets take jp/call. With `-O0` always jp/call. docs/encoding.md rule 9 |

## Macro preprocessor (`#include`, `?macro`) [TR p.57-76]

`unitron asm` runs it first (`--no-mpp` turns it off); `unitron mac` runs it alone.

- `#include "f"` / `<f>`, `#define` (object and function-like, `#` and `##`), `#undef`,
  `#if #ifdef #ifndef #elif #else #endif` (C operators, `defined`), `#error`, `#line` (ignored).
  Identifiers for `#define` are `[A-Za-z_][A-Za-z0-9_]*`. A dotted name (`Routine.loop`, `.loop`)
  is one asm900 identifier [STATIC 3.4] and is copied whole: a `#define` of `loop` does not change
  it.
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
  ?repeat ?while ?variable ?eval ?exit`, the string functions, ...) is an error: `?repeat is not
  supported (only ?macro/?endm)`.

D3: hand-written sources join with `#include` (so macros work everywhere); generated data files
(`bin2db` output) join with `$include`.

## Linking

Every file on the command line is one module. Absolute sections stay at `abs=`; relocatable
sections with the same name are joined in module order and placed in one of two ways:

- `--lcf FILE`: a Toshiba link command file, as for tulink (a subset; docs/lcf.md). It can also
  store an initialized `data` section in ROM (`org=`) and run it from RAM (`addr=`).
- Otherwise unitron places them: `--place NAME=ADDR`, else first fit from `--data-base` (data,
  default 0x4000) or `--code-base` (code/romdata, default 0x200000), or from 0x100 if that would
  not fit the section's small/medium/large range.

Overlapping sections are error E217, which names both source lines. `--no-link` checks one module
with unresolved externs.

## Trace hints (`--hints FILE` on `dis`, `roundtrip`, `lift`)

The trace follows code from the cartridge header's entry point, then from the `--executed`
addresses (hex, separated by white space) and the hints' `code` lines; then a gap sweep decodes
what is left. The sweep is a heuristic, so a hints file pins its result down:

```
# comments start with # or ;
code 200185 hidden_fn     ; a trace entry point (name optional: code_200185)
data 20018C 200191        ; no swept instruction touches [start, end)
```

Addresses are hex, with or without `0x`. A bad line is an error with its line number. Without
`--hints` the output is unchanged.
