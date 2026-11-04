# D06: Deliver the first game slice through the actual native launch path

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Prove the complete workflow on real title code before translating the whole game.

## Prerequisites

D00 selected title, D02 execution path, D03 title skeleton, D04 and D05 verified checking infrastructure. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Choose and document a bounded cold-boot-to-gameplay scenario, such as title/menu plus one controllable level segment, with audio and explicit expected state checkpoints. Declare its routine/code-byte frontier.

2. Recover reset/startup, video initialization, required IRQs, input, essential game logic and the relevant Z80 driver/upload path. Translate bottom up with WHAT/HOW comments and typed names; retain ABI wrappers only at justified boundaries.

3. Run each new routine through per-call and combined lockstep gates, keep its assembly rebuilding exactly, and integrate the C into the native game immediately.

4. Build a standalone game project using add_subdirectory and linking nmd, with the same C files used by the hybrid. Load user-owned ROM data at runtime with hash verification; never execute its code bytes.

5. Run via nmd_main in headless and windowed modes. Exercise keyboard and actual controller input through the release path; capture video/audio and first-difference diagnostics.

6. Use the slice to expose genuine hardware/API gaps. Fix hardware behavior with separate synthetic/reference evidence and contract discipline; retain title logic in the game.

## Required artifacts

A runnable native first-title slice, source/assembly/catalog/verdict commits in the game workspace, input scenario, capture report and explicit remaining-frontier ledger.

## Acceptance criteria

- One native binary cold-boots and reaches the declared gameplay interaction using C on both processors, with meaningful video and sound.
- No reference core, emulator library, guest interpreter/JIT or unconverted-code fallback is present in the standalone execution path.
- The declared slice's per-call/lockstep and whole-ROM assembly gates pass with nonzero evidence.
- Input materially changes game state; a held screenshot or replay of recorded frames/sound cannot satisfy the gate.

## Validation

Build/run the game using its newly documented CMake and headless script commands. Run relevant nmd regression tasks and test:md-consumer after platform fixes. Keep manual window/controller review separate from automated evidence.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Call this first playable only. Do not report full game, all routines certified, native timing parity, or complete hardware support from this slice. If sound/IRQ/main code still falls back to the candidate, the native slice gate remains open. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D06: Deliver the first game slice through the actual native launch path, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d06-first-native-playable-slice.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Prove the complete workflow on real title code before translating the whole game.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
