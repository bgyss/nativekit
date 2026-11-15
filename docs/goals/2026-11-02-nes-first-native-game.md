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

## Restart baseline and first actions

**Current execution goal, updated 2026-11-15.** Resume the existing Dragon
Warrior (USA) (Rev A) recovery. Do not restart title selection or treat the
reference-capture infrastructure as the finished native game.

| Item | Selected value |
| --- | --- |
| Image SHA-256 | `676b89d76cf9e83eed76c0a67f81da906916f0bce321cf3a9a39ac78bc9c0845` |
| Image / hardware | iNES, NTSC, mapper 1, SAROM/MMC1B2; 64 KiB PRG, 16 KiB CHR, 8 KiB battery RAM |
| Platform work | `consoles/nes/` in nativekit |
| Existing private title repo | Sibling `../dragon-warrior-nes`; read its README and qualification ledger first |
| Reference source | Brian's `bgyss/MesenCE` fork, commit `2be9fe7bbca16e6187b931ad3be2a16854fd0edf`, pinned in `tools/toolchains/mesen.json` |
| Reference launch | Native `mise run capture:nes` command; no GUI, SDL, .NET or Computer Use |

Start from the repository root:

```sh
nix develop
mise trust
mise run env:sync
mise run tools:nes
mise run capture:nes doctor
mise run test:nes
mise run test:nes-capture
```

Nix supplies the native build tools, Python, uv and mise. The committed
`pyproject.toml` and `uv.lock` define a standard-library-only Python environment;
NES mise tasks run through `uv run --locked` using the shell's Python, with
managed Python and automatic Python downloads disabled. Existing host tools
can run the same mise commands when Nix is unavailable. The source setup reads
an existing sibling MesenCE checkout without modifying it; otherwise it fetches
the exact fork commit. `--source /path/to/MesenCE` or `NNES_MESEN_SOURCE` can
select another local source checkout; neither changes the pinned revision.

Read [the capture guide](../../consoles/nes/docs/capture.md) and use its exact
reset/Start/profile commands with a locally supplied `DW1_IMAGE`. The new source
pin reproduced all 56,713 reset events and 241,375 Start-route events from the
existing evidence, including values and timestamps. A new 120-frame no-input
profile recorded 2,620,456 typed callbacks and normalized successfully.
These are reference capture continuity checks, not native-game parity.

The private ledger still reports incomplete PPU rendering, APU channels, DMC
DMA, host/frame execution, battery persistence and gameplay. The native input
provider now samples upcoming frames at NTSC scanline 241, cycle 0, matching
MesenCE's default input scanline; a fresh headless capture measured those
callbacks within six CPU cycles. `nnes` now sets sprite-0 hit from opaque
background/sprite overlap while rendering remains unimplemented.

A fresh 40-frame two-Start run still matches all 1,550 typed bus operations
from the second `$BDB4` entry through fixed-bank `$C9F5`; the `$FCDB` SP and
12-byte `$C9F5` stack window match Mesen. The 9,897-operation route through
`$CA08` matches after one extra native `$FF78` poll pair before NMI; NMI entry
is 2.4 cycles later. The `$C9F5`, `$FC98`, and `$FCAD` phases are within 0.5,
1.9, and 0.4 cycles of Mesen, and the `$CA08` stack matches. The native route
continues through `$F678/$F842/$FC4D`, `$C17A`, and `$C1B9`. The `$C17A` entry
stack/registers match, and the first fill continuation matches 12,111 typed
bus events through the `$C1C8` return after aligning one reference poll pair.
The standalone `$C1B9` call matches 2,910 bus events and 8,824 cycles; its
entry is 3.3 cycles from Mesen. F842's final wait and BRK signature `$01`
dispatch through `$FD3A/$FCBD/$FCEC` to missing bank-0 `$AA7E`. The second
C17A wait and full F842 dispatch span remain unpaired. The second Start read
is 102.1 cycles later natively within its 128-cycle bound. `$81B8` is an
internal branch in bank-1 `$81A0`, not a missing routine. The tested `$BD36`
branch clears OAM while `$93 == $FF`; its active animation stream remains
uncompared. Continue from the private ledger, preserving bank-aware routine
identities and the SAROM adapter. Recover bank-0 `$AA7E`, then qualify the
remaining C17A route and active animation branch. Alternate `$FD00`, rendering,
audio, and gameplay remain open.

The pinned MesenCE 120-frame no-input profile has 139.44-cycle gap RMSE,
+1.04% mean bias, and 2.80% p95 absolute relative error. A 40-frame Start
profile and a combined 160-window fit remain exploratory and are not promoted;
their activity mix does not support one title-wide access profile.

Maintain independent platform and game commits. Recovered game source belongs
only in the private title repository. A thread touching both repositories needs
write access to both; if using a different nativekit worktree, pass that path
as `-DNATIVEKIT=...` when building the game. The ROM and large captures remain
local, ignored inputs/outputs. A fresh checkout must regenerate captures using
the documented commands; do not depend on chat history or ignored build files.
The core source, patches, host, collector, image and output hashes are retained
by each capture manifest.

## Sources of truth

Read these before planning or editing:

- [SNES game contract](../../consoles/snes/docs/contract.md), especially the cartridge/console
  boundary, ordered bus accesses, waits, native dispatch, and timing model.
- [SNES decompilation playbook](../../consoles/snes/docs/snes-decomp-learnings.md), especially the
  preservation tiers, early integration, per-call and lockstep oracles,
  coverage census, and timing without instruction annotations.
- [NES implementation and limitations](../../consoles/nes/README.md).
- [NES machine interface](../../consoles/nes/include/nnes/machine.h) and
  [cartridge interface](../../consoles/nes/include/nnes/nes.h).
- [NES cost-profile contract](../../consoles/nes/docs/cost-profile.md).
- [Existing NES foundation plan](../superpowers/plans/2026-11-02-nes-foundation.md).

Translate SNES-specific details to the 2A03/2A07 and NES hardware. The SNES
contract's claims of hardware parity do not establish NES parity. Preserve
the distinction between bit-exact assembly, behavior-verified C, and any later
enhanced mode.

## Target and initial decisions

The title, revision and board are selected in the restart baseline above.
Verify the supplied image hash and the private title manifest before game work.
Record its local path in ignored local configuration or `DW1_IMAGE`; keep
machine-specific paths out of checked-in documents. If the image or private
repo is unavailable, identify that exact missing input and continue independent
platform work. Do not choose a different game.

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
  Fix actual failures before treating the foundation as usable. The current
  synthetic example computes its routine count from the array; preserve that
  fix rather than assuming the historical registration defect remains.
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
  region, and capture schema. The pinned native MesenCE capture path has now
  been exercised on this image; use `mise run capture:nes` and retain its
  manifests. Extend reference qualification for each new scenario rather than
  assuming the demonstrated reset/Start spans cover gameplay.
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
- Preserve and qualify the existing bank-aware dispatch identities, which
  distinguish the executable content mapped at a CPU address.
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

Keep the NES library, written NES contract, generic capture tools and native
host in nativekit. Keep game recovery, extraction/rebuild scripts, title-specific
comparisons, scenarios and the title ledger in the private game repository.
Commit the exact run commands and small textual evidence summaries.
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
