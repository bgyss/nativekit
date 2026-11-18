# NES headless capture implementation plan

**Goal:** Make NES reference captures reproducible shell commands, including the
existing private Dragon Warrior Lua collectors, without desktop automation.

**Architecture:** Pin Brian's MesenCE branch at
2be9fe7bbca16e6187b931ad3be2a16854fd0edf. Build only its native core,
utilities, Lua, and archive support with a small native host. Exclude SDL,
desktop interop, physical input, and .NET. Preserve typed-operation callbacks.
A Python CLI owns isolated run directories, scenarios, watchdogs, provenance,
and validation. Game-specific source and scripts stay in their private repo.

**Decision:** Brian authorized implementation of the preceding recommendation.
Brian subsequently directed using his local SDL3/formal-verification branch;
its current commit replaces the original 9a05146d baseline in this plan.
Use that local source rather than changing emulator lineage to the
separate mesen-agent fork. The native host provides the SDL-free path locally;
mesen-agent remains a future candidate requiring separate qualification.

## Constraints and review focus

- Never touch the user's installed Mesen or its settings/saves.
- Never reset an edited tool checkout or overwrite capture directories.
- Script errors, incomplete/truncated captures, and timeouts must fail visibly.
- Record exact image, scripts, source/patch identity, executable, and artifacts.
- Use deterministic NTSC, zero-filled RAM, fixed alignment, standard pads.
- A successful capture establishes collection, not game or hardware parity.

## Tasks

- [x] Add failing capture contract tests, including failure injection.
- [x] Add pinned source setup and native host, preserving operation types and
  making Lua failures and script-requested shutdown orderly.
- [x] Add `nnes-capture`: doctor, bounded frame/input scenarios, custom Lua and
  profile scenarios, manifests and artifact validation.
- [x] Wire mise tasks and the NES test runner; document private-game commands.
- [x] Exercise real synthetic captures twice, negative controls, the existing
  Dragon Warrior collectors if the selected local image is available, and NES
  regression checks. Record handoff evidence and remaining limitations.

## Verification, 2026-11-14

- `mise run test:nes-capture`: 9 CLI/validation checks and 7 actual-core test
  groups pass, including repeat-byte checks, input, audio, traces, profiling,
  Lua errors, timeout, event ceiling, missing output and tampering controls.
  Final review added object-cache attestation, exact toolchain input/stamp
  verification, structural PNG/MSS/WAV validation, independent player-2 input,
  button release and mid-frame stop controls.
- `mise run test:nes`: nnes contracts, synthetic cartridge and Python checks pass.
- Local Rev A SHA-256 matches the private title manifest. Existing reset and
  20,000-instruction Start collectors run successfully without GUI changes.
- Comparing every access object against the preexisting private evidence gives
  56,713 reset events and 241,375 Start events identical, including timestamps.
  The local report is `build/captures/existing-reference-comparison.json`.
- A fresh 120-frame no-input profile produces 2,620,456 typed operations and
  normalizes successfully; artifacts live in `build/captures/dw1-profile`.
- `otool -L build/toolchains/mesen/nnes-mesen` lists only libc++ and libSystem.
- Native snapshot stops are at instruction boundaries; callback JSON is sampled
  at the exact stop request. No claim of complete electrical bus coverage or
  emulator/native-game parity. Linux and PAL/expansion devices are unqualified.

See `consoles/nes/docs/capture.md` for the restarted NES/Famicom thread.

## Environment and restart goal

The Nix shell now includes uv, tar/patch and Linux clang alongside the existing
native build tools. `pyproject.toml` and `uv.lock` define the dependency-free
Python environment. `mise run env:sync` and the NES reference tasks use the
shell's Python through uv, with managed Python and interpreter downloads off.
The uv lock check, environment sync, 16 capture tests and existing NES suite
pass. `nix-instantiate --parse flake.nix` succeeds; launching `nix develop`
within this session is unverified because access to the Nix daemon socket is
blocked by the execution sandbox.

The current full-game execution prompt is
`docs/goals/2026-11-02-nes-first-native-game.md`. It retains the complete-game
definition of done, selects the existing Rev A project, and supplies the new
restart workflow. The old plan path redirects to it. Root README and AGENTS
link the goal and guide so a new thread can continue without conversation history.
