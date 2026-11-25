# NGPC hardware names

This file explains `data/hwnames.tsv`, the table of hardware names that `unitron dis` writes into
`hw.inc`: where each group of names comes from, the naming rule, what the source documents get
wrong, and what stays unnamed and why.

The table is embedded into the binary at build time (cmake/embed.cmake). `data/ngpc_hw.inc` is
generated from it: `build/unitron_tests hwinc data --write` rewrites it, and the `hwinc` test fails
if it is stale. Every entry cites a Toshiba or SNK document. Homebrew headers and emulators are not
used as sources.

## Sources per group

SNK's documents are cited by their short names (*SysPro*, *SysWork*, *SysLib*, *SysCall*,
*K2GEres*, *K2GETechRef*, *K2GEpal*, *8Bit*, *MicroDMA*, *SoundMan*) and section; copies are in
[snk/](snk/) (for example [snk/SysPro/SysPro.md](snk/SysPro/SysPro.md)). In `data/hwnames.tsv` a
citation is `SNK DOC, SECTION`, the heading text as the document writes it without its markup, or
`SNK DOC §X-Y` for *K2GETechRef*'s numbered sections, never a line number; parts are joined by
`; `. The `hwnames` test checks that every cited heading exists. IO900H.H is cited by line
(`IO900H.H l.N`).

| Group (kind) | Count | Source |
|---|---|---|
| CPU on-chip I/O (`io`) | 95 | Toshiba's TMP95C061F register header IO900H.H ("For TMP95C061F", in the T900 kit's INCLUDE directory; copy: [toshiba/IO900H.H](toshiba/IO900H.H)), every `#pragma io` line, 0x01-0x7F. Size 2 where the header's `extern` list declares `unsigned int` (TREG4-7, CAP1-4, ADREG0-3); otherwise 1. |
| System work RAM (`sysram`) | 7 | *SysPro*, "SYSTEM INFORMATION" table, for the names; *SysWork*, "SYSTEM WORK DEFINITION", for lengths (0x6F80 is a word). |
| Interrupt vectors (`vector`) | 17 | *SysPro*, "SYSTEM INFORMATION", the table of interrupts available to the user program. The names are ours, taken from SNK's descriptions. Size 4. |
| K2GE registers (`k2ge`) | 34 | *K2GEres*, "K1GE Resister Table" (abbreviations and addresses), and *K2GETechRef* section 4. |
| Memory areas (`mem`) | 14 | *K2GETechRef* 4-2, 4-3-3, 4-4-4/5, 4-15 Table 19; *K2GEpal*; *SysLib*, "LINK COMMAND FILE" (Z80 RAM). Size = area length. |
| Bits (`bit` records) | 18 | *SysWork* "Bit Equation" names and bit diagrams; *K2GEres* "D7" entries. Stored as bit numbers. |

*8Bit* and *MicroDMA* give the same addresses as IO900H.H for TREG0-3, TFFCR, T23MOD, TRDC (*8Bit*
"3. TIMER REGISTER" and the T23MOD, TFFCR and TRDC register sections) and DMA0V-DMA3V (*MicroDMA*
"(4) Micro DMA Start Vector").

## Naming rule

- Official spelling, normalized: uppercase, `.` becomes `_` (`WBA.H` -> `WBA_H`, `Sys_Lever` ->
  `SYS_LEVER`, `SPPLT.01` -> `SPPLT_01`). The official spelling is kept in the last field
  (OFFICIAL) and in each `.inc` comment.
- If SNK's abbreviation column names a bit rather than the register, the register is named from
  its SNK register name. A leading digit moves to the end: `2D Status` -> `STATUS_2D`,
  `2D Control` -> `CONTROL_2D`. 0x8030 uses its summary `Scroll Priority` -> `SCROLL_PRIORITY`.
  The bit names are `bit` records: `STATUS_2D_C_OVR`, `CONTROL_2D_NEG` and
  `SCROLL_PRIORITY_P_F`. A plain `NEG` would clash with the TLCS-900 mnemonic.
- Where SNK gives no identifier, the names are ours: vectors (`VEC_*`), memory areas
  (`SPRITE_VRAM`, `CHARACTER_RAM`, `SPRITE_PAL`, ...) and Sys_Lever bits (`SYS_LEVER_UP`, ...).
- The `.inc` gives bit names as bit numbers (`equ 7`), matching asm900's `bit n,(mem)` form. The
  mask is `1 << n`.

The `hwnames` test checks these rules (`tests/README.md`).

## Notes on the SNK text

- *K2GETechRef* sections 4-13 and 4-14 label the scroll-plane LUT registers `SPPLT.01..13`, which
  looks like a copy of 4-12. *K2GEres* names them `SC1PLT.xx` / `SC2PLT.xx`, and the table uses
  those names.
- Addresses that appear only in *K2GEres*: 2D Status 0x8010, 2D Control 0x8012, Scroll Priority
  0x8030, BG 0x8118. The *K2GETechRef* sections for them show no address.
- Sys_Lever bits 0-5 come from the diagram in *SysWork*, "SYSTEM WORK DEFINITION". Bit 6 is
  OPTION, the first label under the diagram. A second label, "(D BUTTON NEO GEO controller when
  connected)", probably belongs to bit 7, but the diagram is cropped, so bit 7 stays unnamed.
- The *SysWork* Sys_Lever diagram does not mention `joypad.inc`.
- The *K2GETechRef* 4-2 memory map figure is garbled in the available text. Area bounds are taken
  from the text cited per entry.
- *8Bit* spells TFFCR as `TFFCFR` in its section heading and as `TFFCR` in that section's text.

## Variant headers (IO900.H, IO900L.H, IO900H2.H)

None of these are used for names. Differences that matter if a different CPU header is ever used
by mistake:

- WDMOD/WDCR are 0x5C/0x5D in IO900.H (TMP96C141/96CM40/96PM40) and IO900L.H (TMP93CM40F/41F),
  and 0x110/0x111 in IO900H2.H (TMP94C241). In IO900H.H they are 0x6E/0x6F, which agrees with
  *SysPro* ("HARDWARE RESOURCE DIRECTLY ACCESSIBLE THROUGH USER PROGRAM" and "CLEARING WATCH DOG
  TIMER": watchdog 0x6F, clear code 0x4E).
- T01MOD, T23MOD, INTET01/23 and INTETC01/23 do not exist in IO900.H or IO900L.H. They have TMOD,
  INTET10, INTEPW10, INTET54 and INTET76 instead. IO900L.H names the A/D results
  ADREG04/15/26/37 and has CKOCR/SYSCR0/SYSCR1 at 0x6D-0x6F.
- IO900H.H ends at 0x7F. ADREG0L/H (and the other xxL/xxH halves) are commented out in it (for
  example IO900H.H l.128-129), so only the word names are in the table.

## Disagreements with the homebrew headers (reported, not adopted)

Checked against the common homebrew files: `ngpc.h` (some copies lack the timer and Z80 lines),
`library.inc` (only Z80 driver bytes and tables, no hardware names), `system.lib` (code symbols
only) and Jeff Frohwein's homebrew `SYSTEM.INC` (2000).

Name differences, same address:

- 0x6F `WATCHDOG` (ours `WDCR`)
- 0x8002-0x8005 `WIN_X/WIN_Y/WIN_W/WIN_H` (`WBA_H/WBA_V/WSI_H/WSI_V`)
- 0x8009 `RAS_Y` (`RAS_V`); 0x8008 `RAS_H` agrees
- 0x8010 `STS_RG` alias (`STATUS_2D` agrees); 0x8012 `LCD_CTR` alias (`CONTROL_2D` agrees)
- 0x8020/0x8021 `SPR_X/SPR_Y` (`PO_H/PO_V`)
- 0x8030 `SCRL_PRIO`, `SCR_PRIORITY` (`SCROLL_PRIORITY`)
- 0x8032-0x8035 `SCR1_X/SCR1_Y/SCR2_X/SCR2_Y` (`S1SO_H/S1SO_V/S2SO_H/S2SO_V`)
- 0x8118 `BG_COL` (`BG`)
- 0x83F0 `WIN_PAL` (`WINDOW_PAL`); 0x83E0 `BG_PAL` agrees
- 0x8200/0x8280/0x8300 `SPRITE_PALETTE/SCROLL_1_PALETTE/SCROLL_2_PALETTE`
  (`SPRITE_PAL/SCROLL1_PAL/SCROLL2_PAL`)
- 0x8800 `SPRITE_RAM` (`SPRITE_VRAM`); 0x8C00 `SPRITE_COLOUR` (`SPRITE_PAL_CODE`)
- 0x9000/0x9800 `SCROLL_PLANE_1/2` (`SCROLL1_VRAM/SCROLL2_VRAM`)
- 0xA000 `CHAR_RAM`, `TILE_RAM` (`CHARACTER_RAM`)
- 0x6F80 `BAT_VOLT` (`BATTERY_VOLTAGE`); 0x6F82 `JOYPAD` alias (`SYS_LEVER` agrees)
- 0x6F84-0x6F86 `USR_BOOT/USR_SHUTDOWN/USR_ANSWER` (`USER_BOOT/USER_SHUTDOWN/USER_ANSWER`);
  `LANGUAGE`, `OS_VERSION`, `Z80_RAM`, `TRUN`, `TFFCR`, `TREG3`, `T23MOD` agree
- Vectors `SWI3_INT..SWI6_INT, RTCI_INT, VBL_INT, Z80_INT, TI0_INT..TI3_INT, STX_INT, SRX_INT,
  DMA0_INT..DMA3_INT` (ours `VEC_*`)
- Joypad masks `J_UP..J_OPTION` (ours `SYS_LEVER_*` bit numbers; the positions agree)
- SYSTEM.INC bit names `BOOTB_/SHUTDOWNB_/ANSWERB_*` (SNK's `*_BOOT_REQ`, `*_SHUTDOWN_REQ`,
  `RESUME_ANS`, `TIMER_SHUTDOWN_ANS`; the positions agree)

Size or meaning differences:

- 0x6F80 `BAT_VOLT` is declared `u8`; *SysWork* says word.
- 0x83F0 `WIN_PAL` is declared `u8`; *K2GETechRef* 4-15 Caution 1 says palette RAM is 16-bit
  access only.
- 0x6FD4 is both `HBL_INT` and `TI0_INT`; *SysPro* calls it timer 0 only.
- `J_POWER` 0x80: SNK gives no power label for bit 7 (see the Sys_Lever note).

Names the homebrew files have and the SNK documents give no address for (left out here):

- 0x8000 `DISP_CTL0`, 0x8006 `REF` (frame rate; *K2GETechRef* 4-7 gives no address),
  0x87E0 `RESET`, 0x87E2 `GE_MODE` (mode; 4-9 gives no address), 0x87FE `VERSION`
- 0xB8 `SOUNDCPU_CTRL`, 0xBA `Z80_NMI`, 0xBC `Z80_COMM`
- 0x4000 `WORK_RAM/MAIN_RAM`, 0x200000 `CART_ROM`, 0xFF0000 `INT_ROM`
- SYSTEM.INC also touches 0x6F83 and 0x6DA0, which SNK does not document.

BIOS vector numbers: `ngpc.h` gives only SHUTDOWN 0, CLOCKGEARSET 1, RTCGET 2, SYSFONTSET 5. The
homebrew SYSTEM.INC gives 0, 1, INTLVSET 4, 2, ALARMSET 9, ALARMDOWNSET 0x0B, 5, FLASHWRITE 6,
FLASHALLERS 7, FLASHERS 8, FLASHPROTECT 0x0D, GEMODESET 0x0E. Neither file is a source, and the
table does not include them.

## Not named

These are open (`OPEN.md`) until an SNK or Toshiba document gives them:

- **BIOS call numbers.** *SysCall*, "SYSTEM CALL OUTLINE", says the numbers are in SNK's
  SYSTEM.INC, which is not available. *SysCall* and *SysPro* ("NEOGEO POCKET SYSTEM CALL (BIOS
  CALL) TABLE") list the `VECT_*` names only, without numbers, so the table has none.
  `_INT_CLR_BIT` (*SysLib*, "SUBROUTINE DEFINITIONS") has no value either. (The reference ROM's
  reset call with RW3=1 could be CLOCKGEARSET or INTLVSET.)
- **Z80 interface 0xB8 / 0xBA / 0xBC.** No SNK document gives these addresses (*SoundMan* is empty
  in the available copy; *SysPro* "SOUND CONTROL CPU" and *8Bit* "5. TIMER FLIP FLOP" describe
  the Z80 but not the ports). What the drivers do with them is known (`OPEN.md`), but no source
  names them, so they stay numeric.
- **0x6FEC.** The vector slot is "(Reserved)" in *SysPro*, "SYSTEM INFORMATION".
- **Sys_Lever bit 7** (probably the NEO GEO controller's D button). See the notes above.
- **K2GE without SNK addresses:** 0x8000, 0x8006 (frame rate), 0x8400, 0x87E0, 0x87E2 (mode),
  0x87FE.
- **K2GE fields without a bit position in the available text:** BLNK (2D Status, *K2GETechRef*
  4-10), OOWC (2D Control, 4-11), BGON/BGC (BG, 4-6; BGON is a two-bit pattern D7=1/D6=0).
- **Work RAM ranges** 0x4000-0x6BFF (user; *SysPro*, "WORK RAM AREA") and 0x4000-0x5FFF (resume
  save; *SysWork*, "Resume Function Startup"). These are ranges, not registers. Naming 0x4000
  would override the program's own RAM labels.
- **Cartridge header fields** at 0x200000 (*SysPro*, "SOFTWARE CASSETTE RECOGNITION HEADER
  INFORMATION FORMAT"). These belong to the ROM layout, not to the hardware table.
- **CPU match.** The NGPC CPU is assumed to match the TMP95C061F (IO900H.H). This is confirmed
  only where SNK repeats the address (timers, DMA vectors, watchdog).
