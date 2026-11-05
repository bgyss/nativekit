# NMDH v1 D01 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a versioned, test-only reference execution and checkpoint interface for the pinned BlastEm 68000 and Z80 cores, with evidence that the runner can trust.

**Architecture:** Keep NMDH v1 separate from NMDT v2. A patched candidate core emits copied CPU, flow, and bus events through a small C ABI; a Python runner captures bounded NMDH and existing NMDT streams, fingerprints their inputs, and uses Libretro checkpoints at synchronized boundaries. Synthetic cartridges exercise both real CPU paths; replacement decisions stop and hand off without changing state or resuming.

**Tech Stack:** C11; pinned BlastEm `2230addc4dd74dd094fd264987fd808abba32d0e`; Python 3 and `ctypes`; pinned vasm `685a87e5ed14285350ccdb6581c9771bc8df6c7d`; `mise` tasks; synthetic Mega Drive ROM fixtures.

**Spec:** [NMDH v1 design](../../specs/2026-11-04-nmd-decomp-abi-design.md); [D01 milestone contract](../2026-11-04-nativemd-decomp/d01-reference-execution-hooks.md).

**Execution method:** Native single-session execution, matching the shared execution contract's default. Keep the C ABI, generated-core patch and its integration tests in one working session; do not delegate tasks.

## Refreshed Baseline

- Planning baseline: `1777411c701c04ab91362d938b4301fb94990679`; D00 refreshed from that baseline and is complete for its selection/preparation scope.
- At plan creation, current checkout was branch `codex/nativemd-d00`, HEAD `157ad08648deae5fd1d6e2379457fdc9c65ada20`. The design was approved and D01 implementation had not yet started.
- The D00 baseline records that this worktree had no built toolchain. Build from the pinned sources before core tests: BlastEm `2230addc4dd74dd094fd264987fd808abba32d0e`, vasm `685a87e5ed14285350ccdb6581c9771bc8df6c7d`, as listed in `tools/toolchains/pins.env`.
- D01 synthetic controls do not need a retail ROM. Keep any later title inputs external and path-free in tracked evidence.

## Global Constraints

- Preserve NMDT v2 event semantics and all existing trace, profile and survey consumers.
- Instrument both the 68000 and Z80 in the pinned candidate test core; do not add hooks or guest CPU execution to delivered `nmd` or game code.
- `REPLACE` stops at a synchronized CPU/machine boundary and hands the runner an immutable snapshot; D01 does not mutate C-side state or resume from that handoff.
- Capture/restore only at post-frame or explicitly synchronized boundaries; prove replayed state and NMDH/NMDT events match.
- Store no commercial ROM, recovered game code, private ROM path or private capture in tracked files; use authored synthetic cartridges and ignored `build/decomp/` output.
- Fingerprint patch, core, ABI, runner, scenario and ROM; reject missing capabilities, mismatched identities, truncated records, timeouts and storage-limit failures.
- Estimate requested duration and storage from a bounded, same-ROM/same-scenario preflight before capture; do not chunk or discard evidence in D01. Over-limit runs remain incomplete.
- Publish capabilities separately for CPU state, flow, fetch provenance, bus intervals, interrupts, synchronized checkpoints and controlled handoff; do not claim idle-cycle or per-microcycle coverage without a proving control.
- Stream captures under the initial limits of 420 seconds and 256 MiB per capture; keep overflow and timeout results explicitly incomplete.
- Benchmark both CPUs for events per second, bytes per frame, runner overhead and peak process memory before any title-scale capture.
- Preserve SNES behavior, the MD 1.0 contract, existing golden baselines and the six individually reasoned `KNOWN` entries.

## Review Focus

- A wrong ABI version, missing capability, malformed record, truncated footer or identity mismatch must be rejected before a trace becomes evidence; cover in Task 2's format and runner checks.
- Wrong 68000 prefetched opcode word, observed PC transition, register or flag state must fail the candidate check, including ROM and work-RAM execution; cover in Task 3's core fixture.
- Wrong Z80 opcode extent, alternate/hidden register, WZ or PC transition must fail the candidate check, including RAM execution; cover in Task 4's core fixture.
- A STOP/REPLACE request while the other CPU and devices are active must return only after scheduler synchronization and before another guest instruction; cover in Task 5's combined-CPU fixture.
- Incorrect bus start/end clocks, clock rebasing or checkpoint state must invalidate replay evidence rather than becoming an inferred timing/state pass; cover in Tasks 6 and 7.

---

## File Map

- `consoles/md/tests/oracle/decomp_abi.h` / `.c`: versioned event structs, capability bits, registration, callback dispatch, sequence and internal error state. These are copied into the pinned test-core build only.
- `tools/toolchains/patches/blastem/0002-nmd-decomp-hooks.patch`: add the ABI object, export `nmd_decomp_*`, instrument the generated CPU loops and scheduler, and repair only state fields demonstrated missing by restore tests.
- `tools/toolchains/setup.sh`: include the decomp ABI and patch in the BlastEm patch/source stamps and copy the ABI source/header into the fresh pinned build tree.
- `consoles/md/tools/decomp/trace.py` / `run.py`: NMDH v1 stream reader/writer and bounded Libretro capture command; keep the existing NMDT v2 writer available as its own stream.
- `consoles/md/tests/decomp/fixtures.py` / `core_check.py`: authored ROM and scenario fixtures and actual-core assertions for both CPUs, stop boundaries, timing and state restore.
- `consoles/md/tests/decomp/abi_check.py`, `format_check.py`, and `runner_check.py`: host-side ABI, stream-validation and runner fail-closed checks.
- `consoles/md/tests/decomp_check.py`: focused case selection for the integration suite and the final all-cases run.
- `consoles/md/docs/decomp/abi-v1.md` / `resource-budget.md`: wire format, event/capability semantics, timing gaps, and measured synthetic capture limits.
- `mise.toml`, `consoles/md/tests/README.md`, and `tools/toolchains/README.md`: add the non-empty D01 task and document its scope, build dependency and evidence limits.

## Tasks

### Task 1: Define the versioned NMDH C ABI

**Files:**
- Create: `consoles/md/tests/decomp/abi_check.py`
- Create: `consoles/md/tests/oracle/decomp_abi.h`
- Create: `consoles/md/tests/oracle/decomp_abi.c`
- Create: `consoles/md/docs/decomp/abi-v1.md`
- Modify: `docs/superpowers/specs/2026-11-04-nmd-decomp-abi-design.md` (record approved/in-progress status)
- Modify: `docs/superpowers/plans/2026-11-04-nativemd-decomp/README.md` (mark D01 partial as implementation starts)
- Modify: `docs/superpowers/plans/2026-11-04-nativemd-decomp/d01-reference-execution-hooks.md` (replace proposed status with partial)

**Interfaces:**
- Permit test-only continuation from an ordinary STOP checkpoint for deterministic replay; REPLACE remains latched with no resume operation.
- Produces `uint32_t nmd_decomp_abi_version(void)` and `uint64_t nmd_decomp_capabilities(void)`.
- Defines `enum nmd_decomp_action { NMD_DECOMP_CONTINUE = 0, NMD_DECOMP_STOP = 1, NMD_DECOMP_REPLACE = 2 }`, CPU IDs `NMD_DECOMP_M68K = 1` and `NMD_DECOMP_Z80 = 2`, event IDs `INSTRUCTION=1`, `FLOW_EDGE=2`, `BUS_ACCESS=3`, `RESET=4`, `EXCEPTION=5`, `INTERRUPT=6`, `STOP=7`, `REPLACE=8`, and `typedef enum nmd_decomp_action (*nmd_decomp_callback)(void *user, const struct nmd_decomp_event *event)`.
- Produces `int nmd_decomp_register(uint32_t version, nmd_decomp_callback callback, void *user)`, `void nmd_decomp_reset(void)` and `int nmd_decomp_error(void)`; reject unsupported versions and invalid callback registration.
- Produces `enum nmd_decomp_action nmd_decomp_publish(struct nmd_decomp_event *event)` for patched CPU/scheduler hooks. It assigns a monotonic sequence, invokes the registered callback synchronously, and honors `CONTINUE`, `STOP` or `REPLACE` only on an instruction-boundary event.
- Event records carry `struct_size`, event kind, CPU, sequence, common master-clock epoch, PC transition and inline copied payload; snapshots contain no pointers into mutable core state.
- Capability bits are `M68K_INSTRUCTION=1<<0`, `M68K_STATE=1<<1`, `Z80_INSTRUCTION=1<<2`, `Z80_STATE=1<<3`, `FLOW_EDGE=1<<4`, `RESET_EXCEPTION_INTERRUPT=1<<5`, `FETCH_PROVENANCE=1<<6`, `BUS_INTERVALS=1<<7`, `MASTER_CLOCK=1<<8`, `SYNC_CHECKPOINT=1<<9`, `CONTROLLED_STOP=1<<10` and `REPLACE_HANDOFF=1<<11`; do not set a bit for an uninstrumented path.

- [x] **Step 1: Write the failing ABI check.** Compile the ABI source as a shared library and check version 1, supported capability mask, registration/reset, callback sequence, copied event values, rejection of a wrong version and invalid action handling.
- [x] **Step 2: Run `python3 consoles/md/tests/decomp/abi_check.py`.** Expected: fail because the NMDH symbols and event structs do not exist.
- [x] **Step 3: Implement the ABI contract.** Define the C11 structs, enums and capability constants in `decomp_abi.h`; implement synchronous callback dispatch, sequence/error handling and capability reporting in `decomp_abi.c`; document each field, event and unsupported coverage in `abi-v1.md`.
- [x] **Step 4: Run `python3 consoles/md/tests/decomp/abi_check.py`.** Expected: `decomp ABI checks: ok`; wrong version and invalid action controls are rejected.
- [x] **Step 5: Commit** `nativemd: define NMDH v1 ABI`.

### Task 2: Add the NMDH stream and bounded runner shell

**Files:**
- Create: `consoles/md/tests/decomp/format_check.py`
- Create: `consoles/md/tests/decomp/runner_check.py`
- Create: `consoles/md/tools/decomp/trace.py`
- Create: `consoles/md/tools/decomp/run.py`
- Modify: `consoles/md/docs/decomp/abi-v1.md`

**Interfaces:**
- Produces `NMDHWriter(path, metadata, max_bytes)` with `write_event(event)`, `finish(status)` and `abort(reason)`; it streams records and never retains the full capture in memory.
- Produces `read_nmdh(path)` which validates the whole stream before yielding records, and `validate_identity(metadata, expected)` which rejects any mismatched identity.
- Produces runner CLI arguments `--mode estimate|capture`, `--core`, `--rom`, `--scenario`, `--frames`, `--estimate-file`, `--out`, `--max-seconds` (default `420`) and `--max-bytes` (default `268435456`). A version-1 scenario JSON has `version`, `region` (`J|U|E`), `model` (`md1va3|md1va6`), `pad1`/`pad2` (`3|6`), `input` (frame to port/button-list map), `required_capabilities` (nonempty ABI capability-name list) and `stop` fields; `stop` has CPU (`m68k|z80`), criterion (`pc`, `return_sentinel`, `interrupt`, `instruction_budget` or `frame_limit`), action (`stop|replace`) and an integer `value` required for PC, return-sentinel and instruction-budget criteria. The required list must include both instruction/state bits for the stop CPU; non-frame stops also require controlled-stop, interrupt stops require interrupt events, and REPLACE requires the handoff bit. Estimate mode captures a bounded three-frame sample and projects time/storage for the requested frame count. Capture mode requires an estimate file whose ROM/scenario/core/patch/ABI/runner identities and requested frame count match exactly, then enforces the same caps. The runner writes a private NMDH stream, a separate NMDT v2 stream, rolling video/audio callback hashes, and a path-free manifest under the requested output directory.
- NMDH v1 uses little-endian integers; a 12-byte header of `NMDH`, u16 version 1, zeroed u16 flags and u32 metadata length followed by canonical UTF-8 JSON identity/capability metadata capped at 16,384 bytes; event records capped at 4,096 bytes and prefixed by u32 payload length. Each record payload is canonical UTF-8 JSON with `struct_size`, `kind`, `cpu`, `flags`, `sequence`, `master_clock`, `epoch`, `pc`, `next_pc` and `payload` fields. A 48-byte `NHEN` footer has u64 record count, u8 completion status (`0=complete`, `1=timeout`, `2=storage_limit`, `3=core_error`, `4=cancelled`), three zeroed reserved bytes and SHA-256 digest of the header and all records. The estimate file records the requested frame count along with observed rate/size and identities. The parser rejects unsupported versions, oversized records, invalid event/capability values, digest/count mismatch, incomplete footer and trailing bytes.
- The identity header records SHA-256 values for ROM bytes, canonical scenario data, core binary, patch set, ABI source/header and runner; it records the pinned core revision and machine options without local paths.
- The runner supervisor executes Libretro in a child process, enforces the timeout and storage cap, and leaves interrupted/limited captures incomplete and unaccepted. D01 does not chunk traces; a run that exceeds a cap is incomplete and must be narrowed and measured again.
- The runner splits `--max-bytes` evenly between NMDH and NMDT v2 files; their combined trace bytes therefore remain below the per-capture cap.

- [x] **Step 1: Write failing stream and runner checks.** Pin round-trip records and identity fields; plant bad magic, version, record size, capability, digest, count, truncation, trailing data, stale identity, missing ABI symbols and pre-existing output directory cases. Verify child timeout and storage-limit failures remain incomplete.
- [x] **Step 2: Run `python3 consoles/md/tests/decomp/format_check.py` and `python3 consoles/md/tests/decomp/runner_check.py`.** Expected: fail because the NMDH reader/writer and CLI are absent.
- [x] **Step 3: Implement the writer, reader and supervisor.** Write binary records directly to disk; keep a `.partial` file on timeout or limit failure; only report a complete trace after a valid footer and digest. Refuse a core that lacks the required NMDH version/capabilities before accepting evidence.
- [x] **Step 4: Run both checks again.** Expected: round-trip succeeds and every planted malformed, stale, missing-symbol and resource-control case is rejected with an explicit incomplete/failure status.
- [x] **Step 5: Commit** `nativemd: add bounded NMDH capture format`.

### Task 3: Instrument 68000 instruction and state events

**Files:**
- Modify: `tools/toolchains/patches/blastem/0002-nmd-decomp-hooks.patch`
- Modify: `tools/toolchains/setup.sh`
- Create: `consoles/md/tests/decomp/fixtures.py`
- Create: `consoles/md/tests/decomp/core_check.py`
- Modify: `consoles/md/tests/decomp/abi_check.py`
- Modify: `consoles/md/tests/decomp/runner_check.py` (pin matching estimate identity through capture)
- Modify: `consoles/md/tools/decomp/run.py` (fix nested estimate metadata validation required by the real-core case)
- Modify: `consoles/md/docs/decomp/abi-v1.md` (pin instruction PC/state timing semantics)
- Modify: `docs/superpowers/specs/2026-11-04-nmd-decomp-abi-design.md` (clarify instruction event pre/post-state meaning)

**Interfaces:**
- Adds the 68000 instruction, flow and state capability bits only when the patched 68000 path is compiled.
- Instruction events contain the pre-instruction PC, actual prefetched opcode word (two bytes) with fetch provenance, observed post-instruction PC transition, master clock and copied 68000 snapshot: D0-D7, A0-A7, USP/SSP selection, PC, SR/CCR flags, prefetch, stop state, pending interrupt fields and relevant cycles. Do not claim extension-word fetch provenance from access-completion records.
- An instruction event describes the just-completed instruction: `pc` and fetched bytes identify its pre-instruction address/encoding; `next_pc` and the copied CPU state reflect the post-instruction boundary before the next guest instruction.
- `fixtures.py` emits an authored cartridge with reset code, direct and computed transfers, a nested call/return and a work-RAM copy/execute sequence. It records expected PCs, bytes and register/flag values in scenario metadata.

- [x] **Step 1: Add failing actual-core assertions.** In `core_check.py --case m68k`, require instruction/event order, the prefetched opcode word and observed next-PC transition, direct/computed flow edges, call/return state, and the same assertions for copied code executing in work RAM.
- [x] **Step 2: Run `python3 consoles/md/tests/decomp/core_check.py --case m68k`.** Expected: fail because the baseline core exports no NMDH ABI or 68000 instruction/state capability.
- [x] **Step 3: Extend the BlastEm patch and build stamps.** Patch `cpu_dsl.py` to wrap the complete opcode body in both generated 68000 execution-loop variants; add the minimal context fields and prototypes in `m68k.cpu`, and snapshot/fetch helpers in `m68k_util.c`. Add the NMDH object and exports; include `cpu_dsl.py`, `decomp_abi.c`, `.h` and all BlastEm patches in `setup.sh` patch/source stamps and copied sources. Fix estimate identity validation in `run.py` as exposed by this actual-core run, and pin it in `runner_check.py`. Preserve all NMDT v2 calls and fields.
- [x] **Step 4: Run `mise run tools:md` and `python3 consoles/md/tests/decomp/core_check.py --case m68k`.** Expected: the real pinned core reports the required 68000 capability and the synthetic ROM's event/state assertions pass; planted PC, opcode-word, register and condition-code mismatches fail.
- [x] **Step 5: Commit** `nativemd: trace 68000 instruction boundaries`.

### Task 4: Instrument Z80 instruction and state events

**Files:**
- Modify: `tools/toolchains/patches/blastem/0002-nmd-decomp-hooks.patch`
- Modify: `consoles/md/tools/decomp/run.py` (match target/return stops against post-instruction `next_pc`)
- Modify: `consoles/md/tests/decomp/runner_check.py` (pin next-PC stop matching)
- Modify: `consoles/md/tests/decomp/fixtures.py`
- Modify: `consoles/md/tests/decomp/core_check.py`

**Interfaces:**
- Adds the Z80 instruction, flow and state capability bits only when its patched path is compiled.
- Z80 snapshots contain main and alternate registers, AF/BC/DE/HL, IX/IY, SP/PC/WZ, I/R, IM/IFF state, flags, reset/BUSREQ/BUSACK, pending interrupt state and relevant cycles.
- Z80 instruction events use the fetched-byte sequence from actual core reads, not a static decoder's guessed extent.

- [x] **Step 1: Add failing Z80 assertions.** The fixture starts the Z80 through the real Genesis bus path, copies code into Z80 RAM and checks immediate, branch, CALL/RET, alternate-register and flag state, WZ visibility, and concurrent 68000 activity.
- [x] **Step 2: Run `python3 consoles/md/tests/decomp/core_check.py --case z80`.** Expected: fail because Z80 instruction/state capabilities are absent.
- [x] **Step 3: Instrument `z80.cpu`, `z80_util.c` and the generated dispatch path through the candidate patch.** Add state fields to `z80.cpu` so the generated `z80.h` receives them; hook each main-table dispatch after a completed instruction, append actual prefix fetches at non-main tables, and copy the full state into NMDH payloads without exposing internal pointers.
- [x] **Step 4: Rebuild with `mise run tools:md` and rerun the Z80 case.** Expected: both CPU traces pass on real synthetic instructions; a planted wrong WZ/register/PC observation is rejected by the fixture checker.
- [x] **Step 5: Commit** `nativemd: trace Z80 instruction boundaries`.

### Task 5: Add flow, interrupt and synchronized stop handoff

**Files:**
- Modify: `tools/toolchains/patches/blastem/0002-nmd-decomp-hooks.patch`
- Modify: `consoles/md/tests/oracle/decomp_abi.h` / `.c` (pending action and immutable handoff record)
- Modify: `consoles/md/tools/decomp/run.py` (require and record the synchronized STOP/REPLACE event)
- Modify: `consoles/md/tests/decomp/runner_check.py` (cover stop criteria and handoff response)
- Modify: `consoles/md/tests/decomp/fixtures.py`
- Modify: `consoles/md/tests/decomp/core_check.py`
- Modify: `consoles/md/docs/decomp/abi-v1.md` (stop, interrupt and handoff payload semantics)
- Modify: `docs/superpowers/specs/2026-11-04-nmd-decomp-abi-design.md` and `docs/superpowers/plans/2026-11-04-nativemd-decomp/d01-reference-execution-hooks.md` (architecture-specific exception support and the full handoff record)

**Interfaces:**
- Emits `FLOW_EDGE` and `RESET` for both CPUs, M68K `EXCEPTION` entry/return, and M68K/Z80 `INTERRUPT` entry/return; the Z80 has no architected exception-entry path. Unresolved indirect targets stay explicit.
- A callback action on an instruction event stops before the next guest instruction. The core then synchronizes the 68000, Z80, VDP, sound, IO and scheduler to one documented master-clock boundary before `retro_run` returns to the runner.
- Runner stop criteria are target PC, caller-supplied normal-return sentinel, interrupt, frame limit or instruction budget. `REPLACE` is the same stop-and-handoff boundary with the immutable triggering snapshot; it performs no memory/register/clock mutation and has no resume operation in D01.
- Target-PC and return-sentinel criteria match the instruction event's `next_pc`, so handoff occurs after the transfer reaches the requested boundary and before the destination instruction executes.

- [x] **Step 1: Add failing controls for both CPU resets, M68K exception and both CPUs' IRQ entry plus return, nested/tail/computed flow and all stop criteria.** Include a combined-CPU case that snapshots both CPU states and device/scheduler clock at the handoff; pin identical STOP and REPLACE snapshots.
- [x] **Step 2: Run `python3 consoles/md/tests/decomp/core_check.py --case handoff`.** Expected: fail in preflight because the baseline core lacks reset/clock/controlled-stop/handoff capabilities; the actual handoff behavior is exercised in Step 4.
- [x] **Step 3: Add the smallest safe stop and return path in the generated CPU loops and `genesis.c` scheduler.** Preserve the callback's copied event; iterate device synchronization to one common clock and fail closed if it does not stabilize; return the stop reason and boundary clock without changing guest state. If safe synchronization cannot be demonstrated, stop with a measured blocker and concrete core/ABI adaptation proposal; do not weaken the gate.
- [x] **Step 4: Rebuild and run the handoff case.** Expected: target/return/interrupt/budget/frame stops pass; `STOP` and `REPLACE` emit no later guest instruction, return identical snapshots, and show synchronized master clocks. The other CPU must make its expected pre-boundary progress.
- [x] **Step 5: Commit** `nativemd: synchronize NMDH stop handoffs`.

### Task 6: Add ordered bus intervals and coverage declarations

**Files:**
- Modify: `tools/toolchains/patches/blastem/0002-nmd-decomp-hooks.patch`
- Modify: ABI header/source for shared clock conversion, epochs and a synthetic rebase control
- Modify: decomp runner for the fixture-only rebase request
- Modify: ABI/runner checks for clock and rebase APIs
- Modify: `consoles/md/tests/decomp/fixtures.py`
- Modify: `consoles/md/tests/decomp/core_check.py`
- Modify: `consoles/md/docs/decomp/abi-v1.md`
- Modify: `docs/superpowers/specs/2026-11-04-nmd-decomp-abi-design.md` (publication order versus shared-clock ordering)

**Interfaces:**
- `BUS_ACCESS` records CPU, read/write/fetch/operand provenance, width, address, value, start clock, completion clock and epoch.
- NMDH uses a shared master-clock epoch for interleaving both CPUs; NMDT v2 keeps its present per-device completion-event semantics.
- Event sequence is callback publication order. Nested CPU synchronization may publish a later sequence with an earlier cross-CPU timestamp; merge by shared master clock and use sequence as the tie-breaker, while requiring per-CPU clocks to stay monotonic.
- Longword 68000 accesses are emitted as two physical 16-bit bus-cycle records.
- Synthetic bus scenarios may set `test_rebase_frame` to exercise the real counter-rebase path; this fixture-only control is part of the scenario identity.
- Advertise start/end coverage only for instrumented CPU bus intervals. Do not advertise idle-cycle or per-microcycle coverage.

- [x] **Step 1: Add failing interval/provenance assertions.** Test byte/word/long 68000 accesses, Z80 memory/I/O, an observed wait-state access, and fetch versus operand/data. Assert start is not after completion and event ordering is stable across a counter-rebase boundary.
- [x] **Step 2: Run `python3 consoles/md/tests/decomp/core_check.py --case bus`.** Expected: fail in preflight because the baseline core lacks `bus_intervals`.
- [x] **Step 3: Instrument the access helpers before and after their mapped handler/stall paths.** Convert local clocks through one versioned offset/epoch, derive interval endpoints around the actual mapped handlers, preserve NMDT hooks unchanged, set only verified capability bits, and document the explicit timing gaps.
- [x] **Step 4: Rebuild and rerun the bus case.** Expected: synthetic expected intervals and fetch/data provenance pass; planted bad start/end, epoch and ordering values fail validation; NMDT v2 comparison remains byte-for-byte at the format/reader level.
- [x] **Step 5: Commit** `nativemd: record NMDH bus intervals`.

### Task 7: Prove complete synchronized checkpoint replay

**Files:**
- Modify: serialize.h and genesis.c for the versioned NMDH checkpoint section
- Modify: libblastem.c for the candidate-only Libretro audio filter checkpoint state
- Modify: ABI header/source for serialized trace state and STOP-only continuation
- Modify: decomp runner for paired checkpoint replay and identity validation
- Modify: ABI/runner checks for the state extension and checkpoint envelope
- Modify: `tools/toolchains/patches/blastem/0002-nmd-decomp-hooks.patch`
- Modify: `tools/toolchains/setup.sh` (include every changed serializer source in the source stamp)
- Modify: `consoles/md/tests/decomp/core_check.py`
- Modify: `consoles/md/docs/decomp/abi-v1.md`

**Interfaces:**
- Use Libretro `retro_serialize_size`, `retro_serialize`, and `retro_unserialize` only post-frame or after Task 5's synchronized handoff.
- Extend the pinned test-core state only for fields proven absent by the restore audit; include Z80 WZ/MEMPTR if the test confirms it is omitted, plus any NMDH sequence/epoch/pending-stop state that changes replay output.
- Version the extension by the build's patch/core identity; do not change delivered `nmd` or NMDT v2 save/trace contracts.
- Continue deterministically from STOP only for the checkpoint replay control; REPLACE remains latched and has no resume operation.

- [x] **Step 1: Add failing round-trip and state-load controls.** Capture at a post-frame and at a synchronized stop; save, continue for a fixed synthetic input, restore, and rerun. Compare full CPU snapshots, NMDH events, NMDT v2 events, and video/audio callback hashes. Include a Z80 instruction that changes WZ before the checkpoint.
- [x] **Step 2: Run `python3 consoles/md/tests/decomp/core_check.py --case checkpoint`.** Expected: fail in preflight because the Task 6 core lacks `sync_checkpoint`; the baseline has no checkpoint replay path.
- [x] **Step 3: Audit the pinned `m68k_util.c`, `z80_util.c`, `genesis.c` and Libretro audio-output state against their runtime structures.** Add only the missing test-core fields demonstrated by the failing replay, and reject incompatible extension versions.
- [x] **Step 4: Rebuild and rerun checkpoint controls.** Expected: restored and uninterrupted paths produce identical subsequent CPU/device-visible state, NMDH/NMDT events, and video/audio hashes at both safe boundary types; corrupted state and patch/core identity mismatch are rejected.
- [x] **Step 5: Commit** `nativemd: make NMDH checkpoints replayable`.

### Task 8: Close the D01 evidence gates and record resource budget

**Files:**
- Create: `consoles/md/tests/decomp_check.py`
- Create: `consoles/md/tools/decomp/benchmark.py`
- Create: `consoles/md/docs/decomp/resource-budget.md`
- Modify: `mise.toml`
- Modify: `consoles/md/tests/README.md`
- Modify: `tools/toolchains/README.md`
- Modify: `docs/superpowers/plans/2026-11-04-nativemd-decomp/README.md`
- Modify: `docs/superpowers/plans/2026-11-04-nativemd-decomp/d01-reference-execution-hooks.md`

**Interfaces:**
- Produces non-empty `mise run test:md-decomp`, depending on `tools:md`, and runs every ABI, format, actual-core and negative-control case.
- Produces `python3 consoles/md/tools/decomp/benchmark.py --frames 30` for the authored combined-CPU looping fixture. It records actual event count/rate, bytes/frame, runner overhead, peak process memory and all input identities; output artifacts remain in ignored `build/decomp/synthetic-nmdh/`.
- Tracked `resource-budget.md` records the measured results and confirms whether each run completed within 420 seconds and 256 MiB; adjust limits only from measured evidence before title captures.
- D01 moves to complete only after each contract acceptance criterion passes; otherwise record partial/blocked evidence and the first unmet gate without claiming full completion.

- [ ] **Step 1: Add final integration tests and planted failures.** Mutate a valid event stream to contain wrong PC, register/flag, epoch, capability, identity, event order, count, digest and stop boundary; require the corresponding validator to reject each mutation. Add explicit timeout and byte-limit runs that leave incomplete results.
- [ ] **Step 2: Run `python3 consoles/md/tests/decomp_check.py`.** Expected: fail on missing final task wiring or any missing negative control; actual CPU cases must use the patched BlastEm core.
- [ ] **Step 3: Add the shared mise entrypoint and document exact commands and evidence limits.** Wire `test:md-decomp` to `tools:md` and the non-empty integration runner; add the tests/toolchain docs and benchmark command.
- [ ] **Step 4: Run affected checks.** Run `mise run test:md-toolchain`, `mise run test:md-oracle-smoke`, `mise run test:md-trace`, `mise run test:md-profile`, and `mise run test:md-decomp`; expected: all pass and existing NMDT/profile behavior is preserved.
- [ ] **Step 5: Measure the bounded combined-CPU fixture.** Run `python3 consoles/md/tools/decomp/benchmark.py --frames 30`; expected: complete streaming output and measured events/second, bytes/frame, runner overhead and peak memory under the declared limits. If it exceeds a limit, reduce only the synthetic benchmark workload, rerun, and report the measured scope.
- [ ] **Step 6: Review `git diff --check`, update the D01 status from partial to complete only if all gates pass, and record source/tool/core/patch/ABI/runner/scenario identities and the resource report.**
- [ ] **Step 7: Commit** `nativemd: complete D01 reference execution hooks`.

## D01 Acceptance Review

- [ ] Both real CPU hook paths execute authored synthetic instructions and expose independently checked state and flow.
- [ ] Capture/restore reproduces subsequent state and NMDH/NMDT event streams across repeated runs and a state-load boundary.
- [ ] A planted wrong PC, register/flag, epoch or capability is rejected by its corresponding gate.
- [ ] NMDT v2 format and existing trace/profile/survey checks retain documented behavior.
- [ ] Bounded capture stays within declared memory/storage limits; limits and child timeouts yield explicit incomplete results, never accepted truncated evidence.
- [ ] All inputs are fingerprinted; private ROMs, captures and paths remain outside tracked files.
- [ ] `README.md` and the D01 contract accurately report D01 complete, partial or blocked; no D02+ status is advanced.
