# D05: Verify combined C substitution under concurrent hardware execution

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Build the MD equivalent of A7 so routines that pass alone must also behave correctly together.

## Prerequisites

D01, D04; D02 for later standalone comparisons. Synthetic controls do not require the first retail ROM. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Run original and hybrid from equivalent cold or checkpoint states and identical input. Replace all selected C routines together and interleave their real device accesses with recorded reference scheduling.

2. Keep exact bus/event replay inside the verification harness. Do not put per-instruction cycle annotations or captured-result playback into production game C.

3. Compare defined full state at aligned frame and routine boundaries: CPU states, work/Z80 RAM, VDP memories/registers, interrupt/DMA state, device-port order, rendered frames and aligned raw chip audio where supported. Enumerate any inaccessible state as an open gate.

4. Handle calls and replays spanning frames, long waits, resets/state loads, nested IRQs, bus ownership, Z80 uploads, and trace-counter rebasing with bounded queues and two-sided timeout diagnostics.

5. Extend native testcart scheduling beyond the first input_frames snapshot and prove both reference and native paths consume changing per-frame, per-port input. Keep public gameplay input separate from harness-only calls/pokes.

6. Bind every comparison to the exact substituted routine set and detect hidden fallback, missing samples, dropped snapshots and stale libraries.

## Required artifacts

Lockstep runner/comparator, state-schema and replay contract, cross-frame/input fixtures, failure bundles and source-bound result ledger.

## Acceptance criteria

- Concurrent 68000/Z80 synthetic programs pass with C substituted on both processors and nonzero frames/calls/accesses compared.
- Planted IRQ, DMA, input-frame, sample, snapshot and cycle-order defects fail or explicitly report unavailable evidence.
- Timeouts stop both processes and retain the first unresolved interval; no orphaned workers remain.
- Skipped/unsupported intervals remain UNVERIFIED and cannot enter a fully certified aggregate.

## Validation

Run new lockstep controls, mise run test:md-trace, mise run test:md-rom, and affected input/profile/kit checks. Use deterministic repeated input and at least one call crossing a frame boundary.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not infer independent hardware accuracy from BlastEm agreement. Preserve the six existing known candidate differences as named limitations; never mask an entire frame or audio stream to pass around one difference. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D05: Verify combined C substitution under concurrent hardware execution, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d05-whole-image-lockstep.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Build the MD equivalent of A7 so routines that pass alone must also behave correctly together.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
