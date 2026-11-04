# D00: Establish the SNES parity target and select the first title

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Bind the work to an exact ROM revision, a measurable definition of SNES workflow parity, and a recorded baseline.

## Prerequisites

None. Shared preparation can proceed without a commercial ROM; title binding requires a user-supplied runtime ROM or library path. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Inventory the actual SNES decomp tools available to the user, using the SNES playbook as the minimum reference. Identify reusable runner, catalog, assembly, A5, A7, and typed-C gates. If the original game checkout is unavailable, record that fact; do not claim its tools were ported.

2. Record nativekit revision, tool pins and hashes, build configuration, host, model, region, pad types, and current known differences. Separate prior handoff results from newly executed checks.

3. Shortlist at least three user-owned base-console candidates when the library permits. Rank code size, dynamic control flow, RAM execution, sound-driver complexity, available legitimate reference material, and hardware fit. Use longer scripted observation plus static inspection; the 120-frame survey cannot prove absence of SRAM, mappers, or later-game features.

4. Select one exact first-title revision within current hardware scope. Prefer a title without required SRAM/EEPROM, mappers, or expansion hardware. Keep unsupported candidates in the report with reasons; never quietly broaden the platform.

5. Choose a maintained game workspace and ownership boundary. Default generated/private work to ignored build/decomp/<game-id>/ until the user supplies or authorizes a separate game repository. Record ROM SHA-256, original size, container/normalization policy, and offsets without committing private paths or content.

6. Define title-specific cold-boot, menu, gameplay, failure/retry, completion, audio, controller, and applicable save/region scenarios. Write the completion matrix before translation begins.

## Required artifacts

baseline.md, workflow-parity.md, title-selection.md, and a versioned title manifest/scenario inventory. Put public methodology under consoles/md/docs/decomp/; keep private bindings and generated captures under build/decomp/<game-id>/.

## Acceptance criteria

- The selected title is identified by exact hash and hardware requirements, with an explicit eligibility decision and reasons.
- Every SNES deliverable has an MD counterpart or a named missing capability and owning milestone.
- The scenario matrix distinguishes first playable, full game, routine certification, and unreachable-code handling.
- Baseline failures, missing hardware, and unavailable reference tooling remain visible; a prior pass is not a fresh pass.

## Validation

Read the Stage 4 handoff and contract; use mise run test:md-consumer and focused existing gates only where needed to establish new baseline evidence. Run the survey only against the user-supplied runtime path. No title run is possible until that input is available.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not guess a ROM revision, obtain ROMs from the network, treat a survey completion as nmd compatibility, or write into another repository without authorization. Missing ROM input blocks title-dependent work; shared synthetic tooling can continue with its status explicitly partial. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D00: Establish the SNES parity target and select the first title, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d00-baseline-and-first-title.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Bind the work to an exact ROM revision, a measurable definition of SNES workflow parity, and a recorded baseline.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
