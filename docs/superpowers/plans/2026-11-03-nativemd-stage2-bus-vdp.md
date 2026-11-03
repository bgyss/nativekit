# nativemd Stage 2: 68000 Bus and VDP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development`
> (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Each task
> has a test/review gate; keep shared interfaces and the master-clock scheduler in order.

**Goal:** Implement the base Mega Drive's 68000-visible bus and VDP, including DMA, video output,
H/V counters, and interrupts, with candidate-reference comparisons for the covered cases.

**Architecture:** `nmd` remains a game-facing native machine model, not a shipped CPU emulator.
Game C makes explicit bus operations; one shared master-clock scheduler advances the bus, VDP,
DMA, and interrupts. A software renderer produces pixels first. Synthetic 68000 test cartridges
run on the separate patched BlastEm candidate, while a native C interpreter executes the same
step table through `nmd` and compares reads, clocks, and pictures.

**Tech Stack:** C11, CMake, Python 3, vasm, Mise, the pinned local BlastEm candidate, and the
existing nativekit kit APIs.

**Spec:** `docs/superpowers/specs/2026-11-01-nativemd-design.md`, especially sections 1–3; Stage 2
scope in `docs/superpowers/handoff/2026-11-01-nativemd-handoff.md`.

## Global Constraints

- Follow approach A: write the 68000 bus and VDP fresh from hardware documentation; do not copy
  code from the separate `segagenesisrecomp` checkout.
- Scope is the base Mega Drive: 68000 bus, Mode 5 VDP, planes, window, sprites, DMA, H/V counters,
  and levels 6/4/2 interrupts. Master System compatibility mode, SRAM/EEPROM, mappers, Sega CD,
  32X and SVP remain out of scope.
- The game-facing calls are `rd8`, `rd16`, `wr8`, and `wr16`; longword helpers, when used, perform
  high word then low word in 68000 order. Accesses carry the bus's time; C arithmetic carries none.
- Keep one master-clock unit for bus, VDP, DMA, and interrupt events. Use the Sega manual's
  7.67-MHz NTSC and 7.60-MHz PAL 68000 clocks when deriving frame pace and access intervals.
- `consoles/md/` owns all MD names. `kit/` stays console-neutral; the SNES and its outputs stay
  unchanged. The library target is `nmd`, headers live in `include/nmd/`, and fatal errors start
  with `nativemd: ` and exit 70.
- Stage 2 has no per-game CPU cost profile. It implements hardware bus cycles only; profile tools
  are Stage 4 work.
- Use synthetic test code only. Never commit game ROM bytes, graphics, paths or credentials.
- Keep the renderer software-first. Add a GPU backend only if a measured VDP requirement calls for
  it.
- Every task keeps the Stage 0 gate: nine builds with zero warnings, consumer, golden compare,
  kit checks, ROM compare, and SNES timing at or below 23.07 s CPU. Add MD build/oracle checks once
  their targets exist.
- References: Sega's [Technical Overview 1.00](https://segaretro.org/images/1/18/GenesisTechnicalOverview.pdf)
  documents the 16-bit memory map, VDP ports/registers, interrupts, RAM-access timing and DMA;
  Motorola's [M68000 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf)
  documents access widths, instruction bus cycles, and address errors.

## Review Focus

1. Odd-address word/long access and the 24-bit address boundary must follow 68000 behavior; pin
   both valid and invalid cases in the bus tests.
2. A two-word VDP control command is one command; intervening data-port activity cancels its
   latch. Pin the sequence and relative port clocks.
3. Active-display FIFO pressure and DMA must delay the 68000 at the documented bus event; pin
   before/after clocks and data, including VBlank behavior.
4. DMA fill is VRAM-only per Sega's manual; memory-to-VDP DMA separately covers VRAM, CRAM, and
   VSRAM, while VRAM copy covers byte slots. Test valid modes and reject unsupported modes clearly.
5. HINT/VBlank/EXT pending times, masks, and STOP/resume order must not lose or reorder an
   interrupt at a line or frame boundary.

## File Map

- Create `consoles/md/CMakeLists.txt`, `include/nmd/{machine,cart,nmd}.h`, and
  `src/front/md_console.c` for the initial `nk_console` integration.
- Create `src/console/{bus.c,dispatch.c,md_internal.h}` for the 24-bit bus, shared clock, routine
  table, and 68000 interrupt entry/return.
- Create `src/vdp/vdp.c`, `dma.c`, and `render.c` for VDP state/ports, slot-timed transfers, and
  software pixels.
- Create `src/front/md_present.c` for presenting the software frame through the kit.
- Create `tests/rom/{testcart.c,mdtest.py,cases.py,run.py,KNOWN}` and focused checks under
  `tests/unit/`. `mdtest.py` emits deterministic 68000 ROMs from a step table; `testcart.c` runs
  the same table through the native machine calls.
- Modify the root CMake/Mise integration to build `md` by default after the target exists and to
  expose `test:md-rom`.

---

### Task 1: Add the nmd library and console shell

**Files:**
- Create: `consoles/md/CMakeLists.txt`, `include/nmd/{machine.h,cart.h,nmd.h}`,
  `src/front/md_console.c`, `src/front/md_console.h`, `src/console/bus.c`
- Create: `tests/unit/console_check.c`
- Modify: root `CMakeLists.txt`, `mise.toml`, `consoles/md/tests/CMakeLists.txt`

**Interfaces:**
- `nmd_region`: `NMD_NTSC` or `NMD_PAL`.
- `typedef struct nmd_routine { uint32_t addr; void (*abi)(void); const char *name; } nmd_routine`,
  sorted by address.
- `typedef struct nmd_cart { const char *title; nmd_region region; const uint8_t *rom;
  uint32_t rom_size; const nmd_routine *routines; size_t routine_count; } nmd_cart`; no mapper or
  persistent RAM fields.
- `nmd_main(const nmd_cart *cart, int argc, char **argv)` starts the `nk_console`;
  `nmd_power(const nmd_cart *)` and `nmd_run_frame(void)` support tests and applications with
  their own loop.
- `machine.h`: `uint8_t rd8(uint32_t)`, `uint16_t rd16(uint32_t)`, `void wr8(uint32_t,uint8_t)`,
  `void wr16(uint32_t,uint16_t)`, `uint32_t rd32(uint32_t)`, `void wr32(uint32_t,uint32_t)`,
  `uint64_t nmd_wait_until(int (*ready)(void *ctx), void *ctx)`, `void nmd_wait_vblank(void)`,
  `void nmd_stop(void)`, and `uint8_t nmd_tas(uint32_t addr)`. `typedef struct nmd_cpu_state {
  uint32_t d[8], a[8], pc; uint16_t sr; } nmd_cpu_state` is exposed as `extern nmd_cpu_state
  nmd_cpu` for routine and interrupt entry. `nmd_tas` returns the old byte, writes it with bit 7
  set, and updates `nmd_cpu.sr` as TAS specifies. The contract remains provisional until Stage 4.
- `md_console` is the only `nk_console` table. `nmd_main()` delegates to `nk_main()`; no
  console-specific concept is added to `kit/`. Its name is `nativemd` and its settings prefix is
  `NMD`.
- `src/console/bus.c` initially defines `nk_fatal` with the required `nativemd: ` prefix and exit
  status 70; Task 2 extends it with the actual bus map.
- The `nmd` target folds in `nk_coro`, `nk_resample`, `nk_dispatch`, `nk_front`, `nk_input`,
  `nk_app`, the selected `nk_render`, and `NK_HOST_OBJECTS`, following the SNES target's CMake
  wiring. It links host frameworks/libraries selected by the generic `NK_HOST` option.

- [x] Run `cmake -S . -B build/md-stage2-red -DNK_CONSOLES="snes;md"` before the target exists;
  confirm it fails with `nativekit: no console md`.
- [x] Create the minimal `nmd_cart`, `machine.h`, and `nmd_console` shell, then add `md` to the
  default console list after the target builds.
- [x] Add `console_check.c` as a linked consumer of `nmd_main`; confirm the test fails before the
  library symbol is supplied and passes after the target is wired.
- [x] Verify `nmd_main` links to the kit's `nk_main` and the console shell compiles with zero
  warnings in null and macOS configurations.
- [x] Run the Stage 0 gate and commit.

### Task 2: Implement the 68000 bus and base memory map

**Files:**
- Create: `src/console/md_internal.h`
- Create: `tests/rom/{testcart.c,mdtest.py,cases.py,run.py,KNOWN}`
- Create: `tests/unit/bus_check.c`
- Modify: `include/nmd/machine.h`, `include/nmd/cart.h`, `mise.toml`
- Modify: `src/console/bus.c` with the map, timing, and access functions

**Interfaces:**
- `rd8/rd16/wr8/wr16` address the 24-bit 68000 space; words are big-endian and byte accesses use
  the 68000's address lane. `rd32/wr32` perform two 16-bit bus operations, high word first, with
  no interrupt between the words.
- Internal prototypes are `uint8_t nmd_bus_read8(uint32_t)`,
  `uint16_t nmd_bus_read16(uint32_t)`, `void nmd_bus_write8(uint32_t,uint8_t)`, and
  `void nmd_bus_write16(uint32_t,uint16_t)`. `md_clock` advances by documented bus cycles and
  waits. A 68000 access to Z80 RAM is permitted only after the documented word-write BUSREQ
  handshake; Stage 2 stores the 8-KiB Z80 RAM and control bits but does not run the Z80 CPU.
- Map ROM `$000000-$3FFFFF`; the Z80 window `$A00000-$A01FFF`; YM ports `$A04000-$A04003` and
  bank register `$A06000`; I/O `$A10000`; memory mode/BUSREQ/RESET at `$A11000/$A11100/$A11200`;
  VDP ports mirrored through `$C00000-$DFFFFF`; and mirrored 64-KiB work RAM `$E00000-$FFFFFF`.
  Pad/version/TMSS semantics wait for Stage 4; YM/PSG state waits for Stage 3, though the bus
  still decodes and times those ports. Do not add SRAM or mapper support. Unknown/prohibited
  accesses fail with `nativemd:` instead of returning guessed data.

- [x] Add a 68000 table interpreter in `mdtest.py` and a native interpreter in `testcart.c`; the
  same `cases.py` table drives both the candidate ROM and `nmd`.
- [x] Add failing unit and ROM cases for ROM/work-RAM reads, mirrored work RAM, byte lanes, big-
  endian words, ordered longwords, 24-bit address masking, and the four-clock-period base memory
  bus cycle. The Z80 RAM case must issue a word write of `$0100` to `$A11100`, poll BUSREQ
  acknowledgement before access, then release it with a word write of `$0000`.
- [x] Add a failing odd-address word/long access case; report the 68000 address error as a
  `nativemd:` fatal with exit 70 rather than silently aligning the address.
- [x] Implement the memory map and bus cycle accounting from the Sega memory map and Motorola
  68000 cycle tables. The Sega manual identifies `$A11000` as DRAM-development-cartridge refresh
  mode; do not infer a stock work-RAM refresh stall from that setting. Reserved, prohibited, and
  unsupported cartridge-space accesses fail with `nativemd:`; implement documented open-bus
  behavior only in the device paths that specify it.
- [x] Add `nmd_tas(uint32_t addr)`: the 68000 manual defines TAS as an indivisible byte
  read-modify-write cycle. Verify it returns the old byte, sets bit 7, sets N/Z from the old byte,
  clears V/C, preserves X, and prevents interrupt entry between the read and write.
- [x] Run bus unit cases and MD oracle comparisons; record candidate-bound differences in `tests/rom/KNOWN`.
- [x] Run the current-tree Stage 0 build, test, and timing gates; record host-limited failures in
  the SDD ledger.
- [x] Commit Task 2 as a focused implementation change.

### Task 3: Add the master-clock scheduler, counters, and interrupt dispatch

**Files:**
- Create: `src/console/dispatch.c` and the VDP line-scheduler portion of `src/vdp/vdp.c`
- Create: `tests/unit/interrupt_check.c`, `tests/interrupt_check.py`
- Modify: `src/console/md_internal.h`, `src/console/bus.c`, `src/front/md_console.c`,
  `consoles/md/CMakeLists.txt`, `consoles/md/tests/CMakeLists.txt`, and root `mise.toml`

**Interfaces:**
- `md_advance_to(uint64_t clock)` steps line timing and VDP deadlines in time order;
  `md_ext_irq_at(int level, uint64_t clock)` is the internal EXT-line input that Stage 4's pads
  will drive.
- `nmd_wait_until(int (*ready)(void *ctx), void *ctx)` re-runs a device-read predicate while the
  clock advances and returns elapsed master clocks; `nmd_wait_vblank(void)` waits for VBlank;
  `nmd_stop(void)` waits for an eligible interrupt.
- The VDP asserts level 6 VBlank and level 4 HBlank; `md_ext_irq_at` drives level 2 EXT.
  `dispatch.c` resolves each vector through the cartridge's sorted `nmd_routine` table,
  saves/restores `nmd_cpu`, and rejects a missing handler with `nativemd:`.
- `md_console.run_frame` advances to one VBlank boundary and returns to the kit at the frame's
  region-specific duration.

- [x] Add cases for NTSC/PAL line/frame boundaries, HINT reload cadence, H/V counter reads, level
  priority, mask/unmask, STOP/wake, and an external interrupt pulse.
- [x] Add boundary cases where HINT or VBlank becomes pending during a bus access and during STOP;
  pin that service occurs at the end of the access or at the pending clock in a wait.
- [x] Implement a single event scheduler that advances VDP state before reads, writes, and waits;
  do not derive timestamps independently in the devices.
- [x] Run scheduler/interrupt unit cases and compare the relative HINT trace with the candidate.
- [x] Revalidate current nmd builds, kit, consumer, and MD checks; carry forward unchanged SNES
  Stage 0 evidence and record the existing host-limited subgates.
- [x] Commit Task 3 as a focused implementation change.

### Task 4: Implement VDP registers, ports, memory access, and FIFO timing

**Files:**
- Create: `src/vdp/vdp.c`, `src/vdp/vdp.h`, `include/nmd/hw.h`
- Modify: `src/console/bus.c`, `tests/rom/cases.py`, `tests/unit/vdp_check.c`

**Interfaces:**
- Internal calls `uint16_t md_vdp_read16(uint32_t port, uint64_t clock)`,
  `void md_vdp_write16(uint32_t port, uint16_t value, uint64_t clock)`,
  `void md_vdp_advance(uint64_t to_clock)`, and `uint64_t md_vdp_next_event(void)` serve the bus
  and scheduler.
- `hw.h` publishes `NMD_VDP_DATA` (`0xC00000`), `NMD_VDP_CONTROL` (`0xC00004`),
  `NMD_VDP_HV` (`0xC00008`), and `NMD_PSG` (`0xC00011`) for game C.
- VDP state includes its 24 registers, VRAM/CRAM/VSRAM, address/code latch, data read prefetch,
  status flags, H/V counter, and a four-word write FIFO.

- [x] Add failing cases for control command pairs, second-word latching, data-port cancellation,
  register read/write, VRAM prefetch, CRAM/VSRAM access, H32/H40 status, and H/V counters.
- [x] Add a fixed CPU/VDP port microprogram with expected width/value/order and relative event-clock
  deltas; include a four-word FIFO fill case that proves a later CPU write stalls.
- [x] Implement VDP port decode/mirrors and device-side scheduling; preserve the trace's documented
  distinction between CPU handler-completion clocks and VDP scheduler event clocks.
- [x] Run VDP unit cases and compare port values/clocks to BlastEm; commit.

### Task 5: Implement DMA modes and bus stalls

**Files:**
- Create: `src/vdp/dma.c`
- Modify: `src/vdp/vdp.c`, `src/console/bus.c`, `tests/rom/cases.py`, `tests/unit/dma_check.c`

**Interfaces:**
- `void md_dma_start(uint64_t clock)`, `void md_dma_advance(uint64_t to_clock)`, and
  `int md_dma_busy(void)` operate through the shared scheduler. M68K source DMA reads ROM/work RAM
  words, then writes the selected VDP memory type at destination slots.
- Implement documented memory-to-VDP DMA to VRAM/CRAM/VSRAM, VRAM fill, and VRAM copy. Fill mode
  is VRAM-only per Sega's Technical Overview; do not copy the candidate core's undocumented
  CRAM/VSRAM fill behavior into nmd.

- [x] Add failing cases for DMA source bounds, source word order, each destination memory type,
  VRAM fill seed/slot timing, VRAM copy bytes, zero count, autoincrement, active display, and
  VBlank.
- [x] Assert the 68000 is held for memory-to-VDP DMA and all interrupt timestamps remain ordered
  during DMA. Expose the bus-ownership boundary Stage 3 will use to suspend Z80 accesses to the
  contested 68000 bus; do not claim Z80 execution while that CPU is not implemented.
- [x] Implement slot timing from the documented display modes; compare slot events, data, and
  first/last access clocks to the pinned candidate.
- [x] Run DMA cases and the full Stage 0 gate; commit.

### Task 6: Render Mode 5 background planes, scroll, and window

**Files:**
- Create: `src/vdp/render.c`
- Modify: `src/front/md_console.c`, `src/front/md_present.c`, `tests/rom/cases.py`,
  `tests/unit/render_check.c`

**Interfaces:**
- `const uint32_t *md_vdp_picture(int *w, int *h)` returns the current ARGB frame and active
  dimensions; `md_present.c` presents it with the kit's software renderer.
- Mode 5 supports H32/H40 and V28/V30 screen sizes, planes A/B, plane scroll, window placement,
  and the VDP's tile/palette lookup. Mode IV remains the excluded Master System compatibility
  path.

- [x] Add deterministic ROM cases for plane A/B patterns, tile-map addressing, horizontal and
  vertical scroll, window boundaries, and all four output dimensions.
- [x] Compare every output pixel with BlastEm for those cases; retain frame hashes as diagnostics
  but fail on the first differing pixel.
- [x] Implement a software line renderer that consumes the same timestamped VDP state updated by
  `md_vdp_advance`; do not snapshot a frame only at VBlank.
- [x] Run the new render cases and Stage 0 gates; commit.

### Task 7: Add sprites, priority, shadow/highlight, and interlace

**Files:**
- Modify: `src/vdp/render.c`, `src/vdp/vdp.c`, `tests/rom/cases.py`,
  `tests/unit/render_check.c`

**Interfaces:**
- The compositor resolves plane/window/sprite priority and the VDP shadow/highlight palette
  modes. Interlace tracks field parity and returns doubled-height pixels when active.

- [x] Add deterministic cases for sprite table traversal, clipping and priority, shadow/highlight,
  interlace field alternation, and register changes made mid-line.
- [x] Compare complete frames with BlastEm for each case, including sprite overflow limits the
  official manual documents; do not turn the test oracle's internal behavior into an undocumented
  hardware claim.
- [x] Implement the compositor and field buffer; verify 256/320 widths, 224/240 heights, and
  doubled interlace output through `nk_console.window_size` and `shot`.
- [x] Run render cases, consumer/build checks, and Stage 0 gates; commit.

### Task 8: Complete the Stage 2 ROM matrix and branch review

**Files:**
- Modify: `tests/rom/{cases.py,run.py,KNOWN}`, `tests/README.md`, `docs/nativemd.md`
- Modify: root `mise.toml` and the MD test runner wiring

**Interfaces:**
- `mise run test:md-rom` runs all Stage 2 synthetic cases; `run.py --only NAME,...` supports
  focused task tests.
- `run.py` compares 68000 reads/values/clocks, DMA slots, and Mode 5 frames; `KNOWN` contains only
  individually reviewed differences with a hardware or candidate limitation.

- [ ] Ensure every Stage 2 requirement above has a named case and every case has a passing native
  result or a reasoned `KNOWN` entry.
- [ ] Run `mise run tools:md`, `mise run test:md-oracle-smoke`, `mise run test:md-rom`, and the full
  Stage 0 gates on the final Stage 2 tree. Inspect logs for warnings and the report for ROM paths.
- [ ] Request a fresh whole-branch review; resolve all Critical/Important findings before marking
  Stage 2 complete.
- [ ] Commit the reviewed Stage 2 result. Stage 3 requires its own plan after this stage lands.
