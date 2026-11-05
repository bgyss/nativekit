# Mega Drive Test Fixtures

Oracle tests assemble synthetic cartridges and generate their fixture data at
runtime. No commercial ROM images are stored in this directory. Local library
surveys accept the ROM directory as a runtime argument; reports contain
sanitized titles and observations, not ROM paths or contents.

BlastEm and vasm are local test dependencies built under `build/toolchains/`;
they are not shipped with nativekit. Review the pinned revisions, patch
provenance, and license notes in [`tools/toolchains/README.md`](../../../tools/toolchains/README.md)
before redistributing or using those tools. The vasm setup is limited to
non-commercial use, and exact equivalence between the pinned mirror and an
author-published archive remains unverified.

The candidate BlastEm traces and generated reports are exploratory evidence;
they do not qualify hardware accuracy or title compatibility.

Run `mise run test:md-bus` for native bus checks, `mise run test:md-interrupt`
for scheduler and interrupt checks, and `mise run test:md-rom` for the full
59-case synthetic matrix on BlastEm and nmd. The interrupt fixture compares
HINT line and relative-clock deltas because the candidate trace epoch starts
at a different absolute clock. Candidate-only differences stay in `rom/KNOWN`.

Run `mise run test:md-profile` to verify per-VBlank CPU statistics, 68000/Z80
weight fitting, generated C compilation, source-hash checks, and the native
`--counts` handoff. The candidate stats sidecar and its clock-splitting limits
are documented in [`oracle/TRACE_FORMAT.md`](oracle/TRACE_FORMAT.md); it is
exploratory profile evidence, not hardware qualification.

## D01 decomp execution hooks

Run `mise run test:md-decomp` for the NMDH ABI, stream and identity controls,
runner timeout/storage failure cases, planted CPU/epoch/order errors, both
actual CPU fixtures, synchronized handoffs, and post-frame/STOP checkpoint
replay. The task builds the pinned candidate core through its `tools:md`
dependency. These checks use authored synthetic cartridges and qualify the
candidate test core only; they do not qualify hardware accuracy or a retail
title.

Run `python3 consoles/md/tools/decomp/benchmark.py --frames 30` for the
bounded combined-CPU resource measurement. It writes capture files and a JSON
measurement under ignored `build/decomp/synthetic-nmdh/`. The defaults remain
420 seconds and 256 MiB per capture; the measured workload and limits are
reported in [`decomp/resource-budget.md`](../docs/decomp/resource-budget.md).

Run `mise run test:md-consumer` to build the separate CMake consumer and execute
it headless. `mise run test:md-golden-check` plants pixel, bus-trace, and PCM
changes in the comparator. After `mise run test:md-rom` passes, record the
synthetic-only baseline with `mise run golden:md-record`; compare it later with
`mise run golden:md-compare`.

## Stage 2 ROM matrix

`rom/cases.py` defines the named 68000 bus, VDP port, DMA, and Mode 5 render
cases, including disabled display and sprite status. The runner compares bus
access order and values, DMA slots, and complete active pictures. The direct
VDP microprogram also compares relative CPU-to-VDP event clocks; native unit
checks pin bus-cycle and scheduler timing. To run a focused subset, pass
comma-separated case names after the task separator, for example:

```sh
mise run test:md-rom -- --only render_sprite_basic,render_interlace2
```

The runner rejects unknown names and reports only the selected cases. Stage 2
added four reviewed candidate differences: TAS handling and the candidate's
faster memory-DMA slot cadence. The listed cases remain visible in the full run
so changes to either side are detected.

## Stage 3 sound vectors

`rom/cases.py` covers YM2612 ports, timers, DAC, all eight FM algorithms,
key-on/off envelopes, channel 3 special frequencies with LFO and envelope key
scaling, stereo pan, PSG tones/noise, and combined PSG/YM output. The runner
captures raw source streams for each chip before BlastEm's host audio filter/
resampler. Raw FM and mixed PCM compare sample-for-sample. The approved PSG
tone/noise and YM DAC vectors keep their bounded phase/error tolerance. The
mixed vector uses raw chip streams as the expected input to the shared
sample-rate converter and saturating s16 sum. `audio_compare_check.py` plants
one-unit changes at both stream edges and a larger interior change to prove the
exact comparator reports the first differing sample and channel. Unit checks
pin data-only volume continuation, tone-3 noise-edge switching across a frame,
and PSG/YM source-clock alignment. `rom/KNOWN` records six individually
reviewed candidate differences, including the two PSG protocol cases where
BlastEm shares the hardware divergence. Focus a case with, for example:

```sh
mise run test:md-rom -- --only sound_ym_algorithm_0
```

## Stage 4 controller and model cases

The full matrix includes the six-button TH sequence with scripted input on both
ports, the md1va3 version register, and md1va6 TMSS unlock and VDP access. Unit
checks also cover three-button phases, independent port state, per-frame input
updates, version bits, open TMSS behavior, and locked VDP failures. The six
candidate-only differences in `rom/KNOWN` remain individually named and reasoned.
