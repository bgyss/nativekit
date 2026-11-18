# nativemd Stage 4: I/O, Cost Profiles, and Contract 1.0 — Design

**Status:** Approved 2026-11-04. The user approved the selectable md1va3/TMSS model and separate
68000/Z80 cost profiles as drafted. Derived from the approved nativemd specification and the scope
in `docs/superpowers/handoff/2026-11-01-nativemd-handoff.md`.

## Intent

Complete the base Mega Drive contract so games can read 3-button and 6-button pads, observe the
system version, unlock VDP access on TMSS-equipped hardware, and use measured processor cost
profiles. Close the candidate comparison, record the nmd golden master once, add an MD consumer
build, document the stable contract, and move nativekit and nmd to version 1.0.

The project remains platform work. Stage 4 does not choose or decompile a game; game selection
remains after Stage 4 and uses the existing local ROM survey.

## Current state

Stage 3 is committed as `b4b8f76`. The 68000 and Z80 C routine models share one master-clock
scheduler. The bus decodes the I/O address window, but reads currently fail and most writes are
open. `md_console.input` discards host and scripted buttons, `nmd_cart` has no controller or cost
profile fields, and the public nmd headers have no 1.0 contract document or version constants.

The kit already supplies two players, button tables, per-frame input, access-kind reporting, and the
generic cost-profile fitter in `kit/tools/nk_profile.py`. The SNES contract provides the repository
pattern for controller selection and optional cost profiles. The MD oracle runner already accepts
3-button and 6-button candidate pads and currently defaults to BlastEm's `md1va3` model, which has
TMSS disabled.

## Goals

- Add a deep `src/io/` module that owns port registers, the two controller state machines, system
  version reporting, and TMSS lock/unlock state. `bus.c` decodes addresses and delegates to it.
- Feed two button masks from the kit into the pads. Support 3-button and 6-button input with the
  Sega TH transition sequence and six-button identification timeout. Default zero-initialized
  cartridge settings to two 6-button pads, matching the oracle runner's current default.
- Represent both the current non-TMSS reference model and a TMSS-equipped revision. The proposed
  public selector is appended to `nmd_cart`; a zeroed cart keeps the current `md1va3` reference
  behavior. The TMSS model reports its version, locks VDP accesses until the game writes `SEGA` to
  `$A14000`, and does not emulate the boot ROM or trademark splash screen.
- Add a versioned nmd cost profile with separate 68000 and Z80 access kinds. `nmd-profile` measures
  reference bus traces and fits weights through `nk_profile.py`; the profile adds measured C work
  around the hardware bus cycles and explicit waits. A missing profile means zero extra cost.
- Keep input timing, profiles, version/TMSS, and the new profile tool covered by named unit and
  synthetic-ROM cases. Extend the oracle runner so a case can select the TMSS-equipped candidate
  model for its TMSS fixtures.
- Freeze all public `include/nmd/` headers at 1.0.0, add `contract.md`, update the console docs and
  survey, add an nmd consumer project and golden master, and move `kit/VERSION` to 1.0.0 as the
  handoff requires.

## Non-goals

No multitap, mouse, light gun, SRAM/EEPROM, mapper, Sega CD, 32X, SVP, Master System mode, boot-ROM
emulation, game selection, or Linux verification. No ROM bytes, game code, graphics, or private ROM
paths enter the repository. BlastEm remains candidate comparison evidence, not hardware
qualification.

## Approaches and calls

### Hardware model selection

1. **Selectable models (recommended):** retain the current zero-value `md1va3` contract and add a
   TMSS-equipped model for direct tests and games that require it. Candidate fixtures select the
   corresponding BlastEm model. This preserves all existing cases while testing TMSS explicitly.
2. **One TMSS-equipped model:** make every nmd run require the `SEGA` unlock and switch the full
   oracle matrix to a TMSS model. This is simpler at runtime, but changes the current reference
   configuration and every test cartridge's startup sequence.

Use the selectable-model approach because the existing matrix is pinned to `md1va3`, while the
handoff also calls for TMSS behavior. Keep `md1va3` as the zero-value default and test the TMSS model
in explicit candidate fixtures.

### Cost profiles

Use separate weight arrays for the 68000 and Z80 C routine models. Stage 3 has two instructionless
routine models, and the SNES profile already measures its main and sound CPUs separately. Apply
measured extra work around each CPU's explicit accesses while retaining hardware bus cycles and
waits. `nmd_set_cpu_costs()` controls the 68000 mode as its SNES counterpart does; Z80 sound timing
and its profile remain active. `nmd-profile` measures only ROMs supplied at runtime and emits a
versioned text file, generated C table, and report; it never copies ROM bytes or records local ROM
paths.

The measurement contract follows `nshvc-profile`: `measure ROM [ROM...] --out DIR [--frames N]
[--span INPUT.json]... [--decomp BUILD] [--source DIR]` runs the reference for an attract span and
optional scripted-input spans. Extend the oracle runner with a `--cpustats` sidecar that groups
timestamped fetch/read/write events by processor and VBlank interval. Each row contains a frame
number, read/write counts per access kind, CPU work clocks from the reference cycle counter, and
bus clocks summed from trace-event access durations. The fit row's work gap is CPU work minus bus
clocks. The row builder drops stretches of over 64 reads without a write and single gaps over
1,024 master clocks, following the existing SNES profile convention for polling loops and
self-branches. `nmd-profile` passes those frame rows to `nk_profile.fit_weights` separately for the
68000 and Z80, using the fixed 5% active-frame threshold and minimum 1,000 measured accesses per
kind. Profile values are stored as tenths of a master clock for the 68000 and tenths of a Z80
T-state for the Z80. The profile records ROM hashes and an optional source hash; that hash uses
sorted repo-relative file names and contents, as `nshvc-profile` does. Optional `--decomp` runs the
native build in `--counts` mode and reports access-count differences, while `nmd-profile check
PROFILE --source DIR` detects a stale decomp hash. The tool emits no ROM paths, ROM contents, or
title names in the checked-in artifacts.

## Interfaces and module seams

- Append `model`, `ports[2]`, and optional `cost_profile` fields to `nmd_cart`. Zeroed carts select
  the non-TMSS model, two 6-button pads, and no profile. These defaults preserve callers that
  initialize only today's fields.
- Add `NMD_MODEL_MD1VA3` and `NMD_MODEL_MD1VA6_TMSS`, `NMD_PORT_PAD3` and `NMD_PORT_PAD6`, button
  bit constants, access-kind enums, and `nmd_cost_profile` to `include/nmd/`. `NMD_MODEL_MD1VA6_TMSS`
  reports a nonzero version nibble, matching BlastEm's TMSS model; the region selects NTSC/PAL clock
  bits.
- Add public `nmd_set_cpu_costs(int on)` for the 68000's profile and bus-time mode. Keep the Z80
  profile always active when supplied so sound-driver pacing follows its measured work.
- Add an internal `md_io_read8`, `md_io_read16`, `md_io_write8`, and `md_io_write16` seam. Keep port
  state, TH transitions, direction bits, timeout, and version/TMSS register handling behind this
  module. `md_console.input` translates kit button masks into the port's current pad state.
- Keep reference measurement separate from profile application: the tool reads candidate trace
  events and calls `nk_profile.fit_weights`; the runtime only applies the versioned fixed-point
  profile declared by the cartridge.

## Verification and release gates

- Unit cases cover each controller phase, three- versus six-button response, opposite directions,
  TH direction and timing, timeout, version bits, TMSS VDP lock/unlock, per-kind cost application,
  disabled costs, and profile version errors.
- Synthetic ROM cases compare input polling, 3/6-button TH sequences, version reads, and TMSS
  unlock against the matching BlastEm model. `run.py` reports only individually reasoned `KNOWN`
  differences. Candidate input remains scripted and deterministic.
- `nmd-profile` tests use generated trace rows with known weights and check its text/C output,
  report calculations, runtime-only ROM paths, and stale-source check.
- Record the nmd golden master only after all Stage 4 comparison cases and the consumer pass. Do not
  update it afterward to hide a difference. Verify all nine configured builds, the full `mise run
  test` suite, SNES golden/ROM/performance gates, `test:md-rom`, and the local survey checks.
- Do not claim hardware accuracy or title compatibility from BlastEm alone. Keep the six Stage 3
  known differences visible and preserve host limitations in the gate report.

## Source references

- Sega, [Genesis Technical Overview 1.00](https://segaretro.org/images/1/18/GenesisTechnicalOverview.pdf),
  I/O ports, version register, and TH pin direction.
- Sega, [Genesis Software Manual](https://segaretro.org/images/9/95/GenesisSoftwareManual.pdf),
  version test and TMSS `$A14000` unlock sequence.
- Sega, [Software Production Specifications for Mega Drive 6-Button Control Pad](https://gendev.spritesmind.net/files/md/tech/Software%20Production%20Specifications%20for%20Mega%20Drive%206-Button%20Control%20Pad%20%5B1993-05-14%5D.pdf),
  six-button phase and timeout behavior.
- The approved nativemd design and SNES [cost-profile contract](../../../consoles/snes/docs/cost-profile.md)
  establish the repository's console and profile conventions.
