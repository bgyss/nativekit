# Upstream synchronization: investigation and proposed scope

Status: approach 1 approved by Brian on 2026-11-12; implemented; validation is recorded in the port record.

## Sources and baseline

- nativekit inspected at `880517d`; its SNES header and contract are version 2.10.
- Common ancestor with the locally available upstream history:
  `bc14c178119f78f986d19073a80e53de7ca30276`.
- Live upstream main checked through GitHub on 2026-11-12:
  [`f8f8fbad7e505dd9a70a94ff09384b3c875b74db`](https://github.com/jeff-strutb/nativeshvc/commit/f8f8fbad7e505dd9a70a94ff09384b3c875b74db),
  API 6.2 (instanced meshes).
- Upstream's annotated `exact-console-3.0` tag points to
  [`1c66ebb1fe638206fe26f04dbef1958ad3a28875`](https://github.com/jeff-strutb/nativeshvc/tree/1c66ebb1fe638206fe26f04dbef1958ad3a28875).
  The tag explicitly identifies this as the last register-level console before
  the native engine. The tag and its target were verified through GitHub.
- The local `upstream/main` ref is older (`3800784`, API 4.7). It must not be
  mistaken for current main. Local history was used for the console-era diffs;
  the current README, conversion guide and header were read from GitHub at the
  pinned 6.2 revision.

Direct git access was unavailable: the workspace prohibits writes to
`.git/FETCH_HEAD`, and a separate HTTPS clone could not resolve github.com.
The connected GitHub service supplied the live verification. No remote refs
were changed.

## What changed

The missing documentation describes two different architectures. nativekit
extracts the register-level console into a console-independent kit. Modern
upstream replaces that console with a game engine. Its API is not an additive
update to the APIs this repository currently provides.

| Area | nativekit today | Upstream 6.2 | Migration consequence |
|---|---|---|---|
| Entry and time | Cartridge, reset entry, bus accesses, interrupts, console clock; `nk_console` and `nk_main` | `nshvc_game`, `nshvc_run`, fixed-rate `update`, interpolated `draw` | Requires a new execution contract and converted consumers |
| Concurrency | Deterministic console schedule using coroutines | System, game, render and audio threads, plus jobs | Cannot replace the kit loop without reviewing MD's contract |
| Picture | Console PPU plus software, GL, Metal and Vulkan paths | Native window-resolution geometry; Metal, Vulkan, D3D12; meshes in 6.2 | New picture representation, backend interfaces and tests |
| Audio | SPC700 execution, APU ports and timed DSP | Game music callback on audio thread; voice/sample API and message queue | Existing sound-driver integration must be converted |
| Input | Kit maps native devices to console controllers | Devices exposed directly; game owns bindings | Preserve existing mappings for console consumers |
| Saves and assets | Battery RAM and console dumps/states | Native save files and packaged assets | Different lifecycle and persistence interfaces |
| Verification | Register, memory, pixel and sample comparisons | Feature scenes; converted-game audio comparison; visual inspection | Keep existing console gates; engine evidence is a separate claim |
| Decompilation guide | Companion C runs through cartridge/bus interfaces | Companion C is subsequently converted to engine calls | Copying the new guide alone would instruct consumers to use missing APIs |

Current upstream references:
[README](https://github.com/jeff-strutb/nativeshvc/blob/f8f8fbad7e505dd9a70a94ff09384b3c875b74db/README.md),
[public header](https://github.com/jeff-strutb/nativeshvc/blob/f8f8fbad7e505dd9a70a94ff09384b3c875b74db/include/nshvc/nshvc.h),
[conversion guide](https://github.com/jeff-strutb/nativeshvc/blob/f8f8fbad7e505dd9a70a94ff09384b3c875b74db/docs/conversion-guide.md).

Upstream explicitly reports that a whole game has not yet been run on the 6.x
renderer. Its 116 feature scenes have four documented Mode 7 differences by
design. These are upstream's evidence limits, not nativekit validation results.

## Changes relevant to the existing architecture

The common ancestor to `exact-console-3.0` changes 99 files, with 24,640 added
and 13,554 removed lines, including generated shaders. This is a substantial
port even without the engine rewrite. Candidate groups, to verify individually:

| Group | Upstream work | Local destinations |
|---|---|---|
| Generic host fixes | Windows fullscreen and per-monitor DPI; GL shader-build event pumping | `kit/src/host/windows/`, `kit/src/render/gl/` |
| Generic audio | Fixed-point resampler phase and queue indexing | `kit/src/dsp/resample.c`; SNES resampler checks and kit consumers |
| Native interaction additions | Text input, keyboard presence, pause and pointer integration | `kit/include/nk/host.h`, host implementations, kit input/app callbacks; SNES adapter and header |
| SNES extensions | Wider window masks, window mapping/columns, full-colour canvases, arbitrary canvas scaling and GPU-held pictures | `consoles/snes/include/nshvc/`, PPU/compositor, SNES renderer extensions and associated kit backend hooks |
| SNES scheduling and sound | Lazy scheduling, event-directed waits, bulk HDMA, state-machine boot protocol, sample-at-a-time DSP | `consoles/snes/src/console/`, `src/ppu/`, `src/dsp/` |
| Renderer policy | Production GPU picture path; CPU reference rendering moved to tests; PSP removed in upstream 3.0 | Both render trees, build defaults, reference tests; requires an explicit decision about nativekit's headless software path |

Some upstream changes are already present through independent ports. In
particular, macOS mouse-button press/release tracking and its kit check already
exist (`f195373`). Do not apply those changes twice. The fixed-point resampler
is not present: the local implementation still uses double phase arithmetic.

## Approaches

1. **Update the existing native-console architecture (recommended).** Port
   applicable console-era fixes and additive APIs, using `exact-console-3.0`
   as the reference and preserving nativekit's existing public promises.
   Reconcile removed CPU-renderer behavior explicitly rather than adopting the
   3.0 version number wholesale. Document modern upstream as a distinct engine
   architecture. This serves the current SNES, MD and NES work, but does not
   implement the modern engine API or its conversion guide.
2. **Add the modern engine alongside the consoles.** Keep current console
   targets and add the pinned upstream engine under an opt-in target. Initially
   isolate its private host and renderer headers, generated files and build
   options; prove that an engine consumer and existing console consumers can
   configure in the same parent project before extracting shared services.
   The engine and console expose overlapping `nshvc_*` names and cannot simply
   be linked into one executable. This gains current upstream features while
   retaining consumers, at the cost of substantial duplicated infrastructure.
3. **Replace the SNES console with the modern engine.** Adopt upstream's new
   execution, graphics, sound and packaging contracts, and convert games.
   Review the kit's purpose and MD integration as a separate architectural
   change. This follows upstream most closely but breaks existing SNES consumers
   and conflicts with the repository's current native-console goal.

Brian selected approach 1. Preserve the software renderer and existing headless
and deterministic-console contracts; GPU-only policy changes are not adopted.

## Proposed implementation order for approach 1

1. Record a fresh baseline using kit checks, SNES unit/reference checks and the
   existing consumer integration. Retain deterministic MD and NES smoke gates.
2. Port compatible generic host and resampler changes first, with meaningful
   regression checks. Preserve kit naming and the console-free include boundary.
3. Port SNES scheduling/APU/DSP corrections as separate reviewable batches,
   bringing the corresponding upstream checks and reference cases along.
4. Add requested console extensions with zero-initialized defaults preserving
   current behavior. Amend backend hooks only when required by those extensions.
5. Resolve the software-renderer contract before any GPU-only upstream change.
   Keep reference drawing in tests if the resulting policy permits it.
6. Update local contracts, version declarations, platform evidence and usage
   docs to describe the implemented behavior. Preserve historical decompilation
   guidance where it still matches this repository. Add a clear upstream-status
   link to the root README.
7. Run headless checks and supported Metal checks; run consumer integration and
   MD/NES checks after shared-kit edits. Report untested Windows, Linux and
   mobile behavior explicitly. Record the imported commit for each batch.

## Acceptance criteria and limits

- Existing console consumers continue building and running through their
  current entry points unless an explicit major-contract change is selected.
- Shared kit code contains no SNES-specific engine types or behavior.
- Every imported behavioral fix has its appropriate deterministic check;
  renderer output remains subject to the repository's reference discipline.
- Docs describe shipped local APIs, with separate links for modern upstream.
- No commercial game's recovered source or ROM is imported from the upstream
  examples into this repository.
- This investigation is not a complete per-commit implementation inventory.
  The groups above identify the scope; each port still needs dependency review,
  checks and validation. The initial investigation changed no runtime code;
  subsequent implementation validation is in the port record.

Approved direction: preserve and update the native-console architecture.
