# NES CPU cost profiles

`nnes_cost_profile` supplies native game C work not represented by bus
accesses. The console adds a read/write weight, in tenths of a 2A03 cycle, to
each game-facing access of the matching memory kind. Weights are enabled by
default and `nnes_set_cpu_costs(console, 0)` disables them while retaining the
base bus cycle and explicit cycle charges.

Device state is sampled after the base bus cycle. The profile weight advances
the CPU and basic PPU clock after that operation, before the next access;
operation timestamps include the weight. This preserves register sampling
points while pacing native-C work between accesses.

The NES library copies and version-checks a profile when it creates the
cartridge instance. `nnes_cartridge.access_kind` classifies mapper-dependent
CPU addresses as WRAM, ROM, cartridge RAM, or I/O; without the callback,
internal WRAM and I/O use the built-in ranges and cartridge space defaults to
ROM. The example uses illustrative weights, not measurements from a game.

## Capture and fitting

`consoles/nes/tools/nnes-profile-mesen2.lua` captures typed NES CPU operations from Mesen2.
It requires the optional source changes in
[`tools/nnes-profile-mesen2-optype.patch`](../tools/nnes-profile-mesen2-optype.patch),
and Mesen's **Allow access to I/O and OS functions** script setting. The script
waits 120 frames and records 600 frames by default. Environment settings can
override warmup/capture frames, the reference label, the output filename, and
mapper-specific ranges. For example,
`NNES_PROFILE_MEMORY_RANGES=6000-7FFF:cart_ram` classifies SAROM battery RAM.
Set `NNES_PROFILE_STOP_ON_FINISH=1` when using Mesen's headless test runner so
the collector exits after capture. The script writes
`nnes-profile-raw-<timestamp>.jsonl` in Mesen's script data folder; the Mesen
log shows its full path. Set `NNES_PROFILE_REFERENCE` to the exact Mesen
release or source revision built with the patch. `NNES_PROFILE_OUTPUT_NAME`
selects a repeatable filename. The Lua collector timestamps accesses through
`emu.getCpuCycleCount(emu.cpuType.nes)`.

The collector was exercised with MesenCE source commit `9a05146d` and the local
operation-type patch on Dragon Warrior (USA) Rev A. A separate 120-frame
no-input run recorded 2,620,456 typed CPU bus events; its fit and limits are
documented in the title qualification ledger. This validates the collector and
normalizer path for that run, not general NES timing or gameplay.

Inspect the resulting ROM SHA-1, operation counts, and access kinds before
using a trace. Then normalize and fit it:

```sh
consoles/nes/tools/nnes-profile normalize /path/to/nnes-profile-raw-TIMESTAMP.jsonl \
  --out span-attract.jsonl
consoles/nes/tools/nnes-profile fit span-attract.jsonl span-level.jsonl \
  --out build/profile --name my_game
```

The fitter writes `profile.json`, `profile.c`, and `report.txt`. Include the
generated C symbol in the game and assign its address to
`nnes_cartridge.cost_profile`. Profiles approximate the supplied play spans;
compare fit error and access totals before use. The default 1,000-access
threshold sets less frequently used access kinds to zero; `--minimum-accesses`
can change it.

The fitter uses non-negative least squares over windows. Each window provides
the CPU-cycle gaps around active data accesses and eight counts ordered as
reads (WRAM, ROM, cartridge RAM, I/O), then writes in the same order. The fitted
weights minimize the difference between each window's measured gaps and the
weighted access counts. A deterministic sparse tie-break resolves kinds that
the trace cannot distinguish. Use varied spans that exercise memory kinds
independently to reduce this ambiguity.

## Trace contract

Normalized input to `fit` is JSON Lines. Its first object identifies the ROM,
reference, kind map, cycle basis, and exclusions:

```json
{"type":"header","format":"nnes-reference-trace","version":1,"rom_sha1":"0123456789abcdef0123456789abcdef01234567","reference":"Mesen2 + nnes operation-type callback patch","kind_map":"nnes-profile-kinds-v1","access_semantics":"data-only","polls_excluded":true,"dummy_access_cycles_removed":true,"dma_access_cycles_removed":true,"dma_stall_cycles_included":true,"cycle_basis":"cpu_cycles_including_dma_stalls"}
```

Each following object is one measurement window:

```json
{"type":"window","index":1,"gap_cycles":12000,"counts":[900,2500,100,80,400,500,30,20]}
```

The Mesen collector emits `nnes-cpu-op-trace` version 1 JSONL. Each access has
an increasing relative frame and CPU-cycle timestamp, its address, and an
operation type. Data reads and writes also carry a `kind`. The normalizer counts
only data reads/writes, removes tagged dummy and DMA bus-access cycles from the
gaps, and filters repeated same-address reads longer than 64 accesses as poll
loops by default. Gaps over 1,024 cycles are omitted by default. Change these
limits only when the reference trace supports it.

Raw headers must set `operation_types_included` to `true` and
`cycle_basis` to `cpu_cycles_including_dma_stalls`. Operation labels are
`read`, `write`, `opcode`, `operand`, `dummy_read`, `dummy_write`, `dma_read`,
`dma_write`, `ppu_rendering_read`, or `idle`. Only data reads and writes require
the four-value access kind and contribute profile counts. Opcode and operand
fetch cycles remain in gaps as native C work; dummy read/write cycles are
removed because `nnes_rd8_dummy` and `nnes_wr8_dummy` model their base bus
cycles and side effects without profile weights.

Mesen exposes opcode, operand, dummy, and DMA operation types after applying
the source patch. The cycle timestamps include DMA stall time. The normalizer
subtracts bus cycles for tagged `dma_read` and `dma_write` events, but some DMA
stall cycles may have no operation callback and therefore remain in measured
gaps. The generated report and JSON record this limitation. Those residual
cycles can inflate the fitted native-work weights; profiles are provisional for
games that use DMA and should be regenerated when `nnes` models DMA timing.

The default memory-kind map is WRAM below `$2000`, I/O from `$2000` through
`$401F`, then ROM for cartridge space. Use `NNES_PROFILE_MEMORY_RANGES` to
override mapper-dependent ranges so they match the game's
`nnes_cartridge.access_kind` callback.

The collector and patch are independent of the `nnes` build and runtime. The
patch extends Mesen's Lua memory callback from two parameters to
`(address, value, operationType)`; existing Lua callbacks may ignore the added
third parameter. Relevant Mesen2 source references:

- [NES CPU operation types and cycle count](https://github.com/SourMesen/Mesen2/blob/master/Core/NES/NesCpu.h)
- [ScriptManager operation dispatch](https://github.com/SourMesen/Mesen2/blob/master/Core/Debugger/ScriptManager.h)
- [Lua callback implementation](https://github.com/SourMesen/Mesen2/blob/master/Core/Debugger/ScriptingContext.cpp)
- [Lua callback documentation](https://github.com/SourMesen/Mesen2/blob/master/UI/Debugger/Documentation/LuaDocumentation.json)
