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
