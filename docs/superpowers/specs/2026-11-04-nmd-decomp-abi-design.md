# D01 design: NMDH v1 reference-execution interface

**Status: approved; D01 implementation is in progress.** This design follows the selected approach: a separate decomp hook ABI plus a narrowly patched save-state implementation in the pinned test core. It is scoped to [D01](../plans/2026-11-04-nativemd-decomp/d01-reference-execution-hooks.md).

## Intent

Give the later code-map, per-routine, and whole-image gates a controlled, inspectable reference for both CPUs. Existing NMDT v2 records access completions and selected device activity; it does not provide instruction-boundary state, controlled routine stops, or complete machine checkpoint evidence. D01 adds those capabilities in candidate tooling while leaving NMDT v2 and the delivered `nmd` contract unchanged.

The interface will be deep at the runner seam: callers select a CPU/stop criterion and consume versioned events and snapshots; BlastEm-specific dispatch, event ordering, clock rebasing, and save-state repair stay inside the test-core adapter.

## Goals

- Expose 68000 and Z80 instruction-boundary events with PC, fetched opcode bytes, architectural state, and a sequence from which dynamic control-flow edges can be reconstructed.
- Expose reset and interrupt entry/return events for both CPUs, plus exception entry/return where the architecture defines an exception path. The Z80 has no architected exception-entry mechanism; its reset and maskable/NMI events are covered instead.
- Expose ordered CPU bus transactions with start and completion clocks, read/write/fetch provenance, width, address, and value. Distinguish covered intervals from any unimplemented idle-cycle detail.
- Provide controlled stop and replacement-decision hooks at an instruction boundary, plus explicit stop criteria for normal return, target PC, interrupt, frame limit, or instruction budget. A replacement decision only synchronizes and hands control back to the runner; D01 does not apply replacement effects or resume from that decision point.
- Capture and restore a complete machine checkpoint at documented safe boundaries, including CPU hidden state and scheduler epochs that affect subsequent execution.
- Fail closed on version, capability, source/core/ROM/scenario identity, truncation, timeout, storage-limit, or state-replay mismatches.
- Measure event rate, bytes per frame, runner overhead, and peak memory on authored synthetic ROMs before any long title capture.

## Non-goals

- Do not change, supersede, or reinterpret NMDT v2. Existing trace, profile, survey, and golden consumers keep their current behavior.
- Do not add a guest interpreter, JIT, instruction helper, or original-code fallback to `nmd` or a delivered game.
- Do not implement A5/A7 certification, a complete title code census, native reset/main execution, or game translation in D01; D03-D06 own those gates.
- Do not treat BlastEm as hardware qualification. Do not store commercial ROMs, recovered game code, or private ROM paths in tracked nativekit files.

## Approaches considered

### A. Separate NMDH ABI with a test-core checkpoint extension — selected

Add a new NMDH v1 interface and capture stream alongside the existing NMDT v2 trace. Reuse Libretro `retro_serialize` / `retro_unserialize` for the machine checkpoint, then extend only the pinned candidate build's serializer for hidden state required by the decomp runner. Code review already identified Z80 `WZ` (MEMPTR) as absent from BlastEm's current save-state serialization; D01 tests will audit this and other relevant fields before relying on it.

This keeps the existing format stable and reuses BlastEm's CPU/device scheduler. It requires changes to the generated-core execution loop, the core save-state implementation, and the runner, so the checkpoint test must prove identical follow-on state and events across restore.

### B. Separate NMDH ABI with a custom checkpoint sidecar

Leave Libretro save states untouched and duplicate the decomp-relevant CPU/scheduler state in a new sidecar. This reduces changes to existing serialization but creates a second state implementation that must stay synchronized with BlastEm's evolving hidden state. It is rejected for D01 because the current test core already serializes most of the machine and the missing-field audit is narrower than a parallel serializer.

### Rejected: extend NMDT v2

Adding instruction/state records to NMDT v2 would change an existing format with active trace, survey, and profile consumers, contrary to the D01 requirement to preserve those consumers. An access-only extension is also insufficient for state and controlled-execution requirements.

## Interface and seam

The proposed ABI name is **NMDH v1** (hook/execution evidence); `NMDX` is already the synthetic ROM step-table magic. The C interface lives in `consoles/md/tests/oracle/decomp_abi.h` / `.c`, and its patch is applied only to the pinned BlastEm test build. The runner lives under the planned `consoles/md/tests/decomp/` and `consoles/md/tools/decomp/` directories.

The public C surface stays small:

- Query `nmd_decomp_abi_version()` and `nmd_decomp_capabilities()` before enabling any required gate.
- Register one synchronous callback with an ABI version and caller-owned context; unregister/reset it explicitly between runs.
- Receive a versioned event record containing a monotonic sequence, CPU, event kind, global clock/epoch, PC transition, and event-specific payload. Event payloads are copied during the callback; no pointer to mutable CPU or machine state escapes it.
- Return one of `CONTINUE`, `STOP`, or `REPLACE` from an instruction-boundary callback. `STOP` and `REPLACE` stop before the next guest instruction, then return control only after the CPU/scheduler reaches a documented synchronized boundary. `REPLACE` is a decision-point handoff: the runner receives the stop reason and immutable state snapshot, while CPU, memory, and clock remain unchanged. D01 does not provide a replacement mutation protocol or resume from the handoff. Later D04/D05 work owns applying C-side effects and resuming reference execution.

NMDH v1 events are:

| Event | Required payload |
|---|---|
| `INSTRUCTION` | CPU, current PC, opcode/prefetch word, fetched-byte provenance, global clock, and a copied CPU-state snapshot |
| `FLOW_EDGE` | Previous and current instruction PCs, with the observed transition type when known; indirect targets remain explicit when unresolved |
| `BUS_ACCESS` | CPU, read/write, fetch/operand/data provenance, width, address, value, access start clock, completion clock, and epoch |
| `RESET` / `EXCEPTION` / `INTERRUPT` | CPU, vector/reason, entry or return phase, PC/stack transition, and clock; exceptions apply where the architecture defines them, while the Z80 reports reset and IRQ/NMI |
| `STOP` / `REPLACE` | CPU, criterion/value, triggering instruction count and PC, both CPU snapshots, synchronized master clock, and VDP/YM/PSG/IO scheduler clocks |

An `INSTRUCTION` event describes the just-completed instruction: `pc` and fetched bytes identify its pre-instruction address and encoding; `next_pc` and the copied CPU snapshot show the post-instruction state at the boundary before another guest instruction. The copied 68000 snapshot includes D0-D7, A0-A7, USP/SSP selection, PC, SR/CCR flags, prefetch, stop state, pending interrupt fields, and relevant local/global cycles. The Z80 snapshot includes main and alternate registers, AF/BC/DE/HL, IX/IY, SP/PC/WZ, I/R, IM/IFF state, flags, reset/BUSREQ/BUSACK, pending interrupt state, and relevant cycles. The final field list must be checked against the pinned context structures and round-trip test before implementation is accepted. The Z80 computed-goto hook publishes the preceding instruction when the next opcode is fetched, but before that opcode body executes; its snapshot includes the fetch-side PC/R changes.

The runner streams NMDH v1 to a versioned file with a header, capability and identity record, bounded records, and a footer containing record count and completion status. Incomplete or truncated streams are invalid. The existing NMDT v2 file remains independently available for its device events and current consumers; both streams carry the shared master-clock epoch identity needed for later merge/replay.

### Checkpoints and safe boundaries

The runner uses the pinned core's Libretro checkpoint API only at a post-frame boundary or after an explicit decomp stop has synchronized the 68000, Z80, VDP, sound, IO, and scheduler to one global clock. It does not serialize inside a callback or between the read and completion halves of a bus transaction. Restoring a checkpoint for a deterministic replay test uses the core's ordinary execution path; this does not resume a `REPLACE` handoff or apply replacement effects.

The test-core state extension is versioned by the core/patch identity. It adds only fields proven necessary by the restore test, beginning with Z80 WZ and decomp instrumentation state not covered by Libretro serialization. Rebase counters, prefetch, bus arbitration, and device state are audited as a whole. A checkpoint passes only if restoring and replaying the same synthetic input yields the same subsequent state and NMDH/NMDT event streams.

### Timing coverage

NMDH v1 reports CPU bus start and completion clocks and instruction-boundary clocks. It must publish capability bits separately for bus intervals, code-fetch provenance, CPU-state capture, interrupt events, synchronized checkpoints, and controlled stop/replacement. The ABI must not claim exact idle-cycle or per-microcycle coverage unless a synthetic negative-control proves it. Unsupported timing detail remains a named capability gap for D05, not an implicit estimate.

## Synthetic verification

Authored fixtures use synthetic cartridges only and exercise both CPUs: normal calls, computed/tail transfers, reset, IRQ/NMI, code in ROM and RAM, nested calls, state load/restore, counter rebasing, BUSREQ/BUSACK, and both CPUs active in one run. The runner checks event order, PC/state snapshots, fetch/data provenance, bus intervals, epoch monotonicity, and capability manifests. STOP/REPLACE controls must return only at the synchronized boundary, expose an immutable snapshot and reason to the runner, and execute no later guest instruction before control returns. D01 verifies handoff only; tests for changed C effects and resumed reference execution belong to D04/D05.

Negative controls must make the associated gate fail: wrong PC, wrong register/flag state, incorrect epoch, missing capability, altered event ordering, and a corrupted/truncated checkpoint or event stream. Tests run the actual patched BlastEm core against the authored fixtures; a mock-only pass does not count.

Resource controls use streaming writes, explicit event/file limits, a child-process timeout, and a failed/incomplete status on overflow. The initial runner limits are 420 seconds and 256 MiB per capture; the D01 benchmark must report observed events/second, bytes/frame, peak process memory, and runner overhead and may tighten these limits before any title-scale run.

## Files and ownership

- ABI contract/header/implementation: `consoles/md/docs/decomp/abi-v1.md` and the planned oracle header/source under `consoles/md/tests/oracle/`.
- Candidate patch and ABI stamp inputs: `tools/toolchains/patches/blastem/` and `tools/toolchains/setup.sh`.
- Runner, authored controls, and `mise run test:md-decomp`: `consoles/md/tools/decomp/` and `consoles/md/tests/decomp/`.
- Private per-title traces/checkpoints remain under ignored `build/decomp/<game-id>/`; no commercial image, capture, or path is committed.
- Existing `NMDT v2` trace/profile/survey formats remain unchanged. Any hook/core rebuild invalidates dependent NMDH captures by source/core/ABI/runner/scenario identity.

## Accepted decisions and remaining design calls

- Selected by the user: separate NMDH v1 ABI plus a narrowly patched test-core checkpoint extension.
- Selected by the user: `REPLACE` stops at a synchronized CPU/machine boundary and hands an immutable decision snapshot to the runner; C effects and resume are deferred to D04/D05.
- The builder will decide the smallest safe instruction-boundary hook placement in the generated 68000/Z80 loops, document any timing/provenance gap, and stop with a measured blocker if the core cannot return at a synchronized stop point.
- The builder will verify every serialized CPU/scheduler field against real execution; the listed state fields are the audit checklist, not an assumption that Libretro already captures them.
