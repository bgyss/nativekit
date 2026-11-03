# Handoff: Mega Drive / Genesis support in nativekit (nativemd)

Date: 2026-11-01. Branch: `nativemd` (on top of `nativekit`, PR bgyss/nativekit#1, not yet merged).

This document is for whoever picks up the Mega Drive work next, whether a person or an agent. It
says where things stand, what was decided, where to start, and how to know each step is done.
The two documents it builds on are:

- **Spec (approved):** `docs/superpowers/specs/2026-11-01-nativemd-design.md`
- **Stage 0 plan (completed):** `docs/superpowers/plans/2026-11-01-nativemd-stage0-kit.md`

**Current state (2026-11-03):** Stages 0-2 are complete for their stated scopes. Stage 3 Tasks 1-2
add the native Z80 coroutine, memory map, serial bank window, bus arbitration, and a synthetic
Z80 bank-window fixture; the MD unit and ROM checks pass. The PSG and YM code currently routes and
stores port writes, but synthesis, timers, mixing, and audio output remain open. BlastEm remains a
candidate oracle, not a hardware qualification result. Continue with Task 3 in
`docs/superpowers/plans/2026-11-03-nativemd-stage3-z80-sound.md`.

Read the spec first, then this handoff, then the stage 0 plan.

---

## 1. What nativemd is

nativemd is the second console on nativekit: a native Sega Mega Drive / Genesis held to the same
behaviour-exact bar as nativeshvc (the SNES). A game later runs on it as decompiled C that makes
every bus access through the console's calls, in the original order. The console accounts for
the time those accesses take. This is **platform work**: there is no target game yet.

The user has decided:

| Topic | Decision |
|---|---|
| Order | Platform first. The first game is chosen after stage 4, using the ROM survey. |
| Reference emulator | **BlastEm**, pinned and patched, built locally as a test tool, never shipped |
| Scope | Base console only: 68000, Z80, VDP (with DMA and H/V interrupts), YM2612, SN76489, 3- and 6-button pads |
| Not in scope | Sega CD, 32X, SVP, Master System mode, multitap, mouse, light guns, SRAM/EEPROM, mappers, Linux verification |
| Names | `consoles/md/`, library `nmd`, settings `NMD_*`, headers `include/nmd/`, fatal prefix `nativemd: ` |
| Approach | "A": mirror nativeshvc's structure and write the chips fresh from documentation, checked against BlastEm. The separate `segagenesisrecomp` checkout is **reference reading only**; don't copy code from it. |
| ROMs | The user's own local library, supplied as a runtime directory argument for the feature survey and first-game choice; the latest survey scanned 416 candidates. **Never commit ROM bytes, code, graphics, or ROM paths.** |
| Licence | AGPL-3.0, like the repo. The BlastEm patches are GPLv3-derived and live in this repo, which is compatible. vasm is free for non-commercial use, built locally and never shipped. |

Success, from the spec:
- `consoles/md/tests/rom/run.py` against BlastEm matches on every test cartridge (pictures, sound,
  and every read and its clock), and every entry in `KNOWN` is reasoned.
- nativemd has its own golden master.
- The SNES stays byte-for-byte unchanged throughout.
- `kit/` still names no console.
- Games link `nmd` through `add_subdirectory(<root>)`, as SNES games link `nshvc`.
- nativekit reaches 1.0.

## 2. Where to start

Stages 0 and 1 are complete for their recorded scopes. Stage 1 evidence and limitations are in
`docs/superpowers/plans/2026-11-01-nativemd-stage1-oracle.md` and its SDD ledger; Stage 2
implementation and validation are in `docs/superpowers/plans/2026-11-03-nativemd-stage2-bus-vdp.md`.
Continue with Task 3 of the Stage 3 Z80 and sound plan above.

1. From the repository root, use the `nativemd` branch.
2. `nix develop`. Run everything inside it, and use the mise tasks.
3. Run `tools/toolchains/setup.sh`, or `mise run tools`, if `build/tools/` is missing. It builds
   asar 1.91 and the patched bsnes oracle, which the SNES checks need.
4. Ensure the candidate MD tools are present with `mise run tools:md`; `mise run test:md-*`
   covers their synthetic fixtures and survey logic.
5. Follow the Stage 3 plan task by task. Preserve the separate C-routine Z80 model and candidate
   trace boundary; do not turn the sound port baseline into a synthesis or qualification claim.
   Keep later stages ordered and give them their own plans after Stage 3 lands.

### Stage 0 tasks (the plan has the code and the tests for each)

| # | Change | Why the MD needs it |
|---|---|---|
| 0 | Baseline: record golden, rom, perf numbers | so that "unchanged" means something |
| 1 | A single `snes_vfatal` behind `snes_fatal`, `nk_fatal` and `nshvc_fatal`; `kit/tests/kit_names.cmake` bans `nativemd nmd mega drive megadrive genesis ym2612 sn76489 68000 blastem` | the kit must never learn MD names |
| 2 | Includes become `nk/<header>.h`; `gl_api.h` moves to `kit/include/nk/`; `NK_PRIVATE_INCLUDE` | removes the flat include path and the GL public include dir |
| 3 | `NK_HOST/NK_RENDER/NK_TESTS`, with `NSHVC_*` as aliases; `NK_CONSOLES` (default `snes`, later `snes;md`); `alias_check` in `tools/check.sh` | the top level must build more than one console |
| 4 | `rr_set_headless_size` plus `kit/src/render/rr_common.c`, replacing the hard-coded 256×224 in `rr_size.h`; `render_check` | the MD is 256 or 320 wide, 224 or 240 high, doubled when interlaced |
| 5 | `nk_console.players`; `NK_PLAYERS` in `nk.h`; `nk_script_frame(uint32_t b[NK_PLAYERS])`; the script's `N.` prefix allowed up to `players` | more than two players later; the SNES says 2 |
| 6 | `nk_key` and `nk_input_keymap`; the console supplies `keys`/`key_count` | the 6-button pad's X/Y/Z/Mode and bits past 16 |
| 7 | `NK_KINDS_MAX`; `kind_names` | the MD's access kinds (VDP port, Z80 window, …) get names |
| 8 | `nk_quit`; `kit/tests/loop_check.c` with a stub console | pins the battery-save timing, which the kit alone now owns |
| 9 | Docs (`kit/docs/kit.md`, README), `kit/VERSION` 0.2.0, full `run.py` | |

**Rules that hold through all of stage 0:** an item that changes SNES output is a design error.
`include/nshvc/*.h` stays byte-identical. `include/nk.h` must never include `input.h`; it is
`input.h` that includes `nk.h`.

## 3. After stage 0: the stages that still need plans

Each stage gets its own plan (`superpowers:writing-plans`, saved under
`docs/superpowers/plans/`), its per-task reviews, and a branch review. Write each plan only
after the previous stage has landed, because each one depends on what the previous stage found.

### Stage 1: tooling, the oracle, and the survey
- Add pins to `tools/toolchains/pins.env`: BlastEm (by commit) and vasm (`vasmm68k_mot`,
  `vasmz80_oldstyle`). Have `setup.sh` build both into `build/tools/`.
- **First question to settle:** does BlastEm's libretro build expose what the runner needs? If it
  does, model the runner on `consoles/snes/tests/oracle/oracle.c` (1222 lines, a libretro runner
  plus hooks). If it doesn't, link BlastEm's core directly.
- Patches go in `tools/toolchains/patches/blastem/`. They add logging hooks:
  - every 68000 and Z80 bus access, with its master clock;
  - VDP port accesses and DMA slots;
  - YM2612 and PSG writes, with their clocks;
  - each frame's unfiltered picture;
  - the mixed sound and each chip's sound.

  The runner pins speed hacks and filters off.
- `consoles/md/tools/survey.py <rom-dir>` runs each ROM headless under the patched BlastEm for a
  fixed number of frames, then again with a scripted Start press. It records:
  - VDP modes and registers;
  - DMA kinds;
  - interlace;
  - shadow/highlight;
  - HINT use and which lines it runs on;
  - Z80 driver patterns (DAC streaming, direct 68000 YM writes);
  - 6-button probing;
  - SRAM and mapper use.

  The results are written to `consoles/md/docs/survey.md` as aggregate counts and a per-title
  feature table, with no ROM content in it. They decide the order of the test-cartridge areas.

### Stage 2: the 68000 side and the VDP
- The bus and memory map are `consoles/md/src/console/bus.c`, modelled on
  `consoles/snes/src/console/bus.c`. They provide:
  - `rd8/rd16/wr8/wr16`; a 32-bit access is two 16-bit accesses in 68000 order;
  - per-access cycle cost, including VDP port stalls, the FIFO, Z80-space accesses and refresh;
  - fatal errors with the prefix `nativemd: ` and exit 70.
- The VDP (`src/vdp/`) covers:
  - planes A and B, the window and sprites;
  - priority, shadow/highlight and interlace;
  - DMA (68k→VDP, fill, copy) with slot timing;
  - the H/V counters and interrupts: level 6 VBlank, level 4 HBlank, level 2 EXT.
- `src/front/md_console.c` fills in the `nk_console` table, modelled on
  `consoles/snes/src/front/snes_console.c` (160 lines).
- Test cartridges: write an MD counterpart of `consoles/snes/tests/rom/snestest.py`, a small
  68000 interpreter that runs a table of steps, plus `cases.py`, `run.py` and `KNOWN`.

### Stage 3: the Z80 side and sound
- Plan: `docs/superpowers/plans/2026-11-03-nativemd-stage3-z80-sound.md`.
- The Z80 runs on its own coroutine (`kit/include/nk/coro.h`), with its own routine table,
  through `zrd`/`zwr`. BUSREQ and RESET from the 68000 suspend and restart it at exact clocks.
  The bank window costs its real time and stalls the 68000.
- The SN76489 and YM2612 (registers, timers, busy flag, DAC) live in `src/sound/`, and their mix
  feeds `audio_take`. Resample with `kit/include/nk/resample.h`.

### Stage 4: pads, cost profiles, the full comparison, the golden master, and 1.0
- Pads: 3- and 6-button with TH timing, plus the version register and TMSS (`src/io/`).
- `consoles/md/tools/nmd-profile`, built on `kit/tools/nk_profile.py`.
- The full `run.py` passes, with only reasoned `KNOWN` entries.
- Record `consoles/md/tests/golden.py` once its outputs exist, and hold it from then on.
- `consoles/md/tests/consumer/` pins a game linking `nmd`.
- Docs: `consoles/md/docs/{nativemd.md,contract.md,survey.md}`.
- Freeze the contract (`include/nmd/{machine,cart,hw,nmd,z80}.h`) at 1.0 under
  `kit/docs/contract-discipline.md`. The kit goes to 1.0.

Target layout (spec section 1):

```
consoles/md/
  include/nmd/   machine.h cart.h hw.h nmd.h z80.h
  src/console/   bus.c dispatch.c
  src/vdp/       vdp.c dma.c render
  src/z80/       z80bus.c
  src/sound/     ym2612.c sn76489.c mix
  src/io/        pads, version register, TMSS
  src/front/     md_console.c md_input.c md_present.c console.c
  src/render/    md_rr.h (soft first; GPU backends only if the VDP needs them)
  tests/         rom/ oracle/ unit/ golden.py consumer/
  tools/         nmd-profile survey.py
  docs/          nativemd.md contract.md survey.md
```

## 4. How to check the work

**After every task**, the following must hold, inside `nix develop`:

| Check | Expected |
|---|---|
| `mise run build` | 9 builds (null, macos, null-metal, null-gl, null-vulkan, x86, ios, win, none), exit 0, 0 warnings |
| `mise run test:consumer` | exit 0 |
| `mise run golden:compare` | `golden: 0 of 2393 runs differ` |
| `mise run test`, `mise run test:kit` | `0 check(s) failed`, ok |
| `mise exec -- python3 consoles/snes/tests/golden.py time` | ≤ 23.07 s CPU (baseline 20.6 s) |
| `mise run test:rom` (stage ends) | `0 of 1164 differ; 56 more differ as KNOWN lists; 0 listed in KNOWN now pass` |

Once nativemd exists, add to every check: nativemd builds with 0 warnings, and its own golden
master holds once it is recorded.

## 5. What to read first in the repo

| File | Why |
|---|---|
| `kit/docs/kit.md` | what the kit provides and how a console plugs in |
| `kit/docs/contract-discipline.md` | versioning rules for a console's game contract |
| `kit/include/nk/nk.h` | `nk_console`, the one table a console fills in, and `nk_main` |
| `consoles/snes/src/front/snes_console.c` | a complete `nk_console` table, the model for `md_console.c` |
| `consoles/snes/src/console/bus.c`, `dispatch.c` | how bus access cost, timing, routine dispatch and fatal errors are done |
| `consoles/snes/include/nshvc/*.h` | the shape of a game contract: machine, cart, hw, top-level header, second CPU (`spc.h` ↔ `z80.h`) |
| `consoles/snes/tests/rom/{snestest.py,cases.py,run.py,KNOWN}` | the test-cartridge method to copy |
| `consoles/snes/tests/oracle/oracle.c` | the reference runner to copy for BlastEm |
| `consoles/snes/tests/golden.py` | the golden-master pattern |
| `consoles/snes/CMakeLists.txt`, top-level `CMakeLists.txt` | how a console adds its library (`add_subdirectory(consoles/snes)` today) |
| `tools/toolchains/{setup.sh,pins.env}` | where BlastEm and vasm get pinned and built |
| `consoles/snes/docs/snes-decomp-learnings.md` | lessons from the SNES that carry over |
| the separate `segagenesisrecomp` checkout's `GENESIS_AUDIO_FIDELITY.md`, `COSIM.md`, and related docs | background on MD hardware (read only; don't copy code) |

## 6. Environment notes and pitfalls

- Run everything inside `nix develop`. `golden.py` needs `NSHVC_ASAR`, which `mise exec` or the
  mise task sets.
- Never use mise's direnv integration. `.envrc` holds only `use flake`, and environment variables
  go in `mise.toml` under `[env]`.
- The local dcg hook blocks `rm -rf` and heredocs containing backticks. Use other forms. The SDD
  workspace from the extraction is still at `.superpowers/sdd/2026-10-30-nativekit-extraction/`
  (git-ignored).
- Use focused commit subjects consistent with the repository history. Ask before any outward-facing
  action (push, PR, merge).
- Never commit ROM content or ROM paths. Survey output holds only feature names and counts.

## 7. Open items after Stage 1

- Stage 1 is exploratory coverage for valid traffic on the pinned `md1va3` candidate. It does not
  qualify BlastEm as hardware-accurate. The Pico-specific epoch adjustment, 128-KiB VRAM word-fill
  arm, and invalid VDP data-read returns were not exercised by the fixtures.
- Exact equivalence between the pinned vasm mirror and the author's 1.9d archive remains unverified;
  see `docs/research/vasm-1.9d-provenance-audit.md`.
- PR #1 (nativekit) is still open. `nativemd` is stacked on it, so rebase if it changes.
- The repository folder rename to `nativekit` is complete in this checkout.
- An optional extra check: build Stunt Race FX (jeff-strutb/stuntracefx, shallow clone in
  `build/tools/stuntracefx`) against nativekit. It needs the user's own cartridge, plus
  `NSHVC=<root>/consoles/snes` and the library path for the new layout. The user hasn't decided
  whether to do this.
- Console roadmap after the MD: Master System, then PC Engine (and SuperGrafx), then NES, then
  PSX/N64 with the PC Engine CD.
