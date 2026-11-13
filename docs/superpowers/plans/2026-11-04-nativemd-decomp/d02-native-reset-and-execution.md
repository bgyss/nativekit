# D02: Make the native game reset and execution path real

**Status:** Complete 2026-11-10 against authored synthetic controls ([results](#results)); not a retail-title or hardware qualification. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

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

## Results

Executed 2026-11-10 on branch `nativemd` from `4496637` by the
[implementation plan](../2026-11-10-nmd-decomp-d02-implementation.md). Host:
macOS (Darwin 27.3.0, arm64), `build/null` headless configuration; candidate
tools at the pins in `tools/toolchains/pins.env`. No ROM input was used: every
fixture is authored C built from this tree.

Artifacts:

- Contract: [native execution](../../../../consoles/md/docs/decomp/native-execution.md) and the 1.1 section of the [nativemd contract](../../../../consoles/md/docs/contract.md).
- Runtime: `consoles/md/src/console/dispatch.c` (68000 coroutine, native interrupt return, `nmd_restart`, `nmd_set_sr`), `consoles/md/src/z80/z80.c` (stacked Z80 interrupt return), public headers under `consoles/md/include/nmd/`.
- Authored cartridge: `consoles/md/tests/consumer/native.c`, checked by `consoles/md/tests/consumer/run.py`.
- Unit controls: `consoles/md/tests/unit/execution_check.c`, `consoles/md/tests/execution_check.py`.

Version decision: additive only, so **1.1.0**. New things are one appended
cart field whose zero value is 1.0's behavior, appended CPU-state fields, four
calls, and two dump regions. The existing consumer source changed only in its
version assert; `md_consumer` still runs its five idle frames.

| Acceptance line | Evidence |
|---|---|
| Boots through `nmd_main`, main logic over frames, changing input, interrupts, sound work, reproducible reset | `md_native`: 68 scripted frames, three dumps; boot marker, main-frame and interrupt counts, pad history, main's running sum, Z80 command/acknowledge/interrupt counters all equal to values computed from the script; three distinct pictures; non-silent sound; 11 output files byte-identical across two runs. In-process re-power, including while suspended in a handler: `execution_check` `repower`. |
| Missing reset/IRQ/Z80 targets fail; idle cannot pass | 10 fatal cases with exact messages; `NMD_NATIVE_PLANT=skip-reset` (an idle host-mode cart) is rejected: "the reset routine left no boot marker". |
| Negative controls: skipped reset, lost state, wrong interrupt return | Runtime plants 1/2/3 fail 9 named unit cases; cart plants `lose-state` ("main ran 1 frames of 30") and `bad-return` ("1 vertical interrupts in 30 frames") are rejected. |
| Existing consumers keep behavior; version recorded | All tasks below; 1.1.0 above. |

Commands run from the repository root, all exit 0: `mise run test:md-execution`,
`test:md-console`, `test:md-io`, `test:md-z80`, `test:md-bus`,
`test:md-interrupt`, `test:md-sound`, `test:md-vdp`, `test:md-dma`,
`test:md-render`, `test:md-consumer`, `test:kit`, `test:md-rom` (59-case
matrix), `golden:md-compare`, `test:md-profile`, `test:md-golden-check`, and
`git diff --check`. Not run: the macOS/Windows/iOS host builds, `mise run
test`, and the SNES tasks (no kit or SNES source changed).

Limits and open items:

- The unit checks were written alongside the runtime rather than before it;
  their ability to fail rests on the planted-error runs, not on an observed
  red run of each case.
- Nothing native was compared with the reference core. The interrupt frame's
  stacked PC is the game's own `nmd_cpu.pc`, and exception/acknowledge cycles
  beyond their bus accesses are not charged.
- The first title's private workspace was absent, so its needs (other
  exceptions, TRAP, trace, Z80 NMI) are unverified: D03/D06.
- VRAM, CRAM, VSRAM, sound-chip and I/O state have no public observation
  path yet: D05.
- A main loop with no bus access or wait never yields.

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
