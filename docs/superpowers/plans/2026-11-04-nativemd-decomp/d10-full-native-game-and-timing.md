# D10: Qualify complete native gameplay and measured timing

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Run the complete translated game through nmd with measured timing and input-driven evidence, while resolving real title-exposed platform defects.

## Prerequisites

D02, D07 and D09; D08 preservation image remains unchanged. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Run all declared gameplay scenarios on the standalone native binary: cold boot, reset, menus, gameplay progression, failure/retry, ending/credits, audio transitions and applicable player/region modes. Keep harness injection out of the native acceptance path.

2. Audit every reached hardware operation against the nmd contract. Fix console defects with fresh minimal synthetic controls and candidate/manual evidence; keep previously documented candidate differences visible. Unsupported required hardware is an explicit scope decision.

3. Measure both processors using nmd-profile across representative attract, gameplay, high-load and audio-heavy spans. Fit on training spans, reserve held-out spans, bind source/core/ROM hashes and check stale profiles.

4. Predeclare title-specific timing metrics and numerical acceptance limits from the measured baseline before tuning. Record distributions and worst cases, busy-frame behavior and device waits; frame rate is a check, not a fit target.

5. Keep exact behavioral/harness claims separate from profile-driven native timing. Do not require or advertise identical native frames when measured work estimates allow frame-boundary drift; enumerate exact and bounded comparisons individually.

6. Check real release input, audio drain/pacing, window resize/presentation and deterministic headless behavior. Qualify each available host separately; implement save/resume only if the title and runtime support it, and distinguish checkpoint state from battery saving.

## Required artifacts

Full native scenario results, versioned measured profiles and held-out report, platform defect fixes, captures and per-host/manual-review status.

## Acceptance criteria

- All declared game scenarios complete through ordinary supported input on the delivered native execution path.
- Fit and held-out timing meet predeclared bounds, with residuals and unsupported measurements reported; failed bounds remain open.
- Required hardware regressions have minimal checks; no broad ignore filter or altered golden hides a new difference.
- Automated behavior, native timing, host checks and human gameplay review have separate verdicts; missing hardware/manual gates remain explicit.

## Validation

Run the game's native scenario suite, nmd-profile measure/check with external ROM input, and affected MD regression tasks. At the platform release boundary run the preservation matrix in README.md and document existing Windows/host-render limits rather than silently waiving them.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not add cycle-by-cycle CPU emulation to improve timing. If a profile cannot meet the declared timing bounds, report model limits and propose a measured refinement before broadening claims. Candidate agreement is not independent hardware qualification. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D10: Qualify complete native gameplay and measured timing, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d10-full-native-game-and-timing.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Run the complete translated game through nmd with measured timing and input-driven evidence, while resolving real title-exposed platform defects.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
