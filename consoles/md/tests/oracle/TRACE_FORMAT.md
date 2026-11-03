# Candidate trace envelope

This format and its BlastEm hooks are experimental; timing and fidelity are not
qualified. M68K and Z80 CPU-access clocks are emitted after the mapped handler
returns, so stalls applied by that handler are included when it advances the
CPU clock. Consumers must not infer that an absent event means the operation
did not occur.

Version 2 uses little-endian integers. Its 16-byte header is `NMDT`, a 16-bit
version (`2`), a 16-bit record size (`20`), and a 64-bit record count. Each
20-byte record contains a 64-bit master clock; 8-bit device, operation, width,
and reserved fields; and 32-bit address and value fields. The reserved field
must be zero. A 12-byte footer contains `NEND` and the same 64-bit count. The
reader rejects unsupported versions, invalid fields, per-device clock
regressions, incomplete records, a missing footer and trailing data. It
validates the entire input before yielding any events.

Device IDs 1–5 denote the 68000, Z80, VDP, YM2612 and PSG. Operation IDs 1–5
denote fetch, read, write, DMA, and interrupt. Widths are 1, 2 or 4 bytes. Events from
different devices may be interleaved out of clock order; clocks for each device
must be nondecreasing. The epoch accumulator adds common cycle-counter
deductions to later local counters. The callback is synchronous and passes
each field by value. Capability bits report instrumented paths:

- `m68k_cpu_access`: generated-core byte/word reads and writes, with explicit
  prefetches tagged as fetches.
- `z80_cpu_access`: generated-core one-byte reads and writes through fast RAM,
  mapped memory and I/O helpers; opcode fetches are tagged separately.
- `vdp_port`: control/data-port reads and writes observed by VDP handlers. For
  VDP records, address `0` is the data port and address `4` is the control port.
- `vdp_68k_dma_source`: 68K-to-VDP DMA source slots. For these `DMA` records,
  `address` is the source byte address, `value` is the transferred word, and the
  clock is the VDP scheduler's source-slot time.
- `vdp_dma_fill`: VDP fill-mode write slots. These records carry the destination
  VDP address tagged with `0x10000000`, plus the written byte and destination
  slot clock.
- `vdp_dma_copy`: VDP copy-mode write slots, with the destination byte and
  destination-slot clock; the address is tagged with `0x20000000`.
- `vdp_hint_lines`: HINT pending events while enabled. The address is the
  V-counter line, the value is the HINT reload register, and the clock is the
  VDP line-boundary time.
- `ym2612_write`, `ym2612_port_write`, and `psg_write`: selected register and
  port writes only. YM port events tag their source in the address: `0x80000000`
  for a 68000 bus write and `0x40000000` for a Z80 bus write, ORed with the
  two-bit YM port number. CPU port events use access-completion time; a YM
  register commit is timestamped at the next YM internal clock boundary (42
  master clocks for the pinned model).
- `chip_audio`: the runner writes `ym2612.raw` and `psg.raw` as S16-native-endian
  stereo PCM at the manifest sample rate. These are source-resampled,
  source-gained contributions captured before the final mix and DC blocker; the
  sample counts align with `audio.raw`.

The synthetic `md1va3` Z80 fixture checks 15 master clocks per Z80 T-state and
the 3T bank-window wait included in a read completion timestamp. A fill fixture
also checks a one-byte write 5 H32 slots after the seed port write. These cover
candidate paths only. These hooks do not cover all timing details or qualify
hardware fidelity; separate resampled chip streams are not raw chip-clock
captures. This is not a qualified oracle.
