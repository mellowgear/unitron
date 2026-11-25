# Link command files (.lcf)

unitron can read a subset of the link command file language of Toshiba's linker `tulink`.
You can use it to place relocatable sections, instead of `--place NAME=ADDR` or the automatic
first fit. Section numbers below ("TR 8.3.4") refer to chapter 8 of the TLCS-900 Assembler
Reference (`docs/third_party/tr.txt`). The code is in `src/lcf.hpp` and `src/lcf.cpp`.

## Example

```
memory {
    ram (RW) : org=0x4000, len=0x2000
    rom (RX) : org=0x200000, len=0x200000
}
sections {
    user_ram addr=0x5000 : { *(f_area) }
    head     addr=0x200000 : { *(f_head) }
    code     addr=addr(head)+sizeof(head) : { crt.rel(f_code) *(f_code) }
    rodata   addr=addr(code)+sizeof(code) : { *(f_const) }
    roinit   org=org(rodata)+sizeof(rodata)          /* stored in ROM ...      */
             addr=org(user_ram)+sizeof(user_ram)     /* ... used from RAM      */
             : { *(f_data) }
    __DataRAM = addr(roinit);
    __DataROM = org(roinit);
    __DataLen = sizeof(roinit);
}
```

## Supported

- **Comments**: `/* ... */`, which do not nest (TR 8.1.8). `//` line comments also work; they
  are an extension.
- **Reserved words** are case-insensitive (TR 8.1.1). Other names are case-sensitive, except the
  predefined memory names (TR 8.1.2).
- **Numbers** (TR 8.1.3): `0x` hexadecimal, `0y` binary, a leading `0` for octal, otherwise
  decimal. All values are unsigned 32-bit, and bits above bit 31 are dropped.
- **Operators**: the operators and precedence of TR Table 8-1, as in C:
  - unary `! ~ -`
  - `* /`, then `+ -`
  - `<< >>`
  - `< > <= >=`, then `== !=`
  - `&`, then `|`, then `&&`, then `||`

  Comparisons give 1 or 0, and division by zero is an error.
- **Functional operators** (TR 8.1.6):
  - `addr(S)`: the run address of output section S.
  - `org(S)`: the load (image) address of S.
  - `sizeof(S)`: the size of S.
  - `align(N)`: the location counter, rounded up to a multiple of N.
  - `next(N)`: the first multiple of N at or after `.` that no section occupies.
- **Memory definition part** (TR 8.2): `NAME [(ATTR)] : org=EXPR [,] len=EXPR`.
  - You can write `origin` for `org` and `length` for `len`.
  - The attributes are uppercase `R W X I`. Without them, an area has all four.
  - Areas must not overlap (error 168). The exception is predefined areas of the same type
    (TR 8.2.3).
  - The predefined memory names are `code.m code.l data.s data.m data.l romdata.m romdata.l`.
    A section without an address goes to the predefined area for its type and displacement
    first, then to the other predefined areas of that type.
- **Section definition part** (TR 8.3):
  `NAME [org=E] [addr=E] [align=E | align(E)] [len=E] [(DSECT|COPY|NOLOAD|OVERLAY)] : { inputs } [=PAD] [> MEMORY]`.
  - `org` is the allocation address, which is where the section is stored in the ROM image.
    `addr` is the start address, which is where the section runs. If you give only one of them,
    the other gets the same value (TR 8.3.4).
  - `org` and `align` together are an error (TR 8.3.4).
  - `len` sets a minimum size. If the section's contents are bigger, you get warning 513.
  - Attributes (TR 8.3.5):
    - `DSECT` and `COPY` sections skip the memory and overlap checks.
    - `OVERLAY` sections may overlap other sections, but need an address.
    - For each output section, `LcfResult::outputs` reports its attribute. The assembler uses
      it to decide whether to write the section's bytes.
  - The padding value is parsed and reported, but unitron does not apply it.
  - `> MEMORY` selects the memory area for a section without an address. It is ignored, with a
    warning, if the section has an address (TR 8.3.8).
- **Input sections** (TR 8.3.6):
  - `*(a b ...)` takes sections from every module.
  - `FILE(a b ...)` takes sections from one module. FILE matches the module's path, its file
    name, or its file name without the extension, so `main.rel(f_code)` selects `main.asm`'s
    `f_code`.
  - `FILE` without a list takes all of that module's sections.
  - Each input goes to the first specification that matches it. In `*(a b)`, all the `a`
    sections come first, in input order, then all the `b` sections.
  - Inside an output section, each input is aligned to its own alignment.
  - An input section with a different type (code, data or romdata) is error 202.
- **Assign statements** `SYM = E;` and `SYM += E;` (TR 8.1.7):
  - You can write them at the top level or between output sections. They run in file order and
    are returned in `LcfResult::symbols`. If a symbol is assigned more than once, the last value
    wins.
  - `. = E;` and `. += E;` set the location counter (TR 8.1.4). It only works inside
    `sections { }`, and the counter can't decrease (error 229).
- **Checks**, reported as `FILE:LINE: message`:
  - overlap between output sections, or with absolute sections (error 206)
  - a section outside every memory area (201 and 210)
  - a memory area whose attributes don't match the section type (TR Table 8-2, error 227).
    This is checked at the run address. TR 8.3.4 says the load area's type does not have to
    match, so the load address only has to be inside some area.
  - `addr()`, `org()` or `sizeof()` of an unknown section
  - `addr()` or `org()` of a section that isn't allocated yet (error 214)
  - a small, medium or large section beyond 0x100, 0x10000 or 0x1000000
  - syntax errors. Parsing stops at the first one, which `parse_file` throws.

## Not supported

- Padding is not written into the image.
- The `I` attribute and the `-F` fill option have no effect.
- Assign statements inside an output section's `{ }`. TR 8.3.6 doesn't allow them there either.
- Library files, incremental linking (`-r`, TR 8.5), and options or file names inside the
  command file.
- Linker symbols from the assembler can't be used in lcf expressions. Expressions only see lcf
  symbols.
- `^`, `%` and other operators that are not in TR Table 8-1.

## Decisions where TR is silent or loose

- **Sections without an address.** TR 8.1.4 only says the location counter moves each time a
  section is allocated. unitron uses this rule:
  1. It picks the candidate memory areas, in this order:
     - the `> MEMORY` area, if given;
     - otherwise, the predefined memory for the section's type;
     - otherwise, every user area with the attributes the type needs, starting with the area
       that holds `.`, then in definition order.
  2. In each candidate area, the section goes to the first free, aligned space at or after `.`
     if `.` is in that area. Otherwise it goes to the first free space from the area's start.
  3. If there is no memory part, the section goes to the first free space at or after `.`.
     `.` starts at 0.

  The section's alignment is the largest of its `align=` and its inputs' alignments. Free
  space also has to stay within the section's small or medium range.
- **The location counter** becomes the end of the last allocated section's load range. That is
  `org + size`, the allocation address of TR 8.1.4.
- **Unnamed input sections.** TR 8.3.6 says they become output sections of the same name,
  joined in input order. unitron allocates them after the last `sections { }` block, by the
  automatic rule above, and gives a warning for each one. If there is no section definition
  part, it allocates them first and gives no warning (TR 8.3.1). If an unnamed input section
  has the name of an existing output section, that is error 219.
- **`org(S)` for a section that has only `addr=`.** TR 8.1.6 asks for an explicit `org=`, but
  common SDK files use `org()` this way. unitron returns the allocation address, which equals
  the start address (TR 8.3.4).
- **Functional operators outside the output specification field** (TR 8.1.6). They are allowed
  in assign statements too, as SDK files do.
- **`sizeof(S)` before S is allocated.** unitron returns S's size as if S started aligned. When
  S is allocated, it checks that value and reports an error if alignment padding made the size
  different.
- **Overlap checks** cover the load range, and the run range if it is different (for example,
  RAM used by initialized data). Sections of size 0 are not checked.
- **Attribute match** is checked against the one memory area that holds the whole run range. A
  section that spans two adjacent areas does not fit.
- **The padding value** is read as a single term, so `= 0xFFFF > rom` parses as padding followed
  by the output memory.
