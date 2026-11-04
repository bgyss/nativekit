# D02: Make the native game reset and execution path real

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Provide a supported nmd_main path that starts a game's native reset routine and resumes its main logic alongside the Z80 and hardware.

## Prerequisites

D00 contract inventory; D01 supplies reference-backed controls where needed. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Audit nmd_power, nmd_run_frame, bus reset, routine dispatch and the SNES execution path. At the planning baseline, MD reset clears CPU state and the frame runner advances hardware; it does not read and dispatch the game's reset entry or run a 68000 main coroutine.

2. Design the smallest documented entry/resume mechanism, including initial stack/reset-vector state, long-lived game execution, frame yields, hardware waits, stop/restart, reset repetition, and fatal missing entries.

3. Audit interrupt and return-state semantics rather than assuming whole-register restoration is equivalent to the original handler. Check guest-stack-visible effects, status masks, nesting/defer behavior, computed entries and RAM-installed vectors against authored controls.

4. Define the game-facing ABI boundary for each CPU. Audit 68000 D/A/SR/PC and stack-mode requirements, and Z80 alternate registers, flags, interrupt modes and restart semantics needed by the title; add justified state without implementing an opcode loop.

5. Maintain C11 and single-timeline deterministic scheduling. Keep code helpers in the game or verification harness where possible, and console-neutral concepts in kit/. Inventory the actual MD memory-dump/state-observation capabilities needed for later native comparisons. Supply a documented observation path that does not perform extra timed game bus accesses or inject game state; do not assume the SNES adapter's memory exports already exist for MD.

6. Apply contract discipline to frozen 1.0 interfaces: additive opt-in fields/calls must preserve existing zero-initialized consumers; breaking changes require an explicit version decision and migration.

## Required artifacts

A documented native execution contract, minimal runtime implementation and version changes if justified, plus a synthetic native cartridge with a real reset/main/IRQ/Z80 program expressed as C.

## Acceptance criteria

- The authored cartridge boots through nmd_main, runs main logic over multiple frames, accepts changing input, handles interrupts and sound work, and resets reproducibly.
- Missing reset/IRQ/Z80 targets fail explicitly; blank frames or an idle consumer cannot satisfy the execution gate.
- Negative controls detect a skipped reset entry, lost state across a yield, and an incorrect interrupt return.
- Existing public consumers retain behavior and the contract/version decision is recorded.

## Validation

Run mise run test:md-console, mise run test:md-interrupt, mise run test:md-io, mise run test:md-z80, mise run test:md-bus, mise run test:md-consumer, and mise run test:kit as affected. Add a synthetic execution check through the public game launch path.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not add a 68000/Z80 interpreter, guest JIT, instruction dispatch loop, or title-specific behavior to make the fixture run. Do not declare the runtime game-ready from synthetic execution alone. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D02: Make the native game reset and execution path real, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d02-native-reset-and-execution.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Provide a supported nmd_main path that starts a game's native reset routine and resumes its main logic alongside the Z80 and hardware.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
