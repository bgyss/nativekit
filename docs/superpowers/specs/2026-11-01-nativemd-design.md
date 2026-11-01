# nativemd: a behaviour-exact native Mega Drive on nativekit — design

Date: 2026-11-01. Branch: `nativemd`, on top of `nativekit` (PR #1, not yet merged).

## Purpose

Build the second console on nativekit: **nativemd**, a native Sega Mega Drive / Genesis held to the
same bar as nativeshvc. It is platform work: no target game yet. A game later plays on it as
decompiled C making its bus accesses through the console's calls. A survey of the user's own ROMs
(read locally, nothing from them committed) guides which hardware features come first and informs
the choice of the first game to decompile, after the platform is done.

What the user decided:
- platform first, no target game; use the user's ROMs for (1) a feature survey and (2) the later
  choice of a first game;
- reference emulator: **BlastEm**;
- scope: the **base console only** (68000, Z80, VDP with DMA and H/V interrupts, YM2612, SN76489,
  3- and 6-button pads). Sega CD, 32X, SVP, multitap, mouse, light guns, SRAM/EEPROM and mappers
  are later projects;
- name: **nativemd** (`consoles/md/`, library `nmd`, settings `NMD_*`, headers `include/nmd/`,
  fatal prefix `nativemd: `);
- approach A: mirror nativeshvc's structure and write the chips fresh from documentation, checked
  against BlastEm. segagenesisrecomp (`~/src/segagenesisrecomp`) is reference reading only.

## Success criteria

- `consoles/md/tests/rom/run.py` against BlastEm: every test cartridge matches — pictures pixel for
  pixel, sound sample for sample, every read and its clock — with only reasoned entries in
  `consoles/md/tests/rom/KNOWN`.
- nativemd has its own golden master, recorded once its outputs exist and held from then on.
- The SNES is byte-for-byte unchanged throughout: its golden master ("0 of 2393 runs differ"),
  reference comparison ("0 of 1164 differ; 56 more differ as KNOWN lists"), and perf (≤ +3%).
- `kit/` still names no console (the name check gains `nativemd`, `nmd`, `mega drive`,
  `megadrive`, `genesis`, `ym2612`, `sn76489`, `68000`, `blastem`).
- A game links `nmd` the way SNES games link `nshvc` (`add_subdirectory(<root>)`), pinned by a
  consumer test.
- nativekit reaches 1.0 with this console, per `kit/docs/contract-discipline.md`.

## Stages

Each stage is its own implementation plan, task-reviewed and branch-reviewed.

| Stage | Content |
|---|---|
| 0 | Kit changes the extraction's final review flagged (below) |
| 1 | Tooling and survey: vasm, patched BlastEm and its oracle runner, the ROM survey and `survey.md` |
| 2 | 68000 bus and memory map, VDP (planes, window, sprites, priority, shadow/highlight, interlace), DMA, H/V counters and interrupts |
| 3 | Z80, bus arbitration and bank window, SN76489, YM2612 |
| 4 | Pads (3/6-button), cost profiles, full reference comparison, golden master, docs; kit 1.0 |
| after | Choose the first game to decompile, using the survey |

## Section 1: layout and contract

```
consoles/md/
  include/nmd/   machine.h cart.h hw.h nmd.h z80.h      the frozen contract, 1.0
  src/console/   bus.c (68000 map, rd/wr, nk_fatal "nativemd: " exit 70), dispatch.c
  src/vdp/       vdp.c dma.c render (planes A/B, window, sprites, shadow/highlight, interlace)
  src/z80/       z80bus.c (BUSREQ/RESET, bank register, 68k window)
  src/sound/     ym2612.c sn76489.c mix
  src/io/        pads (3/6-button, TH timing), version register, TMSS
  src/front/     md_console.c (nk_console table), md_input.c, md_present.c, console.c
  src/render/    md_rr.h; per-backend md_*.{c,m} only if the VDP needs GPU work (soft first)
  tests/         rom/ (vasm carts, cases.py, run.py, KNOWN), oracle/ (BlastEm runner), unit/,
                 golden.py, consumer/
  tools/         nmd-profile (on kit/tools/nk_profile.py), survey.py
  docs/          nativemd.md, contract.md, survey.md
```

The contract (rules carried from nativeshvc):

- **68000.** The game's C makes every bus access through `rd8`/`rd16`/`wr8`/`wr16` in the
  original order; a 32-bit access is two 16-bit ones in the 68000's order. The C states no time:
  each access costs its bus cycles, wait states included (VDP port stalls and FIFO, Z80-space
  accesses, refresh). Waiting goes through `nmd_wait_vblank`, `nmd_wait_until` and `STOP`.
  Interrupt handlers (level 6 VBlank, level 4 HBlank, level 2 EXT) are routines the hardware
  enters at its own clock.
- **Z80.** The sound driver's C runs on its own coroutine with its own routine table, through
  `zrd`/`zwr`. BUSREQ and RESET from the 68000 suspend and restart it at exact clocks; its
  accesses to the 68000 bus through the bank window cost what they do on hardware and stall the
  68000.
- **Cartridge connector** (`cart.h`): ROM, routine tables, title, region. SRAM, EEPROM and mappers
  reserved for later behind version-checked fields.
- **Extensions:** opt-in `nmd_ext_*`, as `nshvc_ext_*`.
- **Versioning:** contract 1.0 under `kit/docs/contract-discipline.md`.

## Section 2: kit stage 0

The SNES stays byte-identical through every item; an item that would change SNES output is a
design error.

1. **Screen size from the console.** `rr_size.h`'s 256x224 headless default goes; the kit asks
   `window_size`/`headless_scale`. The SNES returns 256x224. The Mega Drive changes between 256 and
   320 wide, 224 and 240 high, doubled when interlaced.
2. **More than two players.** `nk_console.players`; `b[2]` becomes `b[NK_PLAYERS]`; the script's
   `N.` prefix accepted up to `players`. The SNES says 2.
3. **Controller map from the console.** The keyboard map moves out of `kit/src/front/input.c` into
   a console-supplied table (key, positional bit, name). The SNES supplies today's table.
   Positional bits grow past 16 for a 6-button pad's second face row and Mode.
4. **Console-neutral build options.** Top level gets `NK_CONSOLES` (default `snes;md`) and generic
   `NK_HOST`, `NK_RENDER`, `NK_TESTS`; `NSHVC_HOST`, `NSHVC_RENDER`, `NSHVC_TESTS`,
   `NSHVC_EXAMPLES` stay as aliases. Each console adds its own library target. A game linking
   `nshvc` or `nmd` through `add_subdirectory(<root>)` works.
5. **`nk/...` includes.** Consoles include `nk/input.h` etc.; the flat `kit/include/nk` path is
   dropped. `gl_api.h` moves to `kit/include/nk/`, and `kit/src/render/gl` stops being a public
   include directory.
6. **Named access kinds.** `nk_console` gains kind names; the 16-kind cap becomes a kit constant
   the console reads.
7. **A kit-only loop test.** A stub console runs `nk_main` headless through frames, pinning the
   battery save timing (every 120 frames, at quit, never headless).
8. **One fatal function in the SNES.** `snes_fatal`, `nk_fatal` and `nshvc_fatal` share one
   `vfatal`. No behaviour change.

## Section 3: reference, test cartridges, survey, verification

**BlastEm**, pinned by commit in `tools/toolchains/pins.env`, built by `tools/toolchains/setup.sh`.
Stage 1 first confirms whether BlastEm's libretro build exposes what the runner needs; if so the
runner follows `consoles/snes/tests/oracle/oracle.c`, otherwise it links BlastEm's core directly.
Our patches (`tools/toolchains/patches/blastem/`) add hooks logging: every 68000 and Z80 bus access
with its master clock; VDP port accesses and DMA slots; YM2612 and PSG writes with clocks; each
frame's unfiltered picture; mixed and per-chip sound. Speed hacks and filters off, pinned by the
runner.

**Test cartridges.** vasm (`vasmm68k_mot`, `vasmz80_oldstyle`), built from source by `setup.sh`.
An MD counterpart of `snestest.py`: a small 68000 interpreter running a table of steps (register
writes, VDP commands, DMA, waits for a line or VBlank, reads logged to RAM, Z80 programs uploaded
and run, interrupt handlers). `cases.py` covers: VDP modes, planes, scroll, window, sprites,
priority, shadow/highlight, interlace; DMA 68k→VDP, fill, copy, and timing; FIFO stalls; H/V
counters; HINT; Z80 bus and bank window; YM2612 registers, timers, busy flag, DAC; PSG. `run.py`
replays each cartridge's logged accesses on nativemd at the same clocks and compares pictures,
sound and reads.

**Survey** (stage 1). `consoles/md/tools/survey.py` runs each ROM under the patched BlastEm
headless for a fixed number of frames with no input, then with a scripted Start press, and records
features used: VDP modes and registers, DMA kinds, interlace, shadow/highlight, HINT use and lines,
Z80 driver patterns (DAC streaming, direct 68000 YM writes), 6-button probing, SRAM/mapper use.
Output: `consoles/md/docs/survey.md` with aggregate counts and a per-title feature table. No ROM
bytes, code or graphics in the repo; ROM paths stay local (the ROM directory is a command-line
argument). The findings order the test-cartridge areas and feed the first-game choice.

**Verification.** Every task: SNES golden + reference unchanged; nativemd builds with 0 warnings;
nativemd golden (once it exists); `mise run test`. Each stage end: full nativemd reference
comparison, 0 differ apart from reasoned `KNOWN` entries.

## Licensing

nativemd is AGPL-3.0 like the rest of the repo. BlastEm is GPLv3: fetched and built locally as a
test tool, never shipped; our BlastEm patches are GPLv3-derived and kept in this AGPL repo
(compatible, GPL/AGPL §13). vasm is free for non-commercial use; built locally, never shipped.
Note both in `consoles/md/tests/README.md`. No ROM content is committed.

## Out of scope

Sega CD, 32X, SVP, Master System mode, multitap, mouse, light guns, SRAM/EEPROM, mappers, Linux
verification, choosing or decompiling a game (comes after stage 4).
