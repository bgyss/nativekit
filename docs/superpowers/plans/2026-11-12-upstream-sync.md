# Compatible Upstream Synchronization Implementation Plan

> Execute inline, in dependency order, with a fresh final code review.

**Goal:** Import applicable register-level nativeshvc updates while keeping
nativekit's current console consumers, deterministic loop and software renderer.

**Architecture:** Use upstream's console-era history as source, port hardware
changes under `consoles/snes/`, and adapt generic changes into `kit/`. Existing
public behavior takes precedence over upstream's later GPU-only and engine policies.

**Tech stack:** C11, Objective-C hosts, CMake, Python checks.

**Spec:** `docs/superpowers/specs/2026-11-12-upstream-sync-design.md`.

## Global constraints

- No engine rewrite, commercial-game source, or ROM import.
- Keep `nshvc_main`, `nk_console`, software headless rendering and existing input mappings.
- New public struct fields go at the end and zero initialization preserves behavior.
- Record imported revisions and explicit exclusions in the local docs.
- Work in the authorized checkout; git metadata is read-only in this environment.

## Review focus

- Pausing must not advance the machine or its sound, and must resume cleanly.
- Window masks must agree in software, compositor and Mode 7 paths.
- Shared host/input changes must not alter other consoles' defaults.
- Lazy execution must not miss writes, interrupts or sound-port handshakes.
- Headless checks must continue exercising real rendered output.

## Tasks

1. [x] Record fresh baseline: configure a separate `build/upstream-sync` tree;
   run kit checks, SNES checks and consumer tests before imports. Use Apple
   clang if the installed GCC cannot link against the current SDK.
2. [x] Import generic fixed-point resampling and compatible Windows host fixes;
   preserve host/input ABI. Run signal, queue and host build checks.
3. [x] Port additive console-era APIs and graphics behavior: window edges,
   mapped/explicit columns, text/keyboard presence, pause, pointer hooks,
   key bindings, overlay updates, full-colour and arbitrary-scale canvases.
   Bring corresponding upstream checks before implementations, observe missing
   behavior, then adapt kit and console boundaries until checks pass.
4. [x] Port lazy SNES scheduling and sound improvements, direct chip memory,
   register-only write hook, and applicable later correctness fixes. Import
   matching upstream checks first. Preserve CPU/reference rendering and untimed
   default promises; test waits, IPL uploads, chip IRQ and DSP output.
5. [x] Update contracts/version/build declarations and upstream port ledger.
   Document exclusions caused by preserving software rendering. Run kit,
   SNES/reference, MD/NES and consumer checks; run Metal where supported.
6. [x] Review the complete diff independently, fix material findings, and
   report validation and remaining platform limits.

## Execution record

- Baseline configure with profile GCC failed at its linker reading the Xcode
  SDK's arm64e.x1 text stubs; retrying with `/usr/bin/clang` and no `LIBRARY_PATH`.
- Git metadata is read-only; changes will be left reviewable in the checkout.

- Imported hardware through `1c66ebb`, compatible PPU/compositor through
  `d64a524`, and split backends/resampler through the corresponding console-era
  revisions. Imported tests were exercised against missing APIs before ports.
- Ruling: retain software rendering, aspect-fit presentation and the original
  S-CPU-cost toggle's independent SPC timing. GPU-only/GE/engine interfaces are
  excluded, as required by the selected compatibility approach.
- Ruling: watchdog defaults to 30 seconds in headless too, preserving the local
  no-hang guarantee for untimed polling; explicit 0 disables it. Custom hosts get
  weak defaults rather than mandatory new implementation symbols.
- Review found the watchdog could mistake iOS suspension for a stall. A mock
  inactive host failed (exit 70); host activity and missed-timer handling fixed
  it. Background/resume/sleep now pass; an actual stall still exits 70.
- Negative controls: disabled pause advancement and overlay invalidation in
  separately linked scratch copies; the new loop check rejects each (exit 1).
  Coroutine canary corruption and stale direct-memory mapping were also caught.
- Validation: kit/extension, SNES upstream, NES and both console consumer gates
  pass; the full 1,194-case SNES reference matrix has no new failures (56 existing
  known differences). DSP seeds 1–4, RAM seed 1 and edge seed 1 match bsnes.
- Metal/OpenGL/Vulkan, iOS/Metal and Windows/OpenGL libraries compiled; runtime GPU
  comparisons could not open a renderer on this host. No Windows/mobile device
  execution is claimed. Detailed evidence is in the port record.

- Windows executable linking could not be checked: the installed cross-toolchain
  is missing `mcfgthread`. Static-library cross-compilation passed.
