# Headless NES reference capture

Run captures from the nativekit root. No Computer Use, Mesen app, display,
SDL installation, .NET SDK, desktop settings, or audio device is involved.

```sh
nix develop
mise trust
mise run env:sync
mise run tools:nes
mise run capture:nes doctor
mise run capture:nes run --rom /path/to/game.nes --frames 120 \
  --trace --out build/captures/nes-reset
mise run capture:nes validate build/captures/nes-reset
```

`--out` must be new or empty. A failed run remains available with
`manifest.json` reporting `ok: false` and an error, plus stdout/stderr logs.
Use a new directory for the next attempt. Exit zero means collection and
validation succeeded; it does **not** qualify emulator fidelity, the native
game, timing, mapper behavior, or the decompilation.

## Pinned reference and build

[`tools/toolchains/mesen.json`](../../../tools/toolchains/mesen.json) pins
Brian's MesenCE branch at `2be9fe7bbca16e6187b931ad3be2a16854fd0edf`.
Its formal-verification changes are retained. Setup uses the sibling
`../MesenCE` Git checkout when present, or fetches the pinned fork otherwise:

```sh
python3 tools/toolchains/setup_nes.py --source /path/to/MesenCE --jobs 8
```

The local checkout is read only. Setup imports the commit into ignored
`build/toolchains/src/mesen-source`, archives it into a separate patched tree,
and builds `build/toolchains/mesen/nnes-mesen`. It refuses modified or added
source files and rebuilds object caches if their recorded hashes do not match.
It does not install anything on the host or change the original branch.
The Nix shell supplies native C/C++ compilers (system Xcode on macOS), make,
Python, uv, Git and tar/patch. NES mise tasks use the committed `pyproject.toml`
and `uv.lock`; Python 3.11+ comes from the shell, with automatic interpreter
downloads disabled. Run `mise run env:sync` after a fresh checkout. A host
installation of those tools can run the same tasks without Nix.
macOS is exercised; Linux is implemented but requires separate qualification.

The executable links the upstream emulator core, software rendering and
recording, Lua and archive utilities. It excludes `InteropDLL/`, `Sdl/`,
`MacOS/`, and `Linux/` desktop backends. Preserve MesenCE's GPL notices in the
source tree; this test dependency and its binary are not shipped with nnes.

Local changes to the pinned reference are deliberately narrow:

- The existing typed-operation callback patch preserves read/write, opcode,
  dummy and DMA classifications. The pinned debugger does not deliver operand
  fetch callbacks; those cycles remain in profile gaps as native work.
- Capture shutdown is synchronous and owned by the native host. Lua errors
  fail the run, and the NES loop stops at the current instruction's end.
- `setInput` uses the documented three-argument Lua stack. Upstream pads it
  to four entries, causing the port argument to be read from the wrong slot.
  The actual-controller test detects this: setting player 2 previously cleared
  player 1 instead of addressing player 2.

The patch set and host source are hashed in `identity.json`. `doctor` and every
capture reject stale or modified toolchains; run `tools:nes` after changes.

## Scenarios and input

| Scenario | Behavior |
| --- | --- |
| `reset` (default) | Cold boot, both standard pads released, fixed frame budget |
| `start` | Cold boot and hold player 1 Start on a specified frame range |
| `gameplay` | Cold boot with a supplied frame-to-port input schedule |
| `profile` | Run the typed profile collector; `--frames` is capture length and `--warmup` defaults to 120 |
| `script` | Run one or more trusted external Lua collectors; they must call `emu.stop(0)` before the frame ceiling |

```sh
mise run capture:nes run --rom /path/to/game.nes --scenario start \
  --frames 300 --start-frame 40 --start-frames 6 --out build/captures/start

mise run capture:nes run --rom /path/to/game.nes --scenario gameplay \
  --frames 600 --input /path/to/inputs.json --out build/captures/gameplay
```

Input matches the MD runner's convention: one-based frame keys, ports `"1"`
and `"2"`, and uppercase button lists. Each frame is a complete snapshot;
unspecified frames/ports are released. To hold a button across frames, list
it on each frame. For example:

```json
{"40":{"1":["START"]},"41":{"1":["START"]},"90":{"1":["RIGHT","A"]}}
```

Standard buttons are A, B, SELECT, START, UP, DOWN, LEFT, RIGHT. Opposing
directions are rejected. Input is injected at Mesen's `inputPolled` callback;
this follows the reference's poll timing, not host keyboard timing. `script`
collectors own their input so the session does not overwrite it.

All runs use NTSC, zero-filled power-on RAM, fixed CPU/PPU alignment, no mapper
randomization, standard pads, no HD packs or game-database overrides, default
2C02 palette, 48 kHz audio and no dynamic sample-rate adjustment. Fresh homes
provide empty initial battery RAM. Loading prior battery saves and PAL/Famicom
expansion devices are not yet supported.

## Artifacts and limits

Each successful run includes `manifest.json`, `scenario.json`, the actual
combined `session.lua`, snapshots of external collectors, and logs.
`artifacts/` includes PNG, 16-bit WAV, internal RAM, OAM, nametable RAM,
Mesen save state, flat JSON machine state, and a completion record.
`--trace` adds `bus.jsonl`, a streaming typed CPU callback trace with values,
CPU cycles, master clocks, frame indexes and a counted footer. This is the
debugger's callback stream, not a claim to expose every electrical bus cycle.

The JSON state/completion records describe the callback that requested stop.
Binary memory/save-state snapshots are taken once the current instruction
finishes. `native-result.json` records that later master clock. The PNG decodes
the PPU's current buffer, which can be partially drawn for mid-frame stops.
WAV is drained at the reference's audio flush boundaries; a sub-frame capture
may produce no audio samples. Use frame-based runs for visual/audio comparisons.

The manifest binds the image SHA-1/SHA-256, source commit, patch/host hashes,
executable hash, collector hashes, invocation, completion, and output hashes.
`validate` checks the recorded artifacts without needing the ROM or emulator.
It detects changed files, missing completion, malformed/reordered JSONL,
incorrect ROM identity, missing typed-trace footers, incomplete PNG chunks,
truncated compressed save states and missing WAV sample data.

`--timeout` is an external wall-clock watchdog (default 120 seconds), including
Lua load and cleanup. It kills the process group if necessary. Individual Lua
callbacks also have Mesen's five-second watchdog. `--max-events` bounds the
optional session bus trace (default 5,000,000 events); hitting a limit fails
instead of marking a truncated trace as successful. `--frames` is a strict
ceiling for external collectors. Long captures can be large; prefer focused
instruction-bounded collectors and explicit output directories.

## Dragon Warrior thread handoff

The authoritative full-game execution goal and restart baseline are in
[`docs/goals/2026-11-02-nes-first-native-game.md`](../../../docs/goals/2026-11-02-nes-first-native-game.md).
Read that document and the private title's current ledger before continuing.

Game code and collectors stay in `../dragon-warrior-nes`. Supply the selected
Rev A image (SHA-256 `676b89d76cf9e83eed76c0a67f81da906916f0bce321cf3a9a39ac78bc9c0845`)
locally; no game image or recovered source is added to nativekit.

The following commands assume `DW1_IMAGE` points to that image and the private
repo is a sibling. These are shell commands, with no emulator UI steps:

```sh
# Existing reset collector, through its $8178 target.
mise run capture:nes run --rom "$DW1_IMAGE" --scenario script --frames 120 \
  --lua ../dragon-warrior-nes/tools/capture-dw1-mesen-reset.lua \
  --expect-jsonl dw1-separate-mesen-reset.jsonl --out build/captures/dw1-reset

# Existing Start collector, including a 20,000-instruction continuation.
mise run capture:nes run --rom "$DW1_IMAGE" --scenario script --frames 120 \
  --lua ../dragon-warrior-nes/tools/capture-dw1-mesen-start-input.lua \
  --env DW1_POST_TARGET_EXECUTES=20000 \
  --expect-jsonl dw1-start-input-reset.jsonl --out build/captures/dw1-start

# Fresh typed profile, classifying SAROM battery RAM explicitly.
mise run capture:nes run --rom "$DW1_IMAGE" --scenario profile \
  --warmup 120 --frames 600 --env NNES_PROFILE_MEMORY_RANGES=6000-7FFF:cart_ram \
  --out build/captures/dw1-profile
consoles/nes/tools/nnes-profile normalize \
  build/captures/dw1-profile/artifacts/profile-raw.jsonl \
  --out build/captures/dw1-profile/normalized.jsonl
```

`DCOMP_ADAPTER_OUTPUTS` and the profile output directory are set automatically
inside each capture. `--env` accepts explicit `DW1_*` and `NNES_PROFILE_*`
collector settings. Inherited collector settings are cleared. Collectors run
with filesystem/OS access and networking disabled; only use trusted Lua.
Collector filenames and default schemas remain compatible with the private
project's verifiers. Frame ceilings, successful script stop, trace validation,
and output hashes are checked by nativekit; run the private semantic verifiers
as well before claiming a routine matches the original.

## Verification

```sh
mise run test:nes-capture    # builds reference, contracts + real synthetic ROM
mise run test:nes            # nnes hardware contracts + profile/capture CLI checks
```

The synthetic ROM exercises visible palette output, a pulse-channel tone,
NMI RAM updates and serial controller reads. Actual-core controls check repeat
determinism, Start delivery, typed operations, profile normalization, custom
Lua completion, script/callback errors, noncompletion, timeout, event limits,
missing output and corrupted artifacts. Commercial captures are local evidence,
not repository test fixtures.

On 2026-11-14 the existing Dragon Warrior reset and 20,000-instruction Start
collectors ran through this command. Every access object matched the old local
reference captures: 56,713 reset events and 241,375 Start events, including
values and timestamps. A fresh 120-frame no-input profile recorded 2,620,456
operations and normalized successfully. These checks establish continuity for
those spans; the private project's outstanding game/hardware differences remain.
