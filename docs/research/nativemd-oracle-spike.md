# nativemd oracle spike

This is an exploratory assessment for stage 1, not a qualified reference runner.
The locally inspected libretro BlastEm Git revision is
`2230addc4dd74dd094fd264987fd808abba32d0e`. It is a candidate, not yet the
project's pinned reference. The official BlastEm repository uses Mercurial;
the libretro repository tracks upstream and carries libretro changes.

The libretro adapter in `libblastem.c` provides frame execution, video and
audio callbacks. Those callbacks alone do not provide the access-level evidence
required by the nativemd specification. The adapter's input is sampled once per
frame, which must be considered when designing scripted controller tests.

On this arm64 host, the Makefile selects generated 68000 and Z80 cores. Their
`.cpu` descriptions generate C, and the 68000 description uses separate
prefetch and data-memory operations. The memory interfaces and their clock
semantics must be traced before choosing hook points: logging only device port
handlers would omit ordinary memory traffic. Existing PSG and YM event logging
is a possible starting point, but does not establish complete timing or the
required per-chip audio output.

In this revision, `m68k_util.c` implements the generated core's byte and word
access helpers; each adds the base access cost before invoking its mapped
handler, which can add stalls. `z80_util.c` has separate fast-memory and mapped
paths, plus I/O helpers. Hook placement must capture both paths and define
whether a recorded clock is before or after the mapped handler's stalls. The
68000 prefetch calls the word-read operation, so identifying instruction fetches
requires an explicit tag at the prefetch call site rather than guessing from an
address comparison.

`genesis.c` periodically subtracts a common cycle deduction from the 68000,
Z80, VDP, YM and PSG counters. A trace that stores those raw counters as absolute
timestamps would move backward. The runner or instrumentation needs an epoch
accumulator updated at every adjustment, with a test spanning an adjustment
and verifying monotonic clocks across the affected devices.

On macOS arm64, exploratory `make libblastem.dylib` completed successfully.
Compilation generated a large 68000 source and emitted warnings. The resulting
arm64 library exports the libretro load, run, video and batch-audio entry points.
The existing generic portion of the reference runner loaded a locally generated
cartridge containing a 68000 branch-to-self and ran three frames, producing a
256 × 224 PPM. This only proves basic load and frame delivery on this host; the
runner's SNES-specific defaults and the blank synthetic image do not qualify it
as the MD oracle. Bus, DMA, audio, pixel fidelity, pad timing and settings remain
unverified.

The inspected adapter publishes options for region and region forcing, initial
RAM, 68000 clock divider, controller types, audio gains and low-pass cutoff,
and overscan cropping. Its fallback uses the first value of an option when the
frontend supplies none; the observed three-frame smoke used those fallbacks.
The real runner must supply and record explicit values, and exercise both pad
types. In this revision a divider of 7 is labeled native, RAM supports zero
initialization, and a 24000 Hz low-pass cutoff is labeled off. These labels do
not establish sample fidelity or hardware equivalence.

For the assembler, the exploratory Git mirror revision
`685a87e5ed14285350ccdb6581c9771bc8df6c7d` identifies itself as vasm 1.9d.
On this host, `CPU=m68k SYNTAX=mot` and `CPU=z80 SYNTAX=oldstyle` built
successfully. The first assembled `moveq #1,d0; bra.s *` to `700160fe`, and
the second assembled `ld a,42; halt` to `3e2a76`. This tests basic toolchain
operation, not the full cartridge build or the provenance and licensing of a
chosen distribution. The revision is a candidate until those are checked.

The candidate's `doc/vasm.texi` permits non-commercial use and has a specific
commercial-use exception for M68k targeting AmigaOS; it says other cases need
written consent and that individual modules may carry additional terms. The
local use here is a test tool, not a grant of rights for other uses. This Git
mirror's commit records version 1.9d, but equivalence to an author-published
archive has not been established. Keep its source and binaries out of the
repository and document the restriction in any setup instructions.
