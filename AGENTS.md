# Repository Guidelines

## Project Structure & Module Organization

`kit/` contains console-independent application, host, input, audio, coroutine, and rendering code. Public interfaces are in `kit/include/nk/`; implementation, checks, and contracts are in `kit/src/`, `kit/tests/`, and `kit/docs/`. `consoles/snes/` contains the SNES hardware, renderer, shaders, examples, and tests. `consoles/md/` contains the native Mega Drive implementation and exploratory oracle, trace, and synthetic-ROM tools. Shared tooling is in `tools/`; plans and specifications are in `docs/superpowers/`; generated builds and toolchains belong under `build/`.

## Build, Test, and Development Commands

Run commands from the repository root. `nix develop` enters the compiler and tooling environment; run `mise trust` once to authorize repository tasks.

- `mise run build:null` builds headless targets; `mise run build:macos` builds the macOS Metal host; `mise run build` runs the macOS-oriented build matrix.
- `mise run tools` builds pinned SNES reference tools; `mise run tools:md` builds candidate Mega Drive tools.
- `mise run test:kit`, `mise run test:rom`, and `mise run test` run kit checks, SNES reference comparisons, and the full checks with consumer integration.
- Run focused Mega Drive checks with `mise run` and one of `test:md-console`, `test:md-io`, `test:md-z80`, `test:md-sound`, `test:md-bus`, `test:md-interrupt`, `test:md-vdp`, `test:md-dma`, or `test:md-render`. `mise run test:md-rom` compares synthetic cases with the candidate core.
- Run exploratory tooling checks with `mise run` and one of `test:md-toolchain`, `test:md-oracle-smoke`, `test:md-trace`, `test:md-features`, or `test:md-survey`.

## Coding Style & Naming Conventions

Use C11, four-space indentation, snake_case, and put function opening braces on their own lines. Kit interfaces use the `nk_` prefix; console interfaces use `nshvc_` or `nmd_`. Keep console-specific concepts out of `kit/`. No repository-wide formatter or linter is configured; avoid unrelated reformatting.

## Testing Guidelines

Checks are C executables and Python scripts, commonly named `*_check.c` or `*_check.py`. Add checks beside the affected module and wire them into its runner. New checks should catch a planted error, stay deterministic, and report known reference differences explicitly. Mega Drive fixtures use synthetic cartridges; commercial ROMs are not stored here. See `consoles/snes/tests/README.md`, `consoles/md/tests/README.md`, and `kit/docs/contract-discipline.md`.

## Commit & Pull Request Guidelines

Use focused commit subjects such as `nativemd: ...`, `Plan: ...`, or `Docs: ...`. Pull requests should explain behavior changes and affected contracts, list commands run and validation skipped, and link relevant plans or issues. Include screenshots for visible rendering changes. Preserve upstream attribution and license notices.
