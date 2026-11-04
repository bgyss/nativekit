# D09: Turn certified translations into maintainable plain C

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Deliver readable game source rather than an instruction-by-instruction register simulation.

## Prerequisites

D07 certified behavior; preserve D08 assembly rebuilding throughout. Refactoring can be incremental after a certified batch. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Convert leaf routines first to named typed parameters, return values, structs and game concepts. Keep a single source tree shared by native execution and the hybrid checker.

2. Confine CPU-register adaptation to explicit entry wrappers required by by-address dispatch and interrupts. Remove routine bodies whose only abstraction is a copied register file, opcode helper sequence or guest PC state machine.

3. Introduce named RAM layouts/accessors with checked offsets, aliasing, widths and wrap behavior. Preserve ordered observable accesses and exit flags/registers still consumed by callers.

4. Use old-versus-new routine comparison on curated and generated valid entry states, then original-versus-C A5/A7 for changed control-flow and ABI boundaries.

5. Measure remaining register-shaped bodies, instruction helpers, unresolved names and restart/dispatch glue transparently. Do not define a plain-C metric that a renamed register carrier can satisfy.

6. Keep symbolic assembly authoritative and preserve all baseline behavior. Optional gameplay improvements remain separate work after preservation acceptance.

## Required artifacts

Typed C source, RAM layouts and ABI adapters, old/new verification reports and a reviewed plain-C completion inventory.

## Acceptance criteria

- Game logic is expressed in game-level C; remaining register-state access is limited to documented boundary adapters, not the main implementation.
- No opcode dispatch table, per-instruction cycle statements, decoder dependency or guest execution loop is shipped.
- Original-versus-C gates remain green for the changed code and integrated scenarios; assembly still rebuilds the original exactly.
- A planted dropped incidental register/flag or misaligned RAM field is caught by the relevant checker.

## Validation

Use the game workspace's typed-refactor checker, targeted A5/A7 checks and whole-ROM rebuild. Build the actual native binary after each completed conversion slice.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

A carried-register rewrite, generated instruction C, or zero metric obtained by hiding adapters is transitional output. Do not claim plain C until the inventory and code review support it. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D09: Turn certified translations into maintainable plain C, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d09-plain-typed-c.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Deliver readable game source rather than an instruction-by-instruction register simulation.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
