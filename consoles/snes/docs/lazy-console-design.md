# The lazy console in nativekit

The console clock still defines hardware behavior. Hardware work is deferred
until a read, a write, an interrupt deadline or frame output can observe it.
This ports the console-era upstream design through `exact-console-3.0` while
retaining nativekit's software picture and existing timing contracts.

## Observations and deadlines

| Hardware | Work performed when observed |
|---|---|
| Bus and interrupts | Region lookups and access costs are prepared at power-on; waits advance to a possible change or an interrupt deadline |
| PPU | Pending lines are recorded/rendered before a state change affects them; STAT77 sprite flags catch up when read |
| HDMA | Lines whose effects can safely be batched are processed together; chip synchronization and register writes remain ordered |
| Joypads | Automatic-read progress and bits are derived from the clock |
| APU boot protocol | A state machine runs to the S-CPU observation point and stops at observable port handshakes |
| Sound | DSP state catches up at observable reads/writes; audio samples and voice/echo dependencies remain ordered |
| Cartridge memory | A chip may expose direct ranges; `nshvc_cart_direct_changed` invalidates cached mappings when those ranges change |

A wait can skip directly only when its predicate's possible changes are known.
Chip registers without a `next_change` answer, reads with effects, and
sound ports under conflicting chip/DMA/HDMA work retain conservative stepping.
This is not a promise that every arbitrary game poll can be skipped.

## Preservation boundaries

The game still supplies its C and explicit hardware waits. The console executes
no recovered processor instructions. Its interrupt, memory, picture and sound
results remain subject to the existing pinned reference checks.

Software picture generation and CPU reference comparisons remain production
capabilities in this fork. Upstream's later GPU-only rendering policy is not
part of this port. Disabling S-CPU costs still leaves SPC700 access timing
intact; no new speed mode is enabled implicitly.

The application watchdog observes an atomic heartbeat, not unsynchronized
console state. A missing heartbeat diagnoses a loop that cannot advance the
machine. It does not change emulated time or the deterministic schedule.

## Evidence and limits

`waits`, `ipl`, `chip`, `chip_irq`, `hdma_bulk_check`, `scaling_check` and the
DSP/reference matrix exercise these paths. The HDMA comparison checks bulk
against stepped execution; the scaling check checks that idle work does not
grow linearly with the number of frames waited. Neither establishes a
performance bound for every possible game or host.

Upstream's original audit and measurements are available at
[the pinned console revision](https://github.com/jeff-strutb/nativeshvc/blob/1c66ebb1fe638206fe26f04dbef1958ad3a28875/docs/lazy-console-design.md).
They are upstream evidence, not measurements of this extracted implementation.
