# Candidate trace envelope

This format and the first M68K hook are experimental; timing and fidelity are
not qualified. Current M68K clocks denote post-handler access completion,
including stalls applied by the mapped handler. Consumers must not infer that
an absent event means the operation did not occur.

Version 1 uses little-endian integers. Its 16-byte header is `NMDT`, a 16-bit
version (`1`), a 16-bit record size (`20`), and a 64-bit record count. Each
20-byte record contains a 64-bit master clock; 8-bit device, operation, width,
and reserved fields; and 32-bit address and value fields. The reserved field
must be zero. A 12-byte footer contains `NEND` and the same 64-bit count. The
reader rejects unsupported versions, invalid fields, per-device clock
regressions, incomplete records, a missing footer and trailing data. It
validates the entire input before yielding any events.

Device IDs 1–5 denote the 68000, Z80, VDP, YM2612 and PSG. Operation IDs 1–4
denote fetch, read, write and DMA. Widths are 1, 2 or 4 bytes. Events from
different devices may be interleaved out of clock order; clocks for each device
must be nondecreasing. The epoch accumulator adds cycle-counter deductions to
later local counters. The callback is synchronous and passes each field by
value. Its capability bits report available trace paths. The M68K CPU-access bit
covers generated-core byte/word reads and writes plus explicit prefetches. The
VDP-port bit covers control/data-port reads and writes observed by VDP handlers.
For VDP records, address `0` denotes the data port and address `4` the control port.
Neither bit covers DMA master transfers. Z80, YM2612, PSG, video and per-chip
audio are not covered, so this is not a complete oracle.
