# Candidate trace envelope

This format is an experimental transport for the planned oracle. No core emits
it yet, and its event timing and coverage are not qualified. In particular,
the record does not yet say whether a clock denotes access start or completion.
Consumers must not infer that an absent event means the operation did not occur.

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
later local counters. Exact meanings of address and value for every device,
stream capability declarations, loss reporting, and the core hook ABI remain
to be specified and tested before the format is used as oracle evidence.
