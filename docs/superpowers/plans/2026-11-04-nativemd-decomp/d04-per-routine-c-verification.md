# D04: Verify C routines against original execution on both CPUs

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Create the MD equivalent of the SNES A5 checker with explicit entry contracts and failure-sensitive evidence.

## Prerequisites

D01 hooks, D03 shared catalog/decoder controls; use synthetic code before title batches. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Define routine identities by CPU, address, code generation and ROM revision. Generate ABI wrapper tables from the catalog with validated aliases, computed targets and exact ownership; avoid fragile source regex assumptions.

2. Provide test-only 68000/Z80 semantic helpers and curate primary-manual references. Cover the arithmetic/flag, signedness, shifts, addressing side effects, stack/call/return and interrupt cases actually claimed; use independent vectors, not a decoder testing itself.

3. Capture each original call's entry state, ordered reads/writes, return condition, nested calls and relevant memory effects. Replay C from the same entry state with defined hardware-read replay and exact event consumption.

4. Compare exit registers/flags required by the entry contract, memory effects, access widths/order/values and target dispatch. Make extra reads, missing writes, wrong flags and wrong returns actionable first-difference reports.

5. Treat interrupt/DMA overlap explicitly. Isolate with valid staged harness calls or model it with the later lockstep gate; report unverified when the checker cannot judge safely.

6. Implement IDENTICAL, DIVERGENT, UNVERIFIED and UNCOVERED statuses with real call/access counts. Allow reference fallback only in the test hybrid and record every fallback.

## Required artifacts

Hybrid-library builder, ABI tables, per-call runner/checker, semantic-helper controls, verdict schema and minimal verified 68000 and Z80 C examples.

## Acceptance criteria

- At least one routine per CPU and a mixed-CPU communication case pass against original execution with nonzero checked calls and accesses.
- Planted changes to flags, one load/store, a computed target and a return fail for the expected reason.
- Unsupported overlap, missing replay data and zero-call runs cannot produce IDENTICAL.
- The same C source can later bind to nmd bus functions without a second port tree.

## Validation

Use synthetic controls for both CPUs and the new per-call check command. Run existing bus/Z80/interrupt tests if runtime seams change. Record exact core/source/scenario identities in every verdict.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not promote machine-shaped draft C, a successful compilation, zero checked calls, or emulator fallback into routine certification. CPU helpers are transitional verification tools, not the final game's instruction engine. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D04: Verify C routines against original execution on both CPUs, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d04-per-routine-c-verification.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Create the MD equivalent of the SNES A5 checker with explicit entry contracts and failure-sensitive evidence.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
