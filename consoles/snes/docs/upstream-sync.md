# Compatible upstream port, 2026-11-12

nativekit's shared contract is **1.1.0** and its SNES contract is **2.19.0**.
This is the existing register-level console with compatible upstream additions
and later correctness fixes. Games continue to link `nshvc` and enter through
`nshvc_main` or the low-level power/frame interface.

## Source revisions

The fork's common ancestor is `bc14c178119f78f986d19073a80e53de7ca30276`.
The hardware port uses upstream's last register-level console,
[`exact-console-3.0`](https://github.com/jeff-strutb/nativeshvc/tree/1c66ebb1fe638206fe26f04dbef1958ad3a28875).
The compatible PPU and compositor changes were merged through `d64a524`,
retaining CPU drawing; presentation retains the compatible interlace-description
cache from the earlier console implementation. The generic resampler and split
GPU backends carry the corresponding fixed-point/full-colour updates.
Original author and licence notices remain in place.

The later [upstream engine](https://github.com/jeff-strutb/nativeshvc/tree/f8f8fbad7e505dd9a70a94ff09384b3c875b74db)
was API 6.2 when checked. Its `nshvc_game`/`nshvc_run`, threaded rendering,
voice API, shaders and mesh APIs are a separate architecture. Its conversion
guide does not describe this fork's interfaces.

## What was ported

| Group | Imported behavior |
|---|---|
| Windows and input | Missing text/keyboard hooks; existing fullscreen, DPI and GL event-pumping fixes were already present and retained |
| Resampling | Fixed-point output phase and ring indexing, with the same signal-quality checks |
| Native controls | Text entry on iOS, keyboard presence, pointer interception, key rebinding/names, overlay updates |
| Pause | Machine/time and sound pause; hooks/input/overlays continue; blurred/tinted backdrop on software and GPU backends |
| Windows on larger screens | Open edges, mapped edges, explicit screen columns; software, compositor and Mode 7 paths agree |
| Canvases | Full-colour texels; arbitrary screen-canvas resolution (`k = 0`); presented-size query; updated GPU shader layouts |
| Cartridge access | Optional direct chip memory, mapping invalidation, next-change deadlines and register-only write hooks |
| Scheduling | Lazy PPU state/STAT77, clock-derived automatic joypad reads, safe event-directed waits, bulk HDMA with chip synchronization |
| Sound | Boot-ROM state machine, port observation ordering, sample-at-a-time DSP with RAM/read/write dependencies preserved |
| Diagnostics | Coroutine stack canaries; atomic-heartbeat stalled-game watchdog that tolerates deliberate host inactivity and sleep |
| Headless | Opt-in `NSHVC_PRESENT=1` and timing logs, keeping existing CPU pictures/shots/reference checks |

## Compatibility choices

- Software rendering, CPU picture access and reference comparisons remain
  supported. The default window still fits the picture at its aspect ratio.
- `nshvc_set_cpu_costs(0)` continues to leave SPC700 access timing intact.
  Upstream's change to that existing call was not adopted.
- New struct fields are appended. Zeroed hooks/chip descriptions retain their
  old behavior. Shared kit code contains no console-specific types.
- Existing custom POSIX hosts receive weak defaults for new optional host
  functions; they may override them without rewriting their old host adapter.
- `NSHVC_WATCHDOG=N` uses wall-clock seconds without machine/frame-hook progress;
  the default is 30 in both interactive and headless runs. `0` disables it.
  Intentional host suspension resets its grace period. A timer callback delayed
  over a second also grants a fresh deadline rather than treating system sleep
  or prolonged scheduling suspension as a game stall. Custom hosts using the
  no-op timer default provide their own stall detection.
- No PSP/GE API, raw GPU texture/device loan API (upstream 2.22), GPU-only
  rendering policy or modern engine API was imported. Those require a different
  renderer/consumer contract and are not advertised by this fork.

The local minor number records the imported additive interface set, not full
conformance to every policy of an upstream release with that number.

## Validation

Fresh builds used Apple clang because the installed profile GCC could not
link against the current Xcode SDK. Tests ran from `build/upstream-sync`:

- `kit-check`, including software pause/tint/overlay/resume checks, passed.
- `snes-upstream-check` passed: input/rebinding, window masks, full-colour
  canvases, direct memory, pause, boot uploads, waits, HDMA, scaling and fatal
  paths. Bulk/stepped HDMA: **0 of 48 frames differ**.
- Full ROM matrix: **0 new failures in 1,194 cases**, **56 existing known
  differences**, and **0 known cases unexpectedly passing**.
- DSP normal seeds 1–4: **0 differing samples, voice states, DSP reads or RAM
  reads**. RAM-write seed 1 and edge seed 1 were also exact against pinned bsnes.
- Sound-driver comparison: **3,214 key events, 0 differ**, largest shift 14
  samples, within the existing 32-sample allowance.
- Canvas and presented-picture checks passed. Large-screen check:
  **474,824,704 pixels compared, 0 failures**.
- NES checks, standalone SNES consumer and Mega Drive consumers passed. The
  Mega Drive native consumer repeated identically across 68 frames and caught
  its existing three planted errors.
- Separate negative controls caught corrupted stack canaries, stale direct
  mappings, disabled pause advancement and missing overlay-cache invalidation.
  The new watchdog lifecycle test failed before its fix and passes afterward;
  the corresponding stall test still exits 70.
- Metal, OpenGL and Vulkan renderer libraries and generated shader builds
  compiled. The iOS/Metal library and Windows/OpenGL cross-library also
  compiled. Windows executable configuration was blocked by the installed
  cross-toolchain's missing `mcfgthread` library; no Windows link/run is claimed. Runtime Metal/OpenGL comparisons could not open a renderer in this
  environment; GPU output parity is not claimed. No Windows/Linux GUI or real
  mobile-device run is claimed.

Run `mise run test:snes-upstream`, or configure a chosen build and run
`cmake --build BUILD --target snes-upstream-check`. The full existing ROM/DSP
checks still require the pinned asar and bsnes tools. See
[tests/README.md](../tests/README.md) and
[lazy-console-design.md](lazy-console-design.md).
