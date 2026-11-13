# Goal: first complete NES game decompilation and native playback

Implement and qualify the NES side of this repository so one chosen NES game
can be recovered and run using the same preservation method as the SNES side:
symbolic assembly that rebuilds the original image byte for byte, accompanied
by readable native C whose behavior is checked against the original. The C
must run on the NES console implementation with working picture, sound, input,
and the selected cartridge hardware. Complete the chosen game, not only boot,
a title screen, or a synthetic demonstration.

This is an execution prompt. Implement the missing console and game work,
run meaningful tests and reference comparisons, fix failures, and retain the
evidence. A plan or additional convenience helpers alone do not achieve it.

## Sources of truth

Read these before planning or editing:

- [SNES game contract](../../../consoles/snes/docs/contract.md), especially the cartridge/console
  boundary, ordered bus accesses, waits, native dispatch, and timing model.
- [SNES decompilation playbook](../../../consoles/snes/docs/snes-decomp-learnings.md), especially the
  preservation tiers, early integration, per-call and lockstep oracles,
  coverage census, and timing without instruction annotations.
- [NES implementation and limitations](../../../consoles/nes/README.md).
- [NES machine interface](../../../consoles/nes/include/nnes/machine.h) and
  [cartridge interface](../../../consoles/nes/include/nnes/nes.h).
- [NES cost-profile contract](../../../consoles/nes/docs/cost-profile.md).
- [Existing NES foundation plan](2026-11-02-nes-foundation.md).

Translate SNES-specific details to the 2A03/2A07 and NES hardware. The SNES
contract's claims of hardware parity do not establish NES parity. Preserve
the distinction between bit-exact assembly, behavior-verified C, and any later
enhanced mode.

## Target and initial decisions

Before title-dependent work, identify the exact game, region, revision, image
format, SHA-256, mapper/submapper, board wiring, mirroring, RAM/battery layout,
and supported controllers. Record the local input path in an ignored local
configuration; keep machine-specific paths out of checked-in documents.

Use the game the user specifies. If none is specified, inspect the authorized
workspace for an existing title project and ask for the title/revision and
local image location if still missing. Continue baseline and generic hardware
work while awaiting that information. Do not silently select a different game.
A mapper-0 NTSC title is a useful first target when the user has no preference;
mapper 0 is a proposed starting scope, not permission to replace their choice.

Use a locally supplied image and data the user is authorized to use. Do not
download commercial ROMs or commit their images, extracted copyrighted assets,
or reference captures. Define reproducible local extraction and verification
commands, with hashes and manifests in Git where appropriate.

## Architecture and boundaries

1. Keep the NES implementation in `consoles/nes`, with its own `nnes` API,
   CMake targets, state, and dependencies, and NES-specific tools under
   `consoles/nes/tools`. Keep game recovery out of nativekit: a title's
   recovered assembly and C, its board adapter, its ledger and its
   title-specific tools live in that title's own repository, which builds
   against nativekit (as the Mega Drive decomp plan also requires). Preserve
   existing SNES and Mega Drive/Genesis behavior and builds.
   *(Updated 2026-11-09 when the prototype moved from `platforms/nes` and
   `games/nes/<title-slug>` into nativekit's layout.)*
2. Production execution uses recovered game C and native address dispatch.
   A reference emulator/CPU interpreter may provide observations in validation
   tools; it must not execute missing game routines in the delivered native app.
   Missing routines and unsupported hardware remain visible failures.
3. Implement generic NES hardware for behavior the selected title reaches.
   Preserve hardware behavior at the bus boundary. Do not substitute
   title-specific fake register values, prerecorded frames/audio, patched game
   logic, or an emulator video/audio stream for the native implementation.
4. The game provides recovered C, its routine table, cartridge data, and its
   board adapter. The console provides hardware and scheduling. Establish a
   written NES game contract at `consoles/nes/docs/contract.md` before changing
   the public interface substantially.
5. Preserve the SNES model in which recovered game C does not carry an
   instruction clock ledger. Audit the current explicit NES cycle charges and
   dummy-read helpers against that model. Keep real ordered bus operations,
   waits, and measured access profiles; avoid adding per-instruction `cyc(n)`,
   opcode fetch execution, or instruction-boundary calls throughout game C.
   Reference replay may retain instruction/cycle information for verification.
6. Derive timing/profile values from reference observations and hardware
   specifications. Report residual error and pacing limits. Do not tune weights
   by eye or claim universal accuracy from a finite scenario suite.

## Ordered implementation milestones

Maintain one implementation plan and a status ledger with commands, results,
source revisions, unresolved defects, and the next concrete action. Work
through the milestones in order, integrating an initial native vertical slice
early rather than finishing a large framework before attempting a real routine.

### M0: establish a truthful baseline

- Inspect the current checkout and applicable repository instructions. Read
  the existing tests and helper implementations before extending them.
- Configure, build, and run the NES contract tests and synthetic cartridge.
  Fix actual failures before treating the foundation as usable. In the
  currently inspected example, the routine table contains two entries but
  `.routine_count` is `1`; recheck and correct this registration defect.
- Add meaningful regression coverage for helper risks used by the title:
  arithmetic flags, page/zero-page wrap, bus ordering and dummy accesses,
  RMW pairing, stack behavior, interrupts, restart, and multi-instance isolation.
  Exercise representative edge cases and failure paths; avoid redundant tests
  that merely reproduce the implementation expression.
- Record which behavior is demonstrated by synthetic tests and which has
  actually been compared with a reference. Run relevant SNES regression checks
  if shared files change.

Current commands, from the repository root: `mise run test:nes`, or the
library alone:

```sh
cmake -S consoles/nes -B build/nes -DNNES_TESTS=ON -DNNES_EXAMPLES=ON
cmake --build build/nes
ctest --test-dir build/nes --output-on-failure
./build/nes/nnes_native_cart
```

Select a working compiler explicitly with `-DCMAKE_C_COMPILER=...` if the
default toolchain fails its compiler probe. Record the compiler and SDK used;
do not confuse a configure failure with a library failure.

### M1: establish the game record and a working oracle

- Record the chosen image identity and create a deterministic controller-input
  scenario from reset. Pin a reference implementation, version/commit, settings,
  region, and capture schema. Mesen2 is an initial candidate, not an already
  validated capture path: the existing Lua collector/source patch has not
  been exercised live and must be proved before use.
- Produce real reference observations for a short chosen-game interval:
  CPU/register state, ordered bus operations, relevant RAM, mapper state,
  PPU state/frame pixels, and audio where reached. Check timestamps and
  operation-type labels, including dummy and DMA accesses.
- Recover and compare at least one real game routine through a working C
  verification seam early. Keep its original code/data mapping explicit.
- Prove the comparator rejects planted register, bus-value, access-order,
  missing-dispatch, and truncated-capture defects. Use disposable copies or
  fixtures and remove the planted defects after demonstrating failure.
- Begin symbolic assembly reconstruction and the code/data census. A raw
  `.incbin` of executable ROM bytes cannot qualify as recovered assembly.

### M2: cartridge mapping and native routine identity

- Implement the chosen board's PRG/CHR mapping, mirroring, RAM, trainer handling
  if present, reset state, bank switching, and IRQ behavior where applicable.
  Parse image metadata only as needed to identify and extract the cartridge;
  loading an image does not imply executing its instructions.
- Replace the current address-only dispatch limitation for banked games with
  identities that distinguish the executable content mapped at a CPU address.
  Define equivalent identity rules for uploaded/RAM code if the title uses it.
  Do not pretend one C function per 16-bit address covers multiple banks.
- Preserve a simple mapper-0 path. Prove mapping and dispatch with fixtures and
  the selected game's real bank transitions when present.

### M3: timed console hardware and host execution

- Implement a deterministic scheduler that advances NES hardware on bus
  accesses, hardware waits, and DMA. Deliver scheduled interrupts at defined
  operation boundaries without splitting protected RMW/multi-byte operations.
  Validate nested NMI/IRQ handling and native restart/unwind behavior.
- Implement CPU-visible PPU registers, latches, read buffering, VRAM addressing,
  nametable/palette/OAM behavior, rendering and relevant sprite/background
  interactions, vblank and NMI behavior, and the selected region's timing.
- Implement OAM DMA, DMC DMA and their stalls/interactions; implement the APU
  channels, frame sequencer, IRQs, sample generation, and mixing needed by the
  selected title. The NES sound driver is recovered 6502 game C, not an SPC700.
- Implement controller behavior and open-bus behavior reached by the target,
  including relevant I/O reads and dummy accesses. Keep unsupported devices
  explicit. Add battery persistence if the selected board requires it.
- Provide a bounded headless frame runner and an interactive host with live
  video, audio, input, reset, and clean shutdown. Declare host dependencies and
  build instructions. Headless evidence must remain reproducible.
- Reach a genuine native first frame early, then expand hardware coverage
  against reference evidence. First frame is an intermediate milestone only.

### M4: recover the complete selected game

- Build symbolic, named, commented 6502 assembly that reconstructs the entire
  original image with zero differing bytes. Data regions may use extracted
  assets with manifests; executable regions require recovered source.
- Recover readable native C for every executable routine, including reset,
  interrupts, sound code, banked/RAM code, computed targets, and restart paths.
  Preserve entry/exit registers, flags, stack effects where observable, ordered
  bus operations, and hardware waits. Register-shaped C can be an intermediate
  recovery form; finish with readable functions, names, and explanations rather
  than a generated instruction loop or an opcode switch.
- Track code bytes with a static plus dynamic census, not only routine counts
  or traced bytes. Account for executable bytes no scenario reaches through
  targeted routine scenarios and explicit evidence. Do not call unreachable
  or uncovered code verified merely to improve a percentage.
- Keep routine-level headers and provenance keyed to original addresses/banks.
  Keep assembly, C, data, and the routine census mutually consistent.

### M5: behavioral verification and playable native delivery

- Compare each recovered routine against the original on real and curated
  entry states: outputs/clobbers, flags/stack, changed memory, and ordered
  observable bus operations. Verify computed dispatch and side effects rather
  than replacing them with permissive stubs.
- Run all recovered game C together through reference-backed lockstep/replay
  scenarios. Account explicitly for instruction fetches, dummy operations,
  interrupts, and DMA when pairing observations; unmatched events and skipped
  intervals are unverified, not matches. Avoid replaying live I/O side effects
  twice when constructing the comparison harness.
- Run the standalone native app through deterministic reset-to-gameplay,
  menus, representative levels/modes, transitions, death/retry, pause, and
  completion paths present in the chosen title. Include save/reload if relevant.
  Compare live frame pixels, audio samples, RAM and important event boundaries
  over declared horizons, with exact or explicitly justified measurable criteria.
- Resolve the first real divergence before broad reruns. Keep reference tools
  bounded, report expected run lengths, monitor processes, and clean them up.
- Demonstrate production execution uses registered native routines throughout.
  No interpreter fallback, captured audiovisual playback, or title-specific
  hardware stubs may participate in the qualifying run.

## Deliverables and evidence

Keep the NES library, written NES game contract, game recovery project, data
extraction/rebuild scripts, reference adapter, comparison tools, deterministic
scenario suite, native host, and exact run commands in the repository.
Choose tool names to fit existing conventions; document real runnable commands
instead of leaving placeholder invocations in the final handoff.

Create a qualification report under `docs/nes-games/<title-slug>/` with separate
verdicts for assembly identity, routine behavior, coverage, hardware, native
playback, audiovisual comparison, and timing/pacing. Record image hash, source
revision, build identity, reference identity/settings, scenario names/horizons,
code-byte denominators, errors/skips, and evidence paths. Large binary captures
stay in an ignored local artifact directory such as `build/nes-evidence/`.
Require positive counts so an empty trace or zero-frame comparison cannot pass.

## Definition of done and execution policy

This goal is achieved only when the chosen game's symbolic assembly rebuilds
byte exact, its whole executable-code census has the required C and evidence,
the declared full-game scenarios pass behavioral and audiovisual comparisons,
and a native app plays the recovered C with live picture, sound, and controls.
Report timing/profile approximation separately from behavioral verdicts.
Keep all limits and untested cases visible; qualification applies to the
selected image, hardware configuration, and measured scenarios.

Use focused regression tests and reference comparisons for each change, then
the complete qualifying suite for the final revision. Include meaningful
failure-injection checks for the verification gates. Do not report success from
compilation, synthetic tests, or reference-emulator playback alone.

Continue autonomously through implementation, integration, and fixes. Do not
stop after adding more helpers, writing plans, reaching boot, or producing
scaffolding. Do not mark completion while required stages remain unimplemented,
uncovered, divergent, skipped, or unverified. If a required ROM, tool, or
human-controlled input is unavailable, name the exact missing prerequisite,
complete independent work, and leave the goal unachieved with a precise handoff.

Do not merge, push, publish, spend money, fetch commercial game assets, or
change unrelated console implementations as part of this prompt. Enhanced
gameplay, widescreen, extra regions, extra mappers, Famicom Disk System, and
expansion hardware are subsequent goals unless the selected title requires
them. A first title's successful run does not establish general NES parity.
