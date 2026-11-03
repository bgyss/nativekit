# Repository Guidelines

## Project Structure & Module Organization

`kit/` holds console-independent application, host, input, audio, coroutine, and rendering code. Public interfaces are in `kit/include/nk/`; implementation, checks, and contracts are in `kit/src/`, `kit/tests/`, and `kit/docs/`. `consoles/snes/` contains nativeshvc headers, hardware and renderer code, shaders, examples, docs, and tests. `consoles/md/` contains the nativemd library, exploratory Mega Drive/Genesis oracle and trace tools, docs, and tests. Shared tooling is in `tools/`, plans in `docs/superpowers/`, and generated builds and tools in `build/`.

## Build, Test, and Development Commands

Run commands from the repository root:

- `nix develop`: enter the compiler and tooling environment; `mise trust` authorizes repository tasks once.
- `mise run tools` and `mise run tools:md`: build pinned SNES tools and candidate Mega Drive tools, respectively.
- `mise run build:null`, `mise run build:macos`, and `mise run build`: build headless, macOS Metal, and the macOS-oriented matrix. iOS needs Xcode and its SDK; Linux is untested.
- `mise run test:kit`, `mise run test`, and `mise run test:rom`: run kit checks, the full scripted checks with consumer integration, and SNES reference comparisons. Prepare required builds and tools first.
- `mise run test:md-console`, `mise run test:md-bus`, `mise run test:md-interrupt`, `mise run test:md-vdp`, `mise run test:md-dma`, and `mise run test:md-render`: check console, bus, VDP/DMA, scheduler, interrupt, and render paths. `mise run test:md-rom` compares shared synthetic cases with the candidate core.
- `mise run test:md-toolchain`, `mise run test:md-oracle-smoke`, `mise run test:md-trace`, `mise run test:md-features`, and `mise run test:md-survey`: check the exploratory toolchain, synthetic capture runner, trace ABI, feature extraction, and survey reporting.

## Coding Style & Naming Conventions

Use C11, four-space indentation, snake_case, and put function opening braces on their own lines. Kit interfaces use the `nk_` prefix; SNES and Mega Drive interfaces use `nshvc_` and `nmd_`. Keep console-specific concepts out of `kit/`. No repository-wide formatter or linter is configured; avoid unrelated reformatting.

## Testing Guidelines

Checks are C executables and Python scripts, usually named `*_check.c` or `*_check.py`. Add them beside the module and wire them into its runner. Demonstrate a planted-error catch; keep checks deterministic and report known reference differences. See `consoles/snes/tests/README.md` and `kit/docs/contract-discipline.md`.

## Commit & Pull Request Guidelines

Use focused, descriptive commit subjects; recent examples include `nativemd: ...` and `Plan: ...`. Pull requests should describe behavior changes and affected contracts, list commands run and skipped validation, link relevant plans or issues, and include screenshots for visible rendering changes. Preserve upstream attribution and license notices.
