# D01: Build the decomp verification ABI for both reference CPUs

**Status:** Complete for the pinned candidate-tooling and authored synthetic acceptance criteria. This does not qualify BlastEm as hardware-accurate or certify a retail title. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Extend the pinned BlastEm test build from access logging into a controlled reference for routine recovery and C substitution.

## Prerequisites

D00 workflow and baseline inventory; a retail ROM is not required for synthetic hook controls. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Specify a separate versioned decomp capability ABI. Existing NMDT v2 access-completion events are not instruction boundaries or a complete CPU-state interface; preserve existing trace consumers.

2. Expose instruction-boundary PC, decoded extent or fetched bytes, CPU register/flag state, control-flow edges, reset and interrupt entry/return for both CPUs, M68K exception entry/return, code-fetch versus operand/data provenance, and global clock epochs. The Z80 has no architected exception-entry path.

3. Provide checked state capture/restore, complete machine checkpoints at documented safe boundaries, controlled routine execution to explicit return/stop criteria, and execution replacement hooks. Audit all state actually needed, including alternate/hidden CPU state and prefetch/scheduler state where reference execution depends on it.

4. Expose ordered real bus transactions and the event timing needed by the later replay harness. Access-completion timestamps alone must not be advertised as full start/end or idle-cycle coverage.

5. Test normal calls, tail/computed transfers, reset, IRQ, RAM-resident code, nested calls, state loads, counter rebasing, and both CPUs active together. Keep test hooks in candidate tooling, never the delivered native game.

6. Fingerprint patches, core, ABI, runner, scenario and ROM. Reject missing capabilities, truncated records and mismatched identities before accepting evidence; a hook rebuild invalidates dependent evidence. Benchmark a bounded representative synthetic run on both CPUs: events/second, bytes/frame, runner overhead and peak memory. Use streaming capture, declared storage limits and child-process timeouts; estimate each title run's duration/storage before launching it. Chunking must preserve state/epoch identities and must not discard required evidence.

## Required artifacts

A decomp ABI document/header, fresh BlastEm test patches, runner support, authored synthetic hook fixtures, and a measured trace resource-budget report under the planned decomp tool/test directories.

## Acceptance criteria

- Both CPU hook paths execute real synthetic instructions and expose independently checked state and flow.
- Capture/restore reproduces the same subsequent state and event stream across repeated runs and a state-load boundary.
- At least one deliberately wrong PC/register/epoch/capability is rejected by each corresponding gate.
- Existing trace/profile/survey formats and tests retain their documented behavior.
- Bounded capture stays within declared memory/storage limits; exceeded limits and child timeouts produce explicit incomplete results, never a successful truncated trace.

## Validation

Run `mise run test:md-toolchain`, `mise run test:md-oracle-smoke`,
`mise run test:md-trace`, and `mise run test:md-profile` when their paths
change. `mise run test:md-decomp` is the non-empty shared entrypoint for the
NMDH ABI, format, runner, actual-core and checkpoint controls; it depends on
`tools:md`. Run `python3 consoles/md/tools/decomp/benchmark.py --frames 30`
before reporting the D01 resource budget in
[`consoles/md/docs/decomp/resource-budget.md`](../../../../consoles/md/docs/decomp/resource-budget.md).

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

If the pinned core cannot safely expose a required hook, produce a measured blocker and a concrete ABI/core adaptation proposal. Do not substitute access traces for missing execution evidence or swap reference cores silently. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D01: Build the decomp verification ABI for both reference CPUs, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d01-reference-execution-hooks.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Extend the pinned BlastEm test build from access logging into a controlled reference for routine recovery and C substitution.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
