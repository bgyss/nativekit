# nativemd oracle spike

This is an exploratory assessment for stage 1, not a qualified reference runner.
The configured libretro BlastEm candidate pin is
`2230addc4dd74dd094fd264987fd808abba32d0e`. It is pinned for local builds, but
is not a qualified reference. The official BlastEm repository uses Mercurial;
the libretro repository tracks upstream and carries libretro changes.

The libretro adapter in `libblastem.c` provides frame execution, video and
audio callbacks. Those callbacks alone do not provide the access-level evidence
required by the nativemd specification. The adapter's input is sampled once per
frame, which must be considered when designing scripted controller tests.

On this arm64 host, the Makefile selects generated 68000 and Z80 cores. Their
`.cpu` descriptions generate C, and the 68000 description uses separate
prefetch and data-memory operations. Logging only device port handlers would
omit ordinary memory traffic. Existing PSG and YM event logging remains only
one part of the evidence; it does not establish complete timing or the required
per-chip audio output.

In this revision, `m68k_util.c` implements the generated core's byte and word
access helpers; each adds the base access cost before invoking its mapped
handler, which can add stalls. The patch now instruments `z80_util.c` across its
fast-memory, mapped-memory, and I/O paths. `z80.cpu` marks opcode fetches before
the generated core reads them. A synthetic program proves fast Z80 RAM writes,
a bank-window ROM read, a mapped YM-status read, and mapped YM writes. With the
pinned `md1va3` model, the fixture checks 15 master clocks per Z80 T-state and a
3T bank-window wait in the post-handler read timestamp. These checks cover the
candidate paths exercised by the fixture, not all Z80 timing behavior.

The synthetic 68000 bus fixture also checks byte and word `MOVE abs.l,Dn`
read intervals: both measure 16 CPU clocks, or 112 master clocks at the pinned
7-clock divider, matching Table 8-2 of the [Motorola M68000 User's Manual hosted
by NXP](https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf). PSG port
events match their 68000 bus-write clocks; Z80 YM port events match Z80 writes,
while a YM register commit is stamped at the next 42-master-clock chip tick.
These are candidate-path timing checks, not hardware qualification.

`genesis.c` periodically subtracts a common cycle deduction from the 68000,
Z80, VDP, YM and PSG counters. The instrumentation adds that deduction to a
shared trace epoch before those counters are adjusted. The 5,000-frame fixture
crosses the 32-bit boundary, checks VDP events afterward, and checks Z80 YM
writes before and after the adjustment. Post-adjustment Z80 bus and YM port
clocks agree. This verifies the exercised candidate path, not hardware timing
fidelity or every device-counter relationship.

The trace envelope and C callback helper live under `consoles/md/tests/oracle/`.
The reproducible BlastEm patch connects generated 68000 and Z80 accesses,
selected VDP port events, 68K-to-VDP source slots, VDP fill writes, YM2612/PSG
writes, VDP copy writes, and common cycle-counter deductions to the callback. CPU-access clocks
are emitted after mapped handlers return. The core advertises each available
path separately in the manifest. The runner captures frames, mixed PCM, and
separate resampled YM2612/PSG contributions at the frontend rate, before the
final mix and DC blocker. Full device timing and raw chip-clock audio remain absent.
Its libretro build selects BlastEm's generated C interpreter so
the explicit fetch hooks run; this build mode may affect reference behavior and
needs separate validation. The partial trace and mixed audio do not qualify the
oracle.

The initial feature extractor at
[`consoles/md/tools/features.py`](../../consoles/md/tools/features.py) returns
evidence with `observed`, `not_observed`, or `unavailable` status. It can report
VDP register modes, the three DMA kinds, interlace/shadow-highlight bits, Z80
DAC writes, direct 68000 YM writes, pad TH probe sequences, and mapper-register
accesses. HINT events report VDP pending lines, not 68000 interrupt acceptance.
SRAM use remains unavailable unless the run supplies the cartridge SRAM window.

On macOS arm64, the pinned-source `mise run tools:md` build completed with the
Z80 patch applied. The generated core emitted 442 compiler warnings across
three generated/upstream translation units. The resulting arm64 library exports
the libretro load, run, video and batch-audio entry points.
The current runner fixes and records its selected model, region, RAM, clock
divider, pad, audio and overscan options. Explicit zero overscan yields
283 × 243 frames in the synthetic check; this is callback geometry for that
core configuration, not a nominal console-size claim. Synthetic input and sound
checks exercise selected paths, but do not qualify pad timing, pixel/audio
fidelity, DMA or the oracle.

The inspected adapter publishes options for region and region forcing, initial
RAM, 68000 clock divider, controller types, audio gains and low-pass cutoff,
and overscan cropping. The runner supplies explicit values and records which
ones the core queried. Synthetic tests exercise 3- and 6-button settings and
frame-snapshot input; the adapter still samples only once per frame. In this
revision a divider of 7 is labeled native, RAM supports zero initialization, and
a 24000 Hz low-pass cutoff is labeled off. These labels do not establish sample
fidelity or hardware equivalence.

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
