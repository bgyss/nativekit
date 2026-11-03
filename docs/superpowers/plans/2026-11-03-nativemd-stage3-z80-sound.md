# nativemd Stage 3: Z80 and Sound Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this
> plan task-by-task. Each task has a test and review gate.

**Goal:** Add the Mega Drive Z80 sound CPU model, bank window and bus arbitration, SN76489 PSG,
YM2612, and mixed audio output to `nmd`.

**Architecture:** Keep one master-clock scheduler. A game supplies Z80 sound routines as C callbacks
on a coroutine, parallel to the 68000 routine model; each explicit Z80 memory or I/O access pays
its documented T-states, while C computation takes no time. The Z80 bus model owns its 8 KiB RAM,
YM port map, serial bank register, and access to the 68000 bus. PSG and YM2612 state advance from
the region's chip clocks, generate independent streams, and feed the console's audio queue through
the existing resampler.

**Tech Stack:** C11, `nk/coro.h`, `nk/resample.h`, CMake, Python 3, vasm, and the pinned local
BlastEm candidate.

**Spec:** `docs/superpowers/specs/2026-11-01-nativemd-design.md` (sections 1 and 3),
`docs/superpowers/handoff/2026-11-01-nativemd-handoff.md` (Stage 3), and the completed
`docs/superpowers/plans/2026-11-03-nativemd-stage2-bus-vdp.md`.

## Global Constraints

- Follow the approved approach A: implement fresh C models from hardware documentation. Do not copy
  implementation code from `segagenesisrecomp` or vendor an emulator core.
- Keep 68000, Z80, PSG, and YM events on the shared master-clock timeline. NTSC/PAL and bus ownership
  affect all conversions; use rational accumulators instead of rounded per-frame clock constants.
- The C routine model charges bus cycles and explicit waits, not instruction execution. Do not add
  per-game CPU cost profiles; those belong to Stage 4. Compare candidate Z80 traces by access order,
  values, and explicitly paired device offsets; do not compare raw absolute clocks against a C
  routine that does not execute the candidate's Z80 instructions.
- `kit/` remains console-neutral and the SNES stays output-identical. Put all Mega Drive names and
  device code under `consoles/md/`.
- Carry forward the Stage 0 gates on every task: the nine configured builds, consumer, golden compare,
  kit/full test, SNES ROM comparison, and `golden.py time` at or below 23.07 seconds CPU. Record
  host-limited failures instead of treating them as passes.
- Keep all ROM fixtures synthetic. Do not commit ROM bytes, game code, graphics, or ROM paths.
- BlastEm remains a candidate oracle, not hardware qualification. Retain only individually reasoned
  candidate differences in `consoles/md/tests/rom/KNOWN`.
- Do not freeze the public `nmd` contract at 1.0 until Stage 4.

## Review Focus

1. BUSREQ or RESET asserted while the Z80 is running must stop it at the documented boundary,
   preserve or reset state as the hardware does, and never let the 68000 access Z80 RAM too early.
2. Serial bank writes must shift exactly nine low bits; bank-window accesses must map to the right
   68000 address and stall correctly against ordinary bus traffic and DMA.
3. Timer A/B overflow, reset, enable, and reload writes at the same clock as an access must not lose
   an IRQ or clear a status bit before software reads it.
4. PSG latch/data writes, tone divisor zero, noise clock source, and reset seed must remain defined
   across a partial write sequence and a frame boundary.
5. The audio queue must preserve sample order across wrap, underrun, power reset, and concurrent
   host draining without unbounded growth.

---

### Task 1: Add the Z80 routine contract and coroutine clock

**Files:**
- Create: `consoles/md/include/nmd/z80.h`, `consoles/md/src/z80/z80.c`
- Modify: `consoles/md/include/nmd/cart.h`, `consoles/md/include/nmd/nmd.h`,
  `consoles/md/src/console/md_internal.h`, `consoles/md/src/front/md_console.c`,
  `consoles/md/CMakeLists.txt`, `consoles/md/tests/CMakeLists.txt`
- Test: `consoles/md/tests/unit/z80_check.c`

**Interfaces:**
- `typedef struct nmd_z80_routine { uint16_t addr; void (*abi)(void); const char *name; }
  nmd_z80_routine` is sorted by entry address. `nmd_cart` gains
  `const nmd_z80_routine *z80_routines` and `size_t z80_routine_count`.
- `typedef struct nmd_z80_state { uint8_t a,f,b,c,d,e,h,l,i,r,iff1,iff2,im; uint16_t ix,iy,sp,pc; }
  nmd_z80_state` is exposed as `extern nmd_z80_state nmd_z80`.
- Game-facing calls are `uint8_t zrd(uint16_t addr)`, `void zwr(uint16_t addr,uint8_t value)`,
  `uint8_t zin(uint8_t port)`, `void zout(uint8_t port,uint8_t value)`,
  `uint64_t nmd_z80_wait_until(int (*ready)(void *ctx),void *ctx)`,
  `void nmd_z80_call(uint16_t addr,uint16_t ret)`, `uint16_t nmd_z80_ret(void)`, and
  `void nmd_z80_restart(uint16_t addr)`.
- One Z80 T-state is 15 master clocks. Memory accesses cost three T-states and I/O accesses four;
  waits advance the coroutine one T-state at a time. The shared scheduler catches the Z80 up before
  68000-visible device observations.

- [ ] **Step 1: Write `test_z80_reset_gate`, `test_z80_routine_lookup_and_return`,
  `test_z80_coroutine_resume`, and `test_z80_tick_conversion`** in `z80_check.c`. Assert that reset
  is held on power, the `$0000` callback does not run until reset release, BUSREQ stops execution
  at a bus-call boundary, release resumes the saved state, and a new RESET clears it.
- [ ] **Step 2: Run `mise run test:md-z80` and verify the lifecycle cases fail** before supplying the
  coroutine and routine table.
- [ ] **Step 3: Implement `nmd_z80_state`, address-sorted routine lookup, call/return/restart, clock conversion, and
  `coro_new`/`coro_switch` lifecycle** in `src/z80/z80.c`; connect power/reset and scheduler hooks.
- [ ] **Step 4: Run `mise run test:md-z80`** and verify entry, suspension, resume, and reset timing.
- [ ] **Step 5: Commit** the Z80 runtime and contract changes.

### Task 2: Implement the Z80 map, bank register, and arbitration

**Files:**
- Create: `consoles/md/src/z80/z80bus.c`
- Modify: `consoles/md/src/console/bus.c`, `consoles/md/src/console/dispatch.c`,
  `consoles/md/src/console/md_internal.h`, `consoles/md/CMakeLists.txt`,
  `consoles/md/tests/unit/z80_check.c`, `consoles/md/tests/bus_check.py`,
  `consoles/md/tests/rom/{mdtest.py,cases.py,run.py,testcart.c}`

**Interfaces:**
- `zrd/zwr` map `$0000-$1FFF` to Z80 RAM, `$4000-$4003` to YM2612, `$6000` to the serial bank
  register, and `$8000-$FFFF` to the 68000 bank window. `zin/zout` model the Z80 I/O port `$7F` PSG
  path; unmapped accesses fail with `nativemd:`.
- The bank register shifts the written LSB into address bits A15-A23. Bank-window accesses use the
  68000 memory map and contend for the same 68000 bus as 68000 accesses and DMA.
- `$A11100` and `$A11200` requests are synchronized with the Z80 coroutine; a 68000 access to Z80
  RAM remains illegal until BUSREQ acknowledgement.
- The ROM-case format can carry a Z80 assembly fixture and its C-routine entry address; the
  candidate executes the assembled Z80 program while `md_testcart` starts the matching native C
  routine from the same step table.
- The 68000-facing wrappers `md_ym2612_read`/`md_ym2612_write` and the Z80 decoder use the same
  timestamped device functions, so reads from either CPU observe one YM state.

- [ ] **Step 1: Add `test_z80_memory_map`, `test_bank_serial_select`, `test_z80_bank_window`,
  `test_busreq_boundary`, and `test_dma_bank_stall`**. Assert all map regions, each of nine serial
  bank bits, 24-bit bank-window addresses, BUSREQ acknowledgement, reset, prohibited accesses, and
  that a bank access waits until the pending DMA releases the 68000 bus.
- [ ] **Step 2: Run `mise run test:md-z80` and `mise run test:md-bus`**; verify illegal accesses and
  missing waits fail.
- [ ] **Step 3: Implement the Z80 address decoder and shared-bus arbitration** without allowing a
  Z80 window access to recursively run a second coroutine or skip a DMA event.
- [ ] **Step 4: Run both checks and `mise run test:md-interrupt`**; verify exact event ordering.
- [ ] **Step 5: Commit** the Z80 map and arbitration change.

### Task 3: Implement SN76489 register behavior and tone/noise generation

**Files:**
- Create: `consoles/md/src/sound/sn76489.c`, `consoles/md/src/sound/sound.h`
- Modify: `consoles/md/src/console/bus.c`, `consoles/md/src/z80/z80bus.c`,
  `consoles/md/src/console/md_internal.h`, `consoles/md/tests/unit/sound_check.c`,
  `consoles/md/tests/rom/{mdtest.py,cases.py,run.py,testcart.c}`

**Interfaces:**
- `md_psg_write(uint8_t,uint64_t)` accepts the 68000 `$C00011` and Z80 `$7F` write paths at their
  actual master clocks. `md_psg_advance_to(uint64_t)` steps tone and noise dividers from the
  region-specific PSG clock.
- The four channels are three tone generators plus noise; latch/data writes update a 10-bit tone
  divider or four-bit attenuation. Noise control selects periodic/white feedback and a tone-3 or
  fixed clock source.

- [ ] **Step 1: Add `test_psg_latch_data`, `test_psg_period_zero`, `test_psg_noise_modes`, and
  `test_psg_attenuation`**, plus `sound_psg_tone` and `sound_psg_noise` ROM cases. Assert latch
  continuation, all channels, mute/volume values, noise reset, and 68000/Z80 event order.
- [ ] **Step 2: Run `mise run test:md-sound` and focused ROM cases**; ensure a changed latch byte,
  divisor, or LFSR bit changes the asserted state/output.
- [ ] **Step 3: Implement the serial PSG register latch, counters, LFSR, and timestamped writes** in
  `sn76489.c` using the TI datasheet and Sega's sound manual.
- [ ] **Step 4: Run unit and oracle tone/noise cases**; compare per-chip output samples at the
  candidate stream's declared rate.
- [ ] **Step 5: Commit** PSG register/tone behavior before mixing it with YM output.

### Task 4: Implement YM2612 ports, busy state, timers, IRQ, and DAC

**Files:**
- Create: `consoles/md/src/sound/ym2612.c`
- Modify: `consoles/md/src/console/bus.c`, `consoles/md/src/z80/z80bus.c`,
  `consoles/md/src/console/dispatch.c`, `consoles/md/src/console/md_internal.h`,
  `consoles/md/tests/unit/sound_check.c`, `consoles/md/tests/rom/{cases.py,run.py,testcart.c}`

**Interfaces:**
- `md_ym_read(uint8_t port,uint64_t clock)` and `md_ym_write(uint8_t port,uint8_t value,uint64_t clock)`
  serve 68000 byte ports `$A04000-$A04003` and Z80 memory `$4000-$4003`.
- The device models both register banks, BUSY timing, Timer A/B load/enable/reset/overflow flags,
  Z80 IRQ assertion, and channel-six DAC enable/data before operator synthesis is added.
- Timer and busy deadlines are exposed to the shared scheduler; status reads observe and clear the
  documented overflow flags without dropping a same-clock event.

- [ ] **Step 1: Add `test_ym_ports_busy`, `test_ym_timer_a`, `test_ym_timer_b_irq`, and
  `test_ym_dac_samples`**, plus synthetic cases for both port banks, address/data sequencing,
  byte-only access, timer periods/reset/overflow, IRQ masking/acknowledgement, and DAC sample edges.
- [ ] **Step 2: Run `mise run test:md-sound` and focused candidate cases**; verify each timer and
  status edge fails before state/deadline handling exists.
- [ ] **Step 3: Implement timestamped YM ports and timer/IRQ/DAC state** in `ym2612.c`; add timer
  deadlines to `md_advance_to` while preserving 68000/VDP/Z80 event order.
- [ ] **Step 4: Run the sound and interrupt checks** and compare YM port values, BUSY/overflow flags,
  timer IRQ clocks, and DAC samples with the candidate.
- [ ] **Step 5: Commit** register/timer/DAC behavior before implementing FM operators.

### Task 5: Implement YM2612 FM channels and envelopes

**Files:**
- Modify: `consoles/md/src/sound/ym2612.c`, `consoles/md/src/sound/sound.h`,
  `consoles/md/tests/unit/sound_check.c`, `consoles/md/tests/rom/cases.py`

**Interfaces:**
- The six channels have four operators each. Model frequency/block, detune/multiple, total level,
  attack/decay/sustain/release, key-on/off, feedback/algorithms, left/right enable, LFO, channel-3
  special frequencies, and DAC replacement on channel six.
- `md_ym_advance_to(uint64_t)` advances the YM chip at the region's chip clock and emits samples in
  deterministic order. Use fixed integer tables/state where the reference hardware is discrete.

- [ ] **Step 1: Add `test_ym_algorithm_0` through `test_ym_algorithm_7`,
  `test_ym_envelope_key_on_off`, and `test_ym_channel3_and_lfo`**, plus sample vectors for operator
  routing, frequency/block, envelope stages, pan, and DAC/FM channel-six selection.
- [ ] **Step 2: Compare oracle chip streams for those vectors** and retain the first differing
  sample/index as failure output.
- [ ] **Step 3: Implement operator phase, attenuation/envelope, feedback, and channel connection
  state** from Sega's Genesis Sound Software Manual; keep register writes and sample generation on
  the shared scheduler.
- [ ] **Step 4: Run the sound unit suite and full per-chip oracle vectors**; list only individually
  reviewed candidate differences in `rom/KNOWN`.
- [ ] **Step 5: Commit** the FM synthesis engine with its vectors.

### Task 6: Mix chip samples and feed `nk_console` audio

**Files:**
- Create: `consoles/md/src/sound/mix.c`
- Modify: `consoles/md/src/front/md_console.c`, `consoles/md/src/console/dispatch.c`,
  `consoles/md/CMakeLists.txt`, `consoles/md/tests/rom/testcart.c`,
  `consoles/md/tests/rom/run.py`, `consoles/md/tests/rom/cases.py`,
  `consoles/md/tests/unit/sound_check.c`

**Interfaces:**
- `md_sound_advance_to(uint64_t)` brings PSG/YM to the shared master clock, resamples their streams
  to `nmd_console.audio_hz`, applies the documented digital mix, and writes stereo frames to a
  bounded SPSC queue. `audio_take` drains frames and `audio_queued` reports queued frames.
- `md_sound_reset(region)` resets phases, timers, pending events, and ring indices at power on. The
  host may drain audio concurrently; the emulator producer never waits for the host callback.
- The oracle runner manifest records each stream's sample rate, channel count, and frame count. The
  testcart emits the same raw format for mixed output and each chip.

- [ ] **Step 1: Add failing integration tests** for empty/startup audio, deterministic same-run PCM,
  combined PSG/YM output, PAL and NTSC sample counts, queue wrap/full/empty, and reset between runs.
- [ ] **Step 2: Extend the synthetic oracle runner** to compare complete PCM streams and report the
  first differing sample, channel, and rate; prove it fails after a planted sample change.
- [ ] **Step 3: Implement the bounded audio queue and resampling** with atomics matching the kit's
  SPSC audio contract; wire `audio_take`/`audio_queued` to `md_sound`.
- [ ] **Step 4: Run per-chip and mixed sample comparisons** with playback filters disabled and no
  game ROMs in the repository.
- [ ] **Step 5: Commit** sound integration and audio comparisons.

### Task 7: Complete Stage 3 gates and branch review

**Files:**
- Modify: `consoles/md/tests/README.md`, `consoles/md/docs/nativemd.md`,
  `docs/superpowers/plans/2026-11-03-nativemd-stage3-z80-sound.md`
- Modify: root `mise.toml` and the MD test runner wiring

- [ ] Ensure every Stage 3 requirement has a named unit or synthetic-ROM case, and every case either
  matches the candidate or has an individually reviewed `KNOWN` reason.
- [ ] Run `mise run tools:md`, `mise run test:md-oracle-smoke`, `mise run test:md-rom`, the MD unit
  checks, and the Stage 0 gates. Record toolchain-limited failures and sample/log paths without
  including user ROM paths.
- [ ] Request a fresh whole-branch review; resolve Critical/Important findings before Stage 3 is
  complete. Do not record the Stage 4 golden master here.
- [ ] Commit the reviewed Stage 3 result. Stage 4 requires its own plan.

## Source References

- Sega, [Genesis Technical Overview 1.00](https://segaretro.org/images/1/18/GenesisTechnicalOverview.pdf):
  Z80 map, BUSREQ/RESET protocol, serial bank register, YM port/status/timers/DAC registers, and PSG
  port address.
- Sega, [Genesis Sound Software Manual](https://fabiensanglard.net/another_world_polygons_Genesis/GenesisSoftwareManual.pdf):
  YM2612 operator/register layout, envelopes/algorithms, and PSG programming.
- Sega, [IC specifications](https://gendev.spritesmind.net/files/md/tech/Sega%20IC%20specifications%20A4.pdf):
  NTSC/PAL chip-clock signals used to derive rational master-clock conversions.
- Zilog, [Z80 CPU User Manual](https://www.zilog.com/docs/z80/um0080.pdf): Z80 machine cycles,
  interrupt modes, and memory/I/O access timing.
- Texas Instruments, [SN76489AN data sheet](https://map.grauw.nl/resources/sound/texas_instruments_sn76489an.pdf):
  tone/noise generator and serial latch behavior. Confirm Genesis-specific quirks against Sega's
  manual and the pinned candidate; do not assume every TI revision is identical.
