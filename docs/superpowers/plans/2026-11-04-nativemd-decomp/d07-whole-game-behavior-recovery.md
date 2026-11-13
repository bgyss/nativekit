# D07: Recover and certify the complete game's C behavior

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Translate the remaining 68000 and Z80 code into auditable C and close behavior verification across the whole game.

## Prerequisites

D06 first playable and stable A5/A7 infrastructure. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Work by responsibility and bottom-up call dependencies: engine, objects, collision, rendering setup, menus, modes, audio, exceptions and region paths. Include every uploaded/replaced Z80 program and RAM-code generation.

2. For each bounded batch, recover assembly and C together, state entry/exit/clobber contracts, rebuild the complete ROM, run reaching A5 scenarios, then the combined A7 suite on committed source.

3. Expand scenarios across levels/modes, completion, failure/retry, pause, two-player or peripheral paths when supported, audio transitions, resets and rare dispatch targets. Report progress by uniquely owned code bytes per CPU and code generation.

4. Close static census candidates and unobserved branches with reviewed classification and valid harness-only entry scenarios. Keep gameplay-only coverage distinct from staged unit coverage.

5. Record uncovered, divergent and unverified bytes and calls without shrinking the denominator. A routine's reachable subset cannot certify the remainder.

6. Investigate reference/native differences from the first exact divergent event. Do not add title-specific KNOWN entries merely to clear decomp regressions.

## Required artifacts

Complete C companion and assembly filing, expanded scenario corpus, routine ownership/census map and revision-bound A5/A7 ledgers.

## Acceptance criteria

- Every code byte in the reconciled census belongs to a named routine and has a recorded behavior status; no unresolved indirect target or unknown executable generation remains.
- All routines claimed T3 meet the adapted SNES completeness, coverage, per-call and combined-lockstep gates.
- Full T3 completion requires zero DIVERGENT, UNVERIFIED and UNCOVERED code bytes; genuinely untestable code remains an explicit blocker rather than an exception that grants credit.
- The suite exercises actual progression and all declared title scenarios with nonzero frames and calls.

## Validation

Run the game's whole-image rebuild, targeted A5 checks per batch and one integrated A7 suite at each meaningful completion boundary. Run shared/platform regressions when their implementation changes, avoiding repeated full sweeps for naming-only edits.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

If a routine cannot safely run under a valid entry contract, retain an open gate and state its bytes/reason. Do not equate tested gameplay completion with whole-ROM code certification. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D07: Recover and certify the complete game's C behavior, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d07-whole-game-behavior-recovery.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Translate the remaining 68000 and Z80 code into auditable C and close behavior verification across the whole game.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
