# nativemd

`nmd` is nativekit's Sega Mega Drive / Genesis console library. A game supplies
its reconstructed C and makes the original machine accesses through the public
`rd8`, `rd16`, `wr8`, and `wr16` calls. The library models access widths, bus
time, device stalls, and VDP events; it is not a general-purpose 68000
instruction emulator.

## Stage 2 scope

Stage 2 covers the base 68000 memory map, shared master-clock scheduling,
VDP ports and FIFO, VBlank/HBlank interrupts, and Mode 5 VDP output. The
renderer supports H32/H40, V28/V30, planes A and B, window, scrolling,
linked sprites, priority, shadow/highlight, and doubled-height interlace mode.
Display disable, sprite overflow/collision, and odd-field status are modeled.
Memory-to-VDP DMA, VRAM fill, and VRAM copy are modeled with bus stalls.

Z80 instruction execution, YM2612 and SN76489 sound, controller behavior,
TMSS, SRAM, cartridge mappers, Sega CD, 32X, and SVP are outside this stage.
Their ports or storage may be decoded where the bus contract requires it, but
that does not mean those devices are implemented.

## Build and verify

Run from the repository root inside the configured development environment:

```sh
mise run build:null
mise run test:md-bus
mise run test:md-vdp
mise run test:md-dma
mise run test:md-interrupt
mise run test:md-render
mise run test:md-rom
```

`mise run test:md-rom` builds the local vasm and pinned BlastEm tools, then
compares shared synthetic cartridges with `nmd`. Focus selected fixtures with
`mise run test:md-rom -- --only CASE,...`; names are listed in
`consoles/md/tests/rom/cases.py`. The four candidate-specific differences are
documented in `consoles/md/tests/rom/KNOWN`.

BlastEm is a candidate comparison target, not hardware qualification. Keep
claims tied to the checked cases and their documented limitations.
