# unitron play

A headless Neo Geo Pocket (Color) for mapping a game by playing it: load a state, press buttons,
look at the screen, save a new state. It runs the **NgpCraft** emulator core by tixul (MIT,
https://github.com/Tixul/Ngpcraft_emulator, fetched at build time) with its high-level BIOS built in, so it needs no BIOS
file and no other program.

unitron play is the general part. Mapping a real game also takes scripts of your own (where its
RAM lives, how its menus are walked, what its data means); they drive `unitron play` and read the
files it writes. Those files are documented here so a script in any language can use them.
How to play a game this way cheaply and reproducibly: docs/play-agents.md.

## Commands

```
unitron play run ROM FROM NEW [SEQ ...] [--inputs FILE] [--dir DIR] [--lang en|ja] [--scale N] [--no-shots] [--no-trace]
unitron play script ROM ROUTE [--dir DIR] [--scale N] [--no-trace]
unitron play shot ROM STATE [--dir DIR] [-o PNG] [--scale N]
unitron play tiles ROM STATE [--dir DIR]
unitron play montage PNG [PNG ...] [-o PNG] [--cols N]
unitron play trace ROM [--dir DIR] [--addr A[-B]] [--unnamed NAMES] [--sym FILE]
unitron play hints ROM [--dir DIR] [-o HINTS] [--executed FILE]
```

- `run` loads state `FROM` (or `boot`: power on), plays each `SEQ` in turn and takes a screenshot
  after each (`DIR/shots/NEW_<i>_f<frame>.png`), then saves state `NEW` (`DIR/states/NEW.state`) and
  ORs the code it executed into `DIR/coverage.bin`, and merges what it saw into the trace,
  `DIR/trace.bin` (below; `--no-trace` skips it). It prints the screenshot paths, then
  `NEW: frame F, faults N, coverage C instruction addresses`. `--inputs FILE` first plays an input
  log (JSON `[[mask, frames], ...]`, the format of a state's `inputs`), for example to replay a
  state from `boot`, or a log recorded elsewhere.
- `shot` screenshots a saved state: it runs one frame with nothing pressed (the core draws the
  picture while a frame runs) and shows that; the state file is not changed.
- `run --until COND [--until COND ...] [--each SEQ] [--max N]`: after the sequences, play `SEQ`
  (default `w1`) again and again until one of the RAM conditions holds, at most N frames (default
  18000; exit code 3 if it gave up). One screenshot at the end. Conditions: below.
- `script` plays a route file (below): the deterministic way to play.
- `tiles` prints the screen as text: for both scroll planes, the 20 x 19 cells on screen as
  3-digit hex tile numbers (scroll applied). For a game with a tile font, a script of its own maps
  tile numbers to letters; for any game it shows layouts and maps without an image.
- `montage` puts screenshots side by side (4 per row by default), for reading several screens at once.
- `trace` reports on the trace: frames traced, code executed, ROM read as data, and every indirect
  jump or call site with the targets it took. With `--addr 4186` (or a range, `5740-5770`) it lists,
  for each work RAM address, every PC that read or wrote it and how often; `--sym FILE` (the
  `unitron asm --sym` format, `ADDR NAME` lines) shows each PC as the nearest routine + offset.
  This is how a RAM address found by play (a counter, a flag) leads to the code that uses it.
  With `--unnamed NAMES` (a project's RAM names: `NAME equ ADDR` lines such as `ram/wram.inc`, or
  `ADDR NAME` lines) it lists what is left to name: every work RAM address the game used that has
  no name, in runs of consecutive addresses with their reads, writes and the routines using them;
  addresses up to 16 bytes after a name are listed apart as `NAME+n` (fields or elements,
  probably); a last line counts used / named / near a name / unnamed. Play more, name more, and
  the list shrinks: an empty list for a stretch of play means its RAM is mapped.
- `hints` turns the trace into a hints file for `unitron dis --hints` (docs/language.md, "Trace
  hints"): `code` for every target of an indirect jump or call (function pointers, `switch` jump
  tables), `pointers` for runs of 4-byte ROM values that were read and are executed code, `data`
  for ROM read as data and never executed. `--executed FILE` also lists every executed instruction
  address, for `dis --executed`.

Typical loop: play, `play hints ROM -o hints.txt --executed executed.txt`, then
`unitron dis ROM -o src/ --hints hints.txt --executed executed.txt`. Hints only add what play
showed, so the source still rebuilds the ROM byte for byte.
- `--dir` defaults to `play` in the current directory. `--lang` sets the console language (the
  byte a bilingual game reads at 0x6F87) when booting; a loaded state keeps its own.

Example:

```
unitron play run game.ngp boot title 'w300' 'A w60'
unitron play run game.ngp title menu 'A w30 D D A w60'
unitron play montage play/shots/menu_*.png -o m.png
```

## Input sequences

Tokens separated by spaces:

| Token | Does |
|---|---|
| `A` `B` `O` `U` `D` `L` `R` | press: hold 5 frames, release 8 (`O` is Option) |
| `UA`, `DR`, ... | press several buttons together |
| `wN` | wait N frames, nothing held |
| `aN` `bN` `oN` `uN` `dN` `lN` `rN` | hold that button N frames, no release |

A frame is 1/60 s. Menus usually want 20-40 frames between presses, and 60-120 after a screen change.

## Conditions

`ADDR[.w|.l]OP VALUE`: ADDR in hex, OP one of `==` `!=` `<` `>` `<=` `>=`, VALUE decimal or `0x`
hex; `.w`/`.l` read 2 or 4 bytes little-endian (default 1). Examples: `4186==0`, `574E.w<100`.
Quote them in a shell (`<` and `>`).

## Routes

A route is a text file of steps, played in order by `play script`. It is the record of how a
state was reached, so a whole run directory can be rebuilt from its routes, and the same route
always gives the same states. `#` starts a comment.

| Step | Does |
|---|---|
| `from boot [lang ja]` / `from STATE` | start: power on, or load a saved state |
| `seq TOKENS` | play input tokens |
| `each TOKENS` | what `until` plays between checks (default `w1`) |
| `max N` | frame limit for each `until` (default 18000) |
| `until COND [COND ...]` | play `each` until one condition holds; the script stops with an error if none does |
| `expect COND [COND ...]` | all must hold now, else the script stops with the line and the value found |
| `save NAME` | save a state (its parent is the previous save, or the `from`) and merge the trace |
| `shot NAME` | screenshot, `DIR/shots/NAME_fFRAME.png` |

Example:

```
from boot
each A w40               # mash through text
max 20000
until 4190==0x10         # wait for a RAM value that marks the screen we want
expect 4186==0
save first_battle
shot first_battle
```

It prints one line per `save` and the path of each `shot`, nothing else. When a step fails, the
machine at that moment is saved as `ROUTENAME_fail` (the route file's name) for a look.

## States

`DIR/states/NAME.state`, little-endian:

| Part | Content |
|---|---|
| magic | `UNPLAY\0\1` |
| header | u32 length + JSON: `name`, `parent` (the state it was played from, `boot` for none), `rom_sha1`, `lang`, `frame`, `inputs` (the whole input log since power-on: `[[mask, frames], ...]`; mask bits U 1, D 2, L 4, R 8, A 16, B 32, O 64), `faults` (`[[frame, status, pc], ...]`) |
| core | u32 length + bytes each: RTC, CPU, aux (sound CPU and chip, timers, pending interrupts), link cable (structs from `ngpc_core.h`) |
| memory | u32 length + 0x000000-0x00BFFF (I/O, work RAM, Z80 RAM, video) |
| flash | u32 count, then address + (u32 length + bytes) for each run of cartridge bytes the game changed (saves) |

So a script can read a state's RAM (work RAM 0x4000-0x6FFF is at offset 0x4000 of the memory
block) and its input log without unitron. A state is exact: loading it and playing on gives the
same machine, byte for byte, as playing straight through. The input log from power-on means any
state can also be replayed from `boot`.

A state only loads with the ROM it was saved from (`rom_sha1`) and the same core build.

## The trace

While tracing, every instruction the core runs is looked at as it retires (instruction fetches are
not counted as reads). A traced run is the same game as an untraced one, frame for frame; it is
about twice as slow (still ~25x real time). `DIR/trace.bin` holds the merged trace of every run in the directory:

| Part | Content |
|---|---|
| magic | `UNTRACE\2` |
| header | u32 length + JSON: `rom_sha1`, `rom_size`, `frames` (frames traced, all runs) |
| reader | u32 length + one u32 per ROM byte: the first PC that read it as data, 0 = never |
| nread | u32 length + one u16 per ROM byte: data reads, saturating at 65535 |
| indirect | u32 length + records: u32 site, u32 count, then count x (u32 target, u32 times taken) |
| pointer reads | u32 length + (u32 address, u32 value) pairs: 4-byte ROM reads whose value (24 bits) was a ROM address |
| RAM reads | u32 length + (u32 address, u32 PC, u32 count) triples: work RAM (0x4000-0x6FFF) reads by PC |
| RAM writes | the same for writes |

"Indirect" means a `jp` or `call` whose target is not in the instruction (`call XWA`,
`jp XIX+WA`, ...); returns are not included. Targets outside the ROM (BIOS calls) are kept in the
trace but not written as hints.

## coverage.bin

One bit per ROM byte, set where an instruction started executing (byte `i`, bit `b` = ROM
address `0x200000 + 8*i + b`), merged over every `run` in the directory.
