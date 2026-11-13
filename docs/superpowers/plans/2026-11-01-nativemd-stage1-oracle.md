# nativemd Stage 1: Tooling, Oracle, and Survey Implementation Plan

> **For agentic workers:** Use the repository's plan-execution and test-driven
> development workflows. Complete tasks in order; do not claim an oracle capability
> until the corresponding synthetic cartridge proves it.

**Goal:** Build a reproducible local BlastEm reference and survey the supplied
cartridge library without committing cartridge contents or paths.

**Architecture:** A pinned libretro core receives a small, versioned trace-hook
patch. A headless runner drives synthetic test cartridges and captures frames,
audio and clocked events. A separate survey consumes those events and emits
aggregate and per-title feature observations. The core and assembler remain
local test tools under `build/`.

**Tech Stack:** C11, Python 3, CMake, shell, libretro, BlastEm, vasm, Nix and Mise.

**Spec:** `docs/superpowers/specs/2026-11-01-nativemd-design.md` (sections 3–4).
The investigation and candidate revisions are in `docs/research/nativemd-oracle-spike.md`.

## Global Constraints

- Preserve the SNES golden master (0 of 2393 differ), reference comparison (0 of
  1164 differ, 56 documented), public headers and performance gate (≤ 23.07 s CPU).
- No game ROM, code, graphics, paths or credentials enter Git; the input directory
  is an argument. Synthetic cartridges contain only newly authored test code.
- Do not ship the reference core or assembler. Preserve their notices and document
  vasm's non-commercial restriction and source provenance.
- `kit/` stays console-neutral. Tests use the Nix shell and the existing Mise gates.
- BlastEm's local cycle counters are periodically adjusted. All trace clocks must
  use an explicitly tested epoch; distinguish accesses, widths, CPUs and fetches.

## Review Focus

1. An unknown, truncated, or unsupported trace version fails visibly; Task 3 tests it.
2. Counter adjustment never makes an event clock go backward; Task 3 crosses an adjustment.
3. Fast-memory and mapped paths both appear in CPU traces; Task 4 tests both.
4. A failed or unsupported ROM run is reported as such, never counted as absence of a
   feature; Task 6 tests failure and partial results.
5. Survey output contains no input paths or ROM bytes, including on error; Task 6 tests
   path-bearing titles and failure messages.

## Tasks

### 1. Pin and build the local tools

**Files:** `tools/toolchains/pins.env`, `tools/toolchains/setup.sh`, `mise.toml`,
toolchain documentation and synthetic assembly fixtures under `consoles/md/tests/`.

The candidate libretro BlastEm revision is `2230addc4dd74dd094fd264987fd808abba32d0e`;
the candidate vasm mirror revision is `685a87e5ed14285350ccdb6581c9771bc8df6c7d`.
Before pinning the latter, verify its relationship to an author-published 1.9d
distribution and applicable module terms; record what was and was not verified.

- [x] Add a test that builds both assemblers and asserts 68000 `moveq #1,d0; bra.s *`
  emits `700160fe` and Z80 `ld a,42; halt` emits `3e2a76`. Watch it fail before setup.
- [x] Add immutable pins and explicit setup targets for BlastEm and both vasm binaries;
  preserve existing `tools` behavior and place products under `build/`. Verify a clean
  rebuild and a second invocation at the same pins. Do not reset a checkout containing
  uncommitted user changes.
- [x] Build the core on macOS arm64 and verify its libretro exports. Record warnings;
  run the existing SNES gates. Commit the toolchain and test changes.

### 2. Headless libretro runner and synthetic cartridge

**Files:** `consoles/md/tests/oracle/`, `consoles/md/tests/rom/`, relevant CMake and
Mise task definitions.

**Interfaces:** Runner accepts explicit core and ROM paths, frame count, output
directory and input script. It emits unfiltered frame images and mixed PCM, with
format and rate recorded in a machine-readable manifest. Failure is nonzero.

- [x] Create a synthetic cartridge with reset vectors, valid header and a deterministic
  loop, plus an input-sensitive variant. Test load failure, frame count, image dimensions,
  nonempty audio and deterministic repeated output. Prove the test catches a changed
  frame or input.
- [x] Implement the runner against the libretro API, handling `need_fullpath`, pixel
  format and frame pitch, and fixing region, model, clock, filter and RAM-init options.
  Record the selected values and reject unsupported required settings rather than
  silently relying on defaults. Compare two clean runs.
- [x] Keep the existing runner and SNES tests intact; run the stage 0 gate and commit.

### 3. Versioned trace and clock epochs

**Files:** `tools/toolchains/patches/blastem/`, `consoles/md/tests/oracle/`,
synthetic fixtures and tests.

**Interfaces:** Define a documented versioned record format with device, operation,
width, address/value where applicable, and 64-bit master clock. The runner rejects
unknown versions and incomplete records. A hook ABI reports capabilities explicitly.

- [x] Test round trips, malformed and truncated records, and a forced counter-adjustment
  boundary. Assert monotonic timestamps and cross-device epoch consistency.
- [x] Patch the locally pinned core to export the hook ABI and accumulate every relevant
  cycle deduction; apply patches reproducibly to a clean pinned checkout. Verify patch
  reapplication and a deliberate trace-version mismatch.
- [x] Run the fixture and stage 0 gates; commit the format, patch and tests.

### 4. CPU and device observations

**Files:** BlastEm patches, synthetic 68000/Z80 cartridges, oracle tests and trace reader.

- [x] Add fixtures with known byte/word reads and writes, instruction fetches, Z80 fast
  RAM and bank-window accesses, VDP port reads/writes and DMA, and timed YM/PSG writes.
  Assert ordered events, widths, values and clocks against independently calculated
  instruction sequences, including a stalled access. Plant an omitted hook in each path
  and show the tests fail.
- [x] Instrument the generated cores and device paths at the pinned revision. Explicitly
  tag prefetches; include fast and mapped Z80 paths, DMA slots, VDP accesses and register
  writes. State whether clocks are access-start or access-completion and capture stalls.
- [x] Capture unfiltered per-frame pixels, mixed sound and each chip's sound; fixtures
  assert nonzero expected signals and deterministic outputs. Do not promote this task
  until each required stream is actually emitted and its clock relationship is tested.
- [x] Run oracle fixtures and stage 0 gates; commit patches and tests.

### 5. Feature extraction

**Files:** `consoles/md/tools/` feature extractor and synthetic trace fixtures.

- [x] Test each required observation: VDP modes/registers, DMA kinds, interlace,
  shadow/highlight, HINT lines, Z80 DAC/direct writes, six-button probing and
  SRAM/mapper use. Include negative and incomplete-coverage cases.
- [x] Implement extraction from the versioned trace. Each observation carries the
  evidence needed to distinguish observed, not observed under the bounded run, and
  unavailable. Verify tests reject unsupported trace capabilities; commit.

### 6. Bounded library survey

**Files:** `consoles/md/tools/survey.py`, `consoles/md/docs/survey.md`, tests.

- [x] Test a synthetic directory with two cartridges, duplicate titles, an invalid file,
  an unsupported mapper and a path-like title. Verify fixed no-input and Start runs,
  deterministic aggregate counts, per-title statuses and no paths or cartridge bytes.
- [x] Implement `survey.py <rom-dir>` using the runner with a documented fixed frame
  count and Start timing. Emit only sanitized titles, features, bounded-run status and
  aggregate counts; preserve errors without embedding paths. Do not infer absence from
  a failed or unsupported run.
- [x] Run it on the supplied local library only after the fixtures pass. Inspect the
  generated document for paths and content, run all stage 0 gates and the stage 1
  fixtures, and commit only the validated report and tooling.

## Stage completion

Review the full branch against the spec and the five review-focus cases. Report the
exact core and assembler revisions, host, commands, failures and limitations. Stage 1
is complete only when every specified stream and survey feature has direct evidence;
a core build or blank-frame smoke test is not sufficient.

**Status:** Complete for the pinned `md1va3` exploratory valid-traffic fixtures on macOS arm64.
The traces do not qualify BlastEm as a hardware-accurate oracle. Hook-omission mutations cover
the exercised M68K, Z80, VDP-port, DMA, HINT, YM2612, PSG and chip-audio paths. Mutations do not
cover the Pico-specific epoch adjustment, the 128-KiB VRAM word-fill arm, or invalid VDP data-read
returns. Exact equivalence between the pinned vasm mirror and the author's 1.9d archive remains
unverified; the available provenance and applicable module terms are recorded separately.
