# Local test toolchains

Run `nix develop`, then `mise run tools` for the existing reference tools.
`mise run tools:md` separately builds the candidate BlastEm libretro core and
the 68000 and Z80 vasm assemblers under `build/toolchains/`. These tools are
local test dependencies and are not shipped with nativekit. Run
`mise run test:md-toolchain` checks the assemblers with synthetic instructions.
`mise run test:md-features` checks evidence-aware extraction from bounded traces;
`mise run test:md-survey` checks sanitized survey output and aggregate statuses.

The vasm pin is [Leffmann's Git source at commit
`685a87e5ed14285350ccdb6581c9771bc8df6c7d`](https://github.com/Leffmann/vasm/tree/685a87e5ed14285350ccdb6581c9771bc8df6c7d).
The GitHub project describes it as Frank Wille's portable assembler and links
to the official project site. Both the commit subject and `vasm.c` identify it
as version 1.9d. The selected M68k, Motorola syntax, oldstyle syntax, and
binary-output module docs say they are covered by vasm's main terms. The selected
Z80 backend has a separate BSD-style notice in its source and module docs; preserve
that notice as well. The [current official vasm manual](https://sun.hasenbraten.de/vasm/release/vasm.pdf) permits unmodified
archive redistribution and non-commercial use; its commercial exception is
limited to M68k targeting AmigaOS, with written consent required otherwise. This
setup builds the tools locally and does not ship them. Exact equivalence between
this mirror tree and an author-published 1.9d archive remains unverified; see the
[provenance audit](../../docs/research/vasm-1.9d-provenance-audit.md).

The BlastEm pin is a candidate core with exploratory local trace hooks. A
successful build does not qualify it as the timing or fidelity oracle; see
[`nativemd-oracle-spike.md`](../../docs/research/nativemd-oracle-spike.md).
The new tool checkouts refuse a revision change when local work is present.

`mise run test:md-oracle-smoke` exercises the exploratory runner with generated
cartridges. After `mise run tools:md`, `--trace` requires the candidate trace
ABI and writes `bus.trace`. The current patch advertises M68K and Z80 CPU
accesses, VDP ports/HINT lines, 68K-to-VDP source slots, fill/copy-DMA writes,
selected YM2612/PSG writes, and separate resampled YM2612/PSG PCM streams. This output
does not qualify the oracle. The runner
at `consoles/md/tests/oracle/run.py` accepts `--core`,
`--rom`, `--frames`, and an empty `--out` directory. Optional `--input` names a
JSON object mapping one-based frame numbers to port numbers (`"1"` or `"2"`)
and button-name lists, for example `{"1":{"1":["START"]}}`. Each frame is
a snapshot; unspecified frames and ports are released. `--pad1` and `--pad2`
select `3` or `6` buttons (default `6`); six-button-only inputs are rejected
for a three-button pad. Output is PPM frames, native-endian signed 16-bit
stereo PCM, a JSON manifest, and the available clocked trace events. The current
core samples input once per frame. The available traces and resampled chip PCM
are exploratory; complete timing coverage and reference qualification remain open.

The D01 NMDH execution hooks live only in the pinned candidate test build.
`mise run test:md-decomp` builds `tools:md` and runs the NMDH ABI, format,
identity, failure, actual-core, and checkpoint-replay controls. To measure the
authored synthetic capture budget, run
`python3 consoles/md/tools/decomp/benchmark.py --frames 30`; outputs remain in
ignored `build/decomp/synthetic-nmdh/`; the measured budget is recorded in
[`resource-budget.md`](../../consoles/md/docs/decomp/resource-budget.md). The
checkpoint section is candidate-only and does not change the delivered nmd
save contract or NMDT v2 trace format.
