# Lockstep: all C routines at once (D05)

The Mega Drive counterpart of the SNES playbook's lockstep gate (A7). Routines
that pass the [per-call check](check-calls.md) one at a time must also behave
when every one of them runs as C together, on both CPUs, under the machine's
real scheduling: interrupts, DMA, bus requests between the CPUs, and the VDP
and sound chips running beside them.

```sh
python3 consoles/md/tools/decomp/hybrid.py --runtime live --out DIR SOURCE.c...
python3 consoles/md/tools/decomp/lockstep.py --core CORE --rom ROM \
    --scenario SCENARIO.json --frames N --library DIR/hybrid-live.dylib --out OUT \
    [--timeout SECONDS]
```

It exits 0 only for IDENTICAL; `OUT/lockstep.json` holds the report.

## How it runs

The pinned reference core needs patch 0005
([toolchain notes](../../../../tools/toolchains/README.md)). Two runs from
power-on with the scenario's input, each in its own worker process:

1. **Reference.** The original, untouched. Every call of a routine the library
   has C for is cut from the events as `check_calls.py` cuts them, with the
   clock its entry was reached at, the clock it returned at, the clock each of
   its hardware accesses began at, and the machine's 68000 scheduling state at
   those clocks (RAM refresh counter, next sync, next interrupt, run target,
   run-loop slice end). After every frame: SHA-256 of the picture, the sound
   and each snapshot region.
2. **Hybrid.** The same machine, but when a routine's entry is reached the
   core calls the checker. If the reference's matching call can be
   substituted, the C runs on the machine's own registers and memory
   ([`check/live.c`](../../tools/decomp/check/live.c)):
   - ROM and RAM accesses are made at once, without moving the clock.
   - A hardware access must be the original's next one (address, width,
     direction). It is made for real, starting at the clock the original's
     began, with the refresh counter the original's had.
   - When the C returns, the CPU is resumed at the C's return address, at
     the clock the original returned, with the original's scheduling state.

   After every frame the same hashes, which must equal the reference's.

Nothing is played back into the C. What it reads, from RAM, the VDP, the pads
or the other CPU, is what the hybrid machine holds at that moment. Only the
schedule is the reference's. A routine whose C nests calls to other routines
with C runs as one substitution; the inner calls are counted as taken.

Snapshot regions: `m68k` (D0-D7, A0-A7, the other stack pointer, PC, status,
flags, stopped, prefetch, clock), `z80` (registers, alternate set, flags,
I, IFFs, IM, bus request/acknowledge, reset, clock), `work_ram`, `z80_ram`,
`vram`, `cram`, `vsram`, `vdp_registers`, plus `video` and `audio`. Not in
the snapshot: YM2612 and PSG registers (only their sound is compared), the VDP's
internal FIFO and DMA progress, I/O port state, the 68000's pending-interrupt
fields. A defect that shows only there is found when it reaches one of the
compared regions, or not at all.

## What is left to the original

A call is not substituted, and is counted by reason, when:

- `interrupt`: an interrupt or exception was taken during it.
- `spans_frames`: it returned in a later frame than it began.
- `foreign_write`: the 68000 wrote Z80 RAM during a Z80 call.
- `irregular_return`: it, or a call inside it, did not return by its own `rts`/`ret`.
- `fallback`: it calls a routine that has no C.
- `unfinished`: the run ended before it returned.
- `spans_slice`: a Z80 call the original paused inside at the end of a Z80
  run slice while the 68000 ran. Run all at once, the C's accesses would come
  before 68000 accesses that the original's followed (the sound differed).

The core reports every arrival at an entry address, a branch back to the
entry inside a call included. Each call is matched to its first arrival, and
the arrivals inside it are loop passes the hybrid ignores.

Such a call runs as the original. A run with any of them is not **certified**,
however its frames compare.

## Verdicts

| Verdict | Meaning |
|---|---|
| `IDENTICAL` | Every frame equal, at least one call substituted. Certified only if no call was left to the original. |
| `DIVERGENT` | The first difference: a frame's regions, a hardware access out of the original's order or missing, an entry reached at another clock, or a call the original did not make or that is missing. A difference inside a C routine stops the hybrid machine at once. |
| `UNVERIFIED` | Nothing substituted, or fewer frames compared than asked for. |
| `INCONCLUSIVE` | A worker failed or ran past `--timeout`; it is stopped and nothing is judged. |

The report names the ROM, core, library, both scenarios and the runner by
SHA-256, and lists for each routine its calls, how many were substituted and
how many were left to the original by reason.

## Evidence

`lockstep_check.py`, on the per-call control cartridge (4 frames, B held on
pad 1) and its authored C:

- All 4 frames equal with 1,702 calls substituted on both CPUs and 3,386
  hardware accesses made at the original's clocks, among them the 68000
  passing a byte to the Z80 through Z80 RAM under a bus request, a pad read,
  a PSG tone, a RAM-to-VRAM DMA and a routine that enables the vertical
  interrupt.
- 966 calls left to the original: one that the vertical interrupt overlaps and
  that spans frames, six that call a routine with no C, Z80 calls the 68000
  wrote into or that were still open at the end, and Z80 calls that span a Z80
  run slice (most of them). The run is not certified.
- Six planted defects DIVERGENT where planted: a hardware access out of order
  (in the call), a store to the wrong address (`work_ram`), an interrupt mask
  left up (`work_ram`: the handler's counter), half a DMA length
  (`vdp_registers`), a silent PSG volume (`audio` only), and the hybrid run
  fed another pad state (`work_ram`).
- A run given 0.3 s is INCONCLUSIVE and leaves no worker running.
- Breaking the frame comparison in `lockstep.py` makes the check fail.

Making it match took five corrections to the substitution itself, each found
by the clock a later routine was entered at: the RAM refresh counter, the
68000's run target, the run loop's own slice end, and an interrupt latched at
the boundary before the original's return (taken at the boundary after it)
are machine state no event carries, and each must be what the original's
was at the resume clock. And the machine syncs the original made between
instructions inside the call (devices run, a finished frame noted) are made
again at their clocks: the core counts them, the reference records the ones
inside each call, and the C makes each before its first hardware access at or
after that clock, the rest before the resume. Without them a frame ended one
instruction late (first title, frame 351); syncing at the resume clock
instead ran the Z80 too far ahead.

### First title

All 301 routines of the first title with C (23 hand-written, 278 drafts;
kept in its private repository) over the 600-frame no-input boot scenario:
484 frames equal, 37,178 calls substituted, 44,275 hardware accesses made at
the original's clocks, 1,600 calls left to the original. At frame 485 a
routine is entered 56 clocks early: both runs reach a sync after the same
instructions with the same refresh counter, and on the original the 68000
leaves it 56 clocks (four bus cycles) later than refresh alone explains. The
title plays samples through the Z80's window onto the 68000 bus, which
stalls the 68000 in this core; that contention is the open lead.

Earlier, with the 23 hand-written routines only: 486 frames equal, then a
routine entered 56 clocks late. Traced back, both runs execute the same 68000
instructions with the same refresh counter up to a machine sync at the end of
a run slice ($674 in the attract piece at $57A, reached as the original); the
hybrid leaves that sync 56 clocks (four refresh delays) later. No Z80
instruction differs in that window and the bus-busy flag is not held across
it. The cause is open; until it is found the gate is not reliable for runs
of that length.

## Limits

- Authored synthetic controls only; no routine of a title has been checked.
- The reference is the candidate core, not hardware. Equality is with its
  scheduling; native timing is D10's.
- Calls that interrupts overlap, that span frames or that need a routine
  without C run as the original: the gate does not judge them. Nor does it
  judge Z80 calls that span a Z80 run slice, which on this core is most Z80
  calls: judging them needs the C suspended at the slice end, as the
  original is. A title's
  main loop and its interrupt handlers need work before they can be substituted.
- YM2612/PSG registers, the VDP FIFO and I/O state are compared only through
  sound, picture and RAM.
- One run, cold from power-on. Checkpoint starts, state loads during a call
  and resets are not exercised.
