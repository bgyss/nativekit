# nativemd

`nmd` is nativekit's Sega Mega Drive / Genesis console library. A game supplies
reconstructed C and makes every 68000 bus access through `rd8`, `rd16`, `wr8`,
and `wr16`. The model preserves access order, width, master-clock timing, and
device stalls; it is not a general-purpose 68000 instruction emulator.

## Stage 3 scope

Stage 3 implements the base 68000 and Z80 routine models, shared bus scheduling,
Z80 BUSREQ/reset and bank-window arbitration, VINT dispatch and return, Mode 5
VDP output and DMA, H/V interrupts, PSG tone/noise, and YM2612 ports, timers,
DAC, FM operators, envelopes, algorithms, LFO, channel 3 special frequencies,
and stereo pan. The 68000 and Z80 share the serial bank register. Controller
protocol and peripheral work belong to Stage 4.

The sound pipeline resamples the two chip streams to `nk_console.audio_hz`,
adds their already-scaled signed samples with s16 saturation, and queues stereo
frames in a bounded SPSC ring. `audio_take` drains frames for the host callback;
`audio_queued` reports the current queue depth. If the host falls behind, the
producer drops new output frames rather than waiting. Source phase alignment,
reset-safe drain handoff, wrap behavior, and full/empty handling have named
unit checks.

## Build and verify

Run from the repository root inside `nix develop`:

```sh
mise run build:null
mise run build:macos
mise run tools:md
mise run test:md-sound
mise run test:md-rom
```

The focused checks are `test:md-console`, `test:md-z80`, `test:md-bus`,
`test:md-interrupt`, `test:md-vdp`, `test:md-dma`, and `test:md-render`.
`test:md-rom` checks the 56 synthetic cases, exact raw FM and mixed PCM, and
the mixed audio stream. Six individually reviewed candidate differences remain
in `consoles/md/tests/rom/KNOWN`. Run `test:md-oracle-smoke` and `test:md-trace`
when changing the pinned candidate hooks or trace ABI.

BlastEm and its generated traces are exploratory comparison evidence; they do
not qualify hardware accuracy or title compatibility. Stage 4 and Linux
verification remain open.
