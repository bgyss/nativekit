# nativemd

`nmd` is nativekit's native Sega Mega Drive / Genesis console library. A
decompiled game's C uses the real console-facing bus APIs and nmd's C routine
tables; nmd advances the 68000 bus, Z80, VDP, DMA, interrupts, YM2612, PSG, and
controllers on one master-clock timeline. It does not execute machine code.

The public game contract is [1.1.0](contract.md); 1.1 adds
[native execution](decomp/native-execution.md): the game's reset routine and
main logic run on the console's 68000 coroutine. The supported base system
has selectable md1va3 and md1va6-with-TMSS models, NTSC/PAL regions, and two
three- or six-button controller ports. Unsupported bus operations fail with a
diagnostic instead of being guessed. Read [the contract](contract.md) for the
register map, timing rules, input behavior, profile format, and limits.

## Build and verify

From the repository root, inside `nix develop`:

```sh
mise trust                 # once per checkout
mise run build:null        # headless libraries and checks
mise run tools:md          # pinned candidate core and vasm assemblers
mise run test:md-rom       # 59 synthetic candidate comparisons
mise run test:md-consumer  # separate CMake consumers, headless
mise run test:md-execution # native reset entry, resume and interrupt returns
```

Focused checks include `test:md-console`, `test:md-io`, `test:md-z80`,
`test:md-bus`, `test:md-interrupt`, `test:md-vdp`, `test:md-dma`, and
`test:md-render`. `mise run test:md-profile` exercises the two processor cost
fits and native `--counts` handoff.

## Cost profiles

Measure one or more external ROM files with the pinned candidate:

```sh
python3 consoles/md/tools/nmd-profile measure game.bin \
  --out build/game-profile --frames 3600
```

The tool writes `profile.txt`, a public-header `profile.c`, and `report.txt`.
Repeated `--input` arguments accept one-based JSON controller snapshots;
`--decomp` adds native per-kind count comparisons, and `--source` records a
source-tree hash for later `nmd-profile check`. Reports store ROM hashes, not
ROM paths or contents. Candidate timing and fitted profiles are exploratory
evidence, not hardware qualification.

## Synthetic golden master

After `mise run test:md-rom` passes, record the synthetic-only baseline with
`mise run golden:md-record`. Later use `mise run golden:md-compare` to detect
changes to frame images, bus traces, PCM, and other case outputs. The baseline
is written under ignored `build/golden/md/`; the per-case ROMs are generated
fixtures and are not committed.
