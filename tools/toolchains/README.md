# Local test toolchains

Run `nix develop`, then `mise run tools` for the existing reference tools.
`mise run tools:md` separately builds the candidate BlastEm libretro core and
the 68000 and Z80 vasm assemblers under `build/toolchains/`. These tools are
local test dependencies and are not shipped with nativekit. Run
`mise run test:md-toolchain` to check the assemblers with synthetic instructions.

The vasm candidate is a third-party Git mirror of version 1.9d. Its equivalence
to an author-published archive has not been established. Its included terms
permit non-commercial use, with a limited M68k/AmigaOS commercial exception;
other commercial use requires written consent. Individual modules may have
additional terms. Consult the source's legal notices for your use.

The BlastEm pin is an uninstrumented candidate. A successful build does not
qualify it as the timing or fidelity oracle; see
[`nativemd-oracle-spike.md`](../../docs/research/nativemd-oracle-spike.md).
The new tool checkouts refuse a revision change when local work is present.

`mise run test:md-oracle-smoke` exercises the exploratory runner with generated
cartridges. After `mise run tools:md`, `--trace` requires the candidate core's
trace hook and writes `bus.trace`; it currently certifies only the M68K CPU
access path. DMA, Z80, VDP-port and sound traces are still absent. The runner
at `consoles/md/tests/oracle/run.py` accepts `--core`,
`--rom`, `--frames`, and an empty `--out` directory. Optional `--input` names a
JSON object mapping one-based frame numbers to port numbers (`"1"` or `"2"`)
and button-name lists, for example `{"1":{"1":["START"]}}`. Each frame is
a snapshot; unspecified frames and ports are released. `--pad1` and `--pad2`
select `3` or `6` buttons (default `6`); six-button-only inputs are rejected
for a three-button pad. Output is PPM frames,
native-endian signed 16-bit stereo PCM and a JSON manifest. The current core
samples input once per frame. This runner does not yet capture the clocked
traces or per-chip audio required for reference qualification.
