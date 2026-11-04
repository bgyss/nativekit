# nativemd Stage 4: I/O, Cost Profiles, and Contract 1.0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this
> plan task-by-task. Each task ends with red/green evidence and a focused commit.

**Goal:** Complete the base Mega Drive game contract with controller I/O, version/TMSS behavior,
measured 68000 and Z80 cost profiles, and the release gates required for nmd and nativekit 1.0.

**Architecture:** A deep `src/io/` module owns the controller registers, TH state, version register,
and TMSS state; `bus.c` forwards mapped I/O accesses. The host input callback updates two pad
states through a small interface. The profile tool builds per-frame stats from candidate CPU traces
and calls the existing `nk_profile.py` fitter; the runtime applies fixed-point weights around the
68000 and Z80 C routine accesses. A selectable hardware model keeps the current `md1va3` baseline
and adds a TMSS-equipped `md1va6` path.

**Tech Stack:** C11, CMake, Python 3, existing libretro candidate trace ABI, and
`kit/tools/nk_profile.py`.

**Spec:** `docs/superpowers/specs/2026-11-03-nativemd-stage4-design.md`, the approved
`docs/superpowers/specs/2026-11-01-nativemd-design.md`, and
`docs/superpowers/handoff/2026-11-01-nativemd-handoff.md` (Stage 4).

## Global Constraints

- Implement fresh models from Sega documentation; do not copy emulator or reconstruction source.
- Keep the 68000, Z80, VDP, and sound devices on one master-clock timeline with rational conversions.
- Keep `kit/` console-neutral and the SNES golden/ROM/performance outputs unchanged.
- Keep all ROM fixtures synthetic. Accept user ROMs only as runtime paths and never write ROM paths
  or contents into checked-in artifacts.
- Keep BlastEm as candidate comparison evidence, not hardware qualification; only individually
  reasoned divergences belong in `consoles/md/tests/rom/KNOWN`.
- Record the nmd golden once after all Stage 4 cases pass. Freeze it afterward and freeze the public
  nmd headers and nativekit kit at 1.0 only after the consumer and release gates pass.

## Review Focus

1. 3- and 6-button reads must respect CTRL output/input direction, TH transitions, phase order,
   timeout, and changes in buttons across frames.
2. Version bits must match model/region, and TMSS must gate VDP accesses only on the TMSS model;
   `$A14000` word lanes and longword unlock order must be correct.
3. Fractional profile weights must carry across accesses and frames without losing clocks; turning
   68000 costs off must preserve VDP/ DMA/ interrupt waits, and Z80 profile costs must remain on.
4. Candidate statistics must partition at VBlank and exclude long polling/self-branch gaps without
   hiding busy frames or device waits.
5. Golden recording and profile generation must be deterministic, hash their inputs, and never
   replace a baseline to make an unexplained difference disappear.

---

### Task 1: Add the public I/O, model, pad, and profile contracts

**Files:**
- Modify: `consoles/md/include/nmd/{cart.h,nmd.h,hw.h}`
- Modify: `consoles/md/src/console/md_internal.h`, `consoles/md/src/front/md_console.c`
- Create: `consoles/md/src/io/{io.c,io.h}`
- Create: `consoles/md/tests/unit/io_check.c`
- Modify: `consoles/md/CMakeLists.txt`, `consoles/md/tests/CMakeLists.txt`, root `mise.toml`

**Interfaces:**
- Add zero-default model and pad enums: `NMD_MODEL_MD1VA3`, `NMD_MODEL_MD1VA6_TMSS`,
  `NMD_PORT_PAD6`, and `NMD_PORT_PAD3`.
- Forward-declare `nmd_cost_profile` in `cart.h`, then append `model`, `ports[2]`, and
  `const nmd_cost_profile *cost_profile` to `nmd_cart`. A zeroed cart selects `md1va3`, two
  6-button pads, and no profile.
- Add public `NMD_BUTTON_*` bits and two-player `nk_button` labels. Internal
  `md_io_input(buttons)` / `md_io_button_mask(port)` functions keep raw host masks separate from pad
  protocol state; the `nk_console.input` callback passes the two masks to `md_io`.
- Add test task `mise run test:md-io` for the new I/O unit executable.

- [x] **Step 1: Write failing contract and adapter checks.** Add
  `test_nmd_cart_zero_defaults`, `test_nmd_console_button_map`, and
  `test_nmd_console_input_forwards_players` in `io_check.c`. Assert the zeroed-cart defaults, pad
  button bit assignments, all twelve `nk_button` labels, and that the console input callback
  forwards both player masks.

- [x] **Step 2: Run the new I/O check and verify it fails** because model/pad/button contracts and
  input wiring are absent.

- [x] **Step 3: Add the public types and implement the minimal input adapter** in `md_console.c`.
  Keep hardware register behavior in later tasks.

- [x] **Step 4: Run `mise run test:md-io`** and verify defaults and both player masks.

- [x] **Step 5: Commit** the public Stage 4 types and host input adapter.

### Task 2: Implement the 3-button controller and I/O registers

**Files:**
- Modify: `consoles/md/src/io/io.c`, `consoles/md/src/io/io.h`
- Modify: `consoles/md/src/console/bus.c`
- Modify: `consoles/md/tests/unit/io_check.c`

**Interfaces:**
- `md_io_reset`, `md_io_input`, `md_io_read8/16`, and `md_io_write8/16` own `$A10001`, data
  registers `$A10003/$A10005`, and control registers `$A10009/$A1000B`.
- Reads combine driven output bits with the pad's active-low input bits. Port output follows CTRL;
  the 68000's effective I/O byte is the low lane.
- 3-button phases use D0-D3 direction, D4-D5 B/C when TH is high, and A/Start with the unused
  phase bits defined by Sega's port protocol when TH is low.

- [x] **Step 1: Add failing 3-button tests** named `test_io_3button_phase_reads`,
  `test_io_3button_ctrl_driven_output`, and `test_io_port_state_independent`. Pin initial port
  values, low/high TH reads, direction bits, output direction, high-byte writes, and both ports.

- [x] **Step 2: Run `mise run test:md-io` and verify these cases fail** against the currently open
  I/O map.

- [x] **Step 3: Implement the 3-button register state and bus delegation** in `src/io/io.c`.

- [x] **Step 4: Run `mise run test:md-io` and `mise run test:md-bus`**; verify access lanes, state,
  and bus timing.

- [x] **Step 5: Commit** the 3-button I/O module.

### Task 3: Add the 6-button TH sequence and per-frame input

**Files:**
- Modify: `consoles/md/src/io/{io.c,io.h}`
- Modify: `consoles/md/tests/unit/io_check.c`, `consoles/md/tests/rom/cases.py`,
  `consoles/md/tests/rom/run.py`, `consoles/md/tests/rom/testcart.c`

**Interfaces:**
- 6-button pads expose X/Y/Z/Mode only in the Sega-defined extended TH phase; ordinary 3-button
  reads remain compatible.
- Pad data settles 112 master clocks after a TH transition, matching Sega's four-NOP minimum.
- Each port keeps its own TH edge count and timeout. Input masks update at frame boundaries, while
  the current TH sequence and latched read state follow the hardware timing.
- Extend `Case` so candidate runs can select pad type and scripted frame input per port.

- [x] **Step 1: Add failing unit cases** `test_io_pad_data_settles_after_th_change`,
  `test_io_6button_extended_phase`,
  `test_io_6button_timeout_resets`, and `test_io_buttons_sampled_per_frame`, plus the synthetic
  ROM case `io_pad6_sequence`. Pin the extended phase, Mode/X/Y/Z bits, timeout, edge polarity, and
  both ports.

- [x] **Step 2: Run focused `test:md-io` and `test:md-rom -- --only CASE`** and verify the new reads
  or input snapshots differ before implementation.

- [x] **Step 3: Implement the TH transition/timeout state machine** from Sega's 6-button pad
  specification; keep pad state behind `md_io`.

- [x] **Step 4: Run the focused cases and the full `CC=/usr/bin/clang mise run test:md-rom`**.
  Candidate comparisons must match exactly at the bus event boundary; any hardware divergence gets
  its own `KNOWN` reason.

- [x] **Step 5: Commit** the 6-button protocol and candidate input fixture support.

### Task 4: Implement the version register and selectable TMSS behavior

**Files:**
- Modify: `consoles/md/src/io/{io.c,io.h}`, `consoles/md/src/console/bus.c`
- Modify: `consoles/md/tests/unit/io_check.c`, `consoles/md/tests/bus_check.py`
- Modify: `consoles/md/tests/rom/{cases.py,run.py,testcart.c}`
- Modify: `consoles/md/tests/oracle/run.py`

**Interfaces:**
- `$A10001` reports the model and NTSC/PAL version bits; the `md1va3` zero-value model has TMSS
  absent, while `md1va6` has a nonzero version nibble and TMSS enabled.
- `$A14000/$A14002` hold the two 16-bit halves of the TMSS signature. On the TMSS model, VDP
  accesses are locked until the longword sequence writes `0x53454741`; on `md1va3`, those writes
  are ignored and VDP access starts unlocked.
- The candidate runner accepts a per-case model so the TMSS synthetic fixture uses the matching
  BlastEm model; the existing 56 cases stay on `md1va3`.

- [x] **Step 1: Add failing unit tests** `test_io_version_register_model_bits`,
  `test_io_tmss_absent_is_open`, `test_io_tmss_lock_unlock_sequence`, and
  `test_io_tmss_vdp_locked`. Pin model/region bits, both data lanes, partial signature writes, the
  complete `SEGA` unlock, and VDP access while locked.

- [x] **Step 2: Add a matching TMSS candidate case and run it** before implementation; verify the
  candidate loads `md1va6` and the native check fails at the unimplemented `$A14000` write.

- [x] **Step 3: Implement version/TMSS state and VDP lock checks** in the I/O module and bus path.

- [x] **Step 4: Run `mise run test:md-io`, `mise run test:md-bus`, and focused TMSS ROM cases** for
  both model variants.

- [x] **Step 5: Commit** version/TMSS support.

### Task 5: Apply measured cost profiles in both routine models

**Files:**
- Modify: `consoles/md/include/nmd/{cart.h,nmd.h}`, `consoles/md/src/console/{bus.c,md_internal.h}`,
  `consoles/md/src/z80/z80.c`, `consoles/md/src/z80/z80bus.c`
- Modify: `consoles/md/tests/unit/{bus_check.c,z80_check.c}`, `consoles/md/tests/CMakeLists.txt`,
  and root `mise.toml`

**Interfaces:**
- Add `NMD_KINDS` and `NMD_Z80_KINDS` access-kind enums and `nmd_cost_profile` with read/write
  rows. The 68000 weights use tenths of a master clock; Z80 weights use tenths of a T-state.
- `nmd_set_cpu_costs(int)` toggles 68000 bus/profile costs, while waits, interrupts, DMA and Z80
  sound costs remain active. A NULL profile keeps additional weights at zero.
- Expose kind names and `counts_hooks` through `nmd_console` for the generic fitter's native count
  check.

- [x] **Step 1: Add failing cost tests** `test_m68k_cost_profile_kind_weights`,
  `test_m68k_cost_profile_tenths_carry`, `test_z80_cost_profile_tstates`,
  `test_cost_profile_null_is_zero`, and `test_m68k_costs_off_keeps_devices_running`. Pin known
  read/write weights, fractional carry, Z80 conversion, NULL profile, and costs-off while VDP/DMA
  deadlines advance.

- [x] **Step 2: Run `test:md-bus` and `test:md-z80`**; verify the profile-driven clock deltas fail
  against the current fixed access costs.

- [x] **Step 3: Implement fixed-point profile accounting** at the 68000 and Z80 access seams. Keep
  bank/DMA waits separate from profile weights.

- [x] **Step 4: Run `mise run test:md-bus`, `test:md-z80`, `test:md-interrupt`, and the MD ROM matrix**.

- [x] **Step 5: Commit** profile runtime behavior and access-kind reporting (`83f7691`).

### Task 6: Build `nmd-profile` and its reference statistics

**Files:**
- Create: `consoles/md/tools/nmd-profile`, `consoles/md/tests/profile_check.py`,
  `consoles/md/tests/unit/profile_decomp_fixture.c`
- Modify: `consoles/md/tests/oracle/run.py`, `consoles/md/tests/oracle/trace.py`,
  `consoles/md/tests/oracle/TRACE_FORMAT.md`, `consoles/md/tests/feature_check.py`,
  `consoles/md/tests/CMakeLists.txt`, `consoles/md/tests/README.md`,
  `consoles/md/tools/README.md`, `consoles/md/src/front/md_console.c`,
  `consoles/md/tests/unit/bus_check.c`, `kit/include/nk/nk.h`, `kit/src/front/app.c`,
  `kit/tests/kinds_check.c`, `kit/tests/kinds_check.py`, `kit/docs/kit.md`, root `mise.toml`

**Interfaces:**
- `nmd-profile measure ROM... --out DIR` requests per-VBlank CPU statistics from the pinned
  candidate, fits 68000 and Z80 frame rows with `nk_profile.fit_weights`, and writes a versioned
  profile, generated C table, and error report. Optional input spans use the oracle's JSON input
  format; optional `--decomp` runs native `--counts` and reports per-kind count differences.
- `nmd-profile check PROFILE --source DIR` compares the stored hash with a sorted relative-path and
  file-content hash. Reports contain ROM hashes only; ROM paths and contents remain external.
- A `nk_console.count_group` callback keeps the read-poll filter independent for each processor;
  its default group is 0. nmd maps the Z80 marker to group 1 so one CPU's writes cannot flush the
  other CPU's pending reads.

- [x] **Step 1: Add failing profile tests** `test_stats_rows_fit_known_weights`,
  `test_poll_and_gap_filters`, `test_profile_text_and_c_generation`,
  `test_profile_source_hash_is_deterministic`, `test_profile_stale_source_warning`, and
  `test_profile_reports_no_rom_paths`. Also cover missing VBlank rows, candidate sidecar generation,
  native `--counts`, width-4 68000 access splitting, per-processor count ratios, and feature-manifest
  compatibility. Use generated trace rows with known values, a planted change to reference
  statistics, and a changed source file.

- [x] **Step 2: Run the new profile checks and confirm they fail** because the statistics sidecar
  and CLI do not exist. The first run reported `--cpustats` as unknown and the profiler entrypoint
  missing; the count-group check also failed before the public callback was added.

- [x] **Step 3: Extend the oracle runner's `--cpustats` sidecar** with one row per CPU per VBlank:
  access counts, per-CPU candidate clock deltas, and nominal bus clocks. Fetch intervals accrue as
  work; the candidate exposes access completion timestamps, so dynamic waits can remain in residual
  work and are called out as a limitation. Apply the 5% active-frame, 1,000 accesses/kind, >64
  read-only poll, and >1,024 master-clock gap rules.

- [x] **Step 4: Implement `nmd-profile` using the stats rows and `nk_profile.py`**, writing
  `profile.txt`, `profile.c`, and `report.txt` without ROM paths or bytes.

- [x] **Step 5: Run `mise run test:md-profile`, `mise run test:md-toolchain`, and the feature/survey
  checks.** Verify the generated profile compiles against the public `nmd` headers.

- [x] **Step 6: Commit** the profile tool, trace stats, and generated-profile checks (`a75667b`).

### Task 7: Close the reference matrix, consumer, golden, and public docs

**Files:**
- Create: `consoles/md/tests/consumer/{CMakeLists.txt,main.c,run.py}`,
  `consoles/md/tests/golden.py`, `consoles/md/tests/golden_check.py`,
  `consoles/md/docs/contract.md`, `consoles/md/include/nmd/version.h`
- Modify: `consoles/md/tests/rom/{cases.py,run.py,KNOWN}`, `consoles/md/tests/README.md`,
  `consoles/md/docs/{nativemd.md,survey.md}`, `consoles/md/tools/README.md`,
  root `README.md`, `CMakeLists.txt`, `kit/VERSION`, `kit/docs/contract-discipline.md`,
  `consoles/md/CMakeLists.txt`, `consoles/md/include/nmd/*.h`, root `mise.toml`

**Interfaces:**
- The consumer demonstrates a separate CMake project adding nativekit and linking `nmd` through
  `add_subdirectory`.
- Wire `mise run test:md-consumer` to configure, build, and run the MD consumer headless.
- The golden runner follows the existing SNES workflow and records deterministic outputs for the
  synthetic MD cases only. Record once after the reference matrix passes.
- Public nmd headers report 1.0.0; the contract document pins register, timing, input, cost-profile,
  error and compatibility behavior. Bump `kit/VERSION` only after all public-header consumers pass.

- [x] **Step 1: Add failing consumer and golden checks.** Add `test:md-consumer` and make the golden
  comparator detect planted pixel, bus, and PCM changes. The consumer must build and run headless.

- [x] **Step 2: Add all Stage 4 synthetic cases** for 3/6 pads, TH sequencing, version bits, both
  model/TMSS paths, and controller input snapshots. Add individual `KNOWN` reasons only for
  verified candidate divergences. Evidence: the full 59-case matrix includes the Stage 4 pad,
  version, TMSS, and scripted-input cases; no additional `KNOWN` rows were needed.

- [x] **Step 3: Run the full reference matrix and consumer**; require 0 unlisted differences and
  record the final synthetic golden master once. `test:md-rom` reported 0 of 59 unlisted, six
  documented candidate differences, and 0 known cases newly matching. The consumer passed and the
  59-case golden baseline was recorded and compared with 0 differences.

- [x] **Step 4: Write `contract.md`, update the support docs, and bump nmd/nativekit versions** after
  the consumer and all header checks pass. The standalone consumer compiles every public nmd
  header and asserts the 1.0.0 version macros; it builds and runs headless.

- [x] **Step 5: Commit** the Stage 4 reference, golden, consumer, and documentation results
  (`1c47b06`).

### Task 8: Run final Stage 4 gates and review the branch

**Files:**
- Modify: `docs/superpowers/plans/2026-11-03-nativemd-stage4.md`,
  `docs/superpowers/handoff/2026-11-01-nativemd-handoff.md`

- [x] Run `mise run build`, the MD unit/profile/oracle/ROM tasks, `mise run test:md-consumer`,
  `mise run test`, SNES golden/ROM/time gates, and `git diff --check`. Record host/toolchain
  limitations and preserve the six Stage 3 known cases. Evidence: all MD unit, profile, toolchain,
  oracle-smoke, trace, feature, survey, consumer, ROM, kit, and golden checks passed; `test:md-rom`
  reported 0 of 59 unlisted differences, six documented candidate differences, and 0 known cases
  newly matching. `golden:md-compare` reported 0 of 59 differences. The frozen 59-case manifest has
  SHA-256 `e9eb3bdacc221fd3e27b162e21156c046584afd93a91cfc6a2b0fa52a22aec97`.

  SNES preservation gates passed: `golden:compare` reported 0 of 2393 differences, `test:rom`
  reported 0 of 1164 unlisted differences (56 documented known, 0 newly matching), and the minimum
  golden timing was 20.3 s CPU against the 23.07 s limit. `mise run build` completed eight of nine
  configured targets; Windows failed at the `hello.exe` link because Nix MinGW lacks
  `libmcfgthread.a`. iOS returned success with stale CoreSimulator/Xcode warnings. `mise run test`
  had four host/render failures: the macOS pointer check trapped after Launch Services returned
  `LSModifyNotification -50`, and null Metal, GL, and Vulkan overlay checks could not open a
  renderer (`vkCreateInstance -9`). Other repository checks passed. `git diff --check` passed.
- [x] Request a fresh Stage 4 whole-branch review. No Critical or Important issues remained; the
  review-driven I/O, profile-counting, and poll-frame attribution fixes were applied and the
  affected MD, profile, and kit gates passed afterward. A minor limitation remains: the native
  testcart currently consumes only the first `input_frames` snapshot; the committed fixture uses
  one snapshot, while per-frame input sampling is covered by the I/O unit check.
- [x] Record the frozen nmd golden/version evidence, update the handoff to the next requested
  objective (first-game selection from the survey), and commit the final Stage 4 result.

## Source references

- Sega, [Genesis Technical Overview 1.00](https://segaretro.org/images/1/18/GenesisTechnicalOverview.pdf):
  I/O register map and version register fields.
- Sega, [Genesis Software Manual](https://segaretro.org/images/9/95/GenesisSoftwareManual.pdf):
  the version check and TMSS signature unlock sequence.
- Sega, [Software Production Specifications for Mega Drive 6-Button Control Pad](https://gendev.spritesmind.net/files/md/tech/Software%20Production%20Specifications%20for%20Mega%20Drive%206-Button%20Control%20Pad%20%5B1993-05-14%5D.pdf):
  controller phase and reset-time behavior.
- `kit/tools/nk_profile.py`, `consoles/snes/tools/nshvc-profile`, and
  `consoles/snes/docs/cost-profile.md`: NNLS fitter, measurements, and profile hash format.
