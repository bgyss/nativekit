# D12: Prove reuse on a second ROM and document the operating workflow

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Turn the first-title method into a reusable assisted decomp workflow with honest eligibility and progress reporting.

## Prerequisites

D11 full first-title delivery; D00-style identity/hardware binding for a distinct second title or engine. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Extract reusable intake, trace, census, rebuild, ABI, A5/A7 and reporting logic from title-specific scripts. Keep game addresses, RAM maps, upload identities, scenarios and policies in versioned project configuration.

2. Provide an orchestration CLI that discovers existing project state, validates inputs/capabilities, resumes the first unmet gate and emits exact diagnostic/artifact paths. Define the interface before implementing it; never return success merely because files were generated.

3. Support a new-project flow from a runtime ROM path, with eligibility checks, project manifest, reproducible skeleton, worklist and unresolved decisions. Document which work remains agent-assisted/manual and which commands are automated.

4. Choose a second title with meaningfully different control flow or sound-driver structure. Use a predefined bounded slice and pass intake, byte-exact rebuild, routine checks on both CPUs, combined lockstep and native launch using the shared tool path.

5. Measure what was reused, required per-title configuration and any shared-code edits. If title-specific branches leaked into the generic tool, move them into explicit configuration or documented capability extensions.

6. Write an MD decomp playbook with first-title lessons, troubleshooting, exact commands, coverage/status definitions, data boundaries and support matrix. Keep future unsupported hardware as separate milestones.

## Required artifacts

Reusable decomp CLI/configuration, second-title bounded pilot/evidence, workflow/playbook and an explicit automation/eligibility support matrix.

## Acceptance criteria

- The complete first title still verifies after extraction of shared tooling.
- A distinct second title passes the predeclared slice through the shared tools without a hidden emulator fallback in native execution.
- The CLI rejects unsupported hardware, wrong ROMs, missing capabilities and stale evidence; unknown code remains an open gate.
- Documentation distinguishes first-title complete, second-title slice verified, reusable tooling and arbitrary-ROM automatic decompilation.

## Validation

Run shared decomp checks, both title projects' affected gates and the first title's full release suite once after shared-tool extraction. Include malformed/unsupported input controls and a real interrupted/resumed workflow.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not claim any Genesis ROM can be automatically decompiled from two examples. Full second-title recovery and expansion hardware remain separate future goals unless explicitly added. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D12: Prove reuse on a second ROM and document the operating workflow, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d12-reusable-rom-to-decomp-workflow.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Turn the first-title method into a reusable assisted decomp workflow with honest eligibility and progress reporting.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
