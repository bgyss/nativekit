# D11: Package the first completed decomp and native game

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Deliver a reproducible, inspectable first-title result that another contributor can build and verify.

## Prerequisites

D07-D10 complete at their declared gates; D08 full image and D09 plain-C standards are required for the full claim. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Create a clean game checkout/build recipe using pinned nativekit and tools, user-supplied ROM data, hash validation, generated extraction and source-only artifacts. Keep original assets and private traces outside version control.

2. Provide one documented flow for verifying the ROM, rebuilding the preservation image, building the native binary, running scenarios and inspecting evidence.

3. Audit linked libraries, packaged files and launch behavior to establish that BlastEm, hybrid fallback, guest interpreters/JITs and analysis-only decoders are absent from native delivery.

4. Publish a candidate-bound completion report: exact title/revision, code-byte counts per CPU/generation, T3/T4 results, native scenarios, timing ranges, platform versions, known differences and remaining human/hardware gates.

5. Review provenance and license obligations for original project code, console code, test patches, assemblers and any external disassembly/reference. Keep vasm and candidate test tools out of the shipped game as required by the existing tool policy.

6. Demonstrate the clean build on an available host and perform an actual windowed/controller/audio review. Record other hosts as unqualified until their real builds/runs occur.

## Required artifacts

Maintained game repository/workspace handoff, extraction/build/verification entrypoints, source manifest, dependency audit and final first-title evidence report.

## Acceptance criteria

- A clean authorized workspace plus the exact user's ROM produces the verified assembly image and runnable native game using documented commands.
- All full-first-title claims are backed by D07-D10 evidence bound to the final source revision.
- The packaged runtime has no emulator or fallback dependency; a wrong ROM fails before extraction/build.
- Build reproducibility, behavioral equivalence, timing, human review and hardware accuracy are reported as separate claims.

## Validation

Perform a clean first-title build/rebuild/scenario run, dependency/package audit, documentation link/privacy checks and git diff --check. Do not mark unrun remote or physical-host gates passed.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not push, publish source/releases, change repository defaults, upload ROM-derived artifacts, or modify external repositories beyond authorized scope. A reviewable local result is sufficient for this milestone's delivery work; public publication requires explicit authorization. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D11: Package the first completed decomp and native game, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d11-reproducible-first-game-delivery.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Deliver a reproducible, inspectable first-title result that another contributor can build and verify.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
