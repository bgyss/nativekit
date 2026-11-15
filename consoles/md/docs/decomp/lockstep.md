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
     began, with the refresh counter the original's had and the 68000's
     prefetch register set to the original's (VDP status and I/O reads return
     it in their open-bus bits).
   - When the C returns, the CPU is resumed at the C's return address, at
     the clock the original returned, with the original's scheduling state.

   When a 68000 call's C returns, its registers (D0-D7, A0-A7, SR and the
   return address) must be the original's at its return; a difference stops
   the run there, at the call that made it. After every frame the same
   hashes, which must equal the reference's.

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

## Calls an interrupt lands in, that take a TRAP or span a frame

Each call's C runs on its own stack (a coroutine, `check/live.c`). Where the
original took an interrupt inside the call, the C pauses: a draft (and hand
C that marks its instructions with `NMD_AT`) pauses on reaching the
instruction the original was interrupted before, with the original's
registers there; other C pauses before its first hardware access after that
point, or at its end. The CPU is resumed at the interrupted PC with the
original's scheduling state there (its latched interrupt included), the
machine takes the interrupt and runs the handler, and when the handler
returns to that PC the core calls the runner again, which continues the C
with the registers the handler left. The original's syncs inside the
handler happen on their own; those in the call are made in time order with
the pauses.

A frame that ends inside a call is a pause of the same kind: the C stops
where the original stood when the frame ended, the run returns from the
frame (whose hashes are compared there), and the next frame continues the C.
Where the original stopped twice before one instruction (the frame ended,
then an interrupt was taken), a mark pauses for both.

Two rules make a pause land where the original's did:

- A mark pauses only once the C has made every hardware access the original
  made before the pause. In a polling loop every pass has the same
  registers; without the rule the C paused at an earlier pass, before VDP
  status reads whose syncs had run the Z80 on (first title, frame 67: the
  Z80 stood 16 T-states short when the frame ended). `live_check.py` pins it
  on a fake machine.
- The syncs made before a pause are those the original made before it in
  event order, not every sync up to its clock: the sync the core counts at a
  frame's last clock belongs to the next frame (first title, frame 59: made
  early, it moved the Z80's clock under a bus request).

A TRAP is an exception pause. The C reaches it through
`NMD_EXCEPTION_TAKEN(address)` (a draft's `trap_call` makes it): it stops,
the machine resumes at the TRAP itself, executes it and runs the handler as
the original, and the C continues at the instruction after the TRAP with the
registers the handler left. C that runs past the original's TRAP without
taking it is DIVERGENT (`the original took an exception the C did not`).
Other exceptions are still left to the original.

## What is left to the original

A call is not substituted, and is counted by reason, when:

- `interrupt`, `exception`, `spans_frames`: an interrupt, a frame end or a
  TRAP in a call that also has another reason (below), or that calls a routine
  with no C; an exception other than a TRAP.
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

- All 4 frames equal with 1,478 calls substituted on both CPUs and 5,117
  hardware accesses made at the original's clocks, among them the 68000
  passing a byte to the Z80 through Z80 RAM under a bus request, a pad read,
  a PSG tone, a RAM-to-VRAM DMA, a routine that enables the vertical
  interrupt, one that horizontal interrupts land in, one that runs past the
  end of a frame (interrupted too), one that polls the VDP status through a
  vertical blank across a frame's end, and one that takes a TRAP whose
  handler has no C.
- 1,196 calls left to the original: six that call a routine with no C, Z80
  calls the 68000 wrote into or that were still open at the end, and Z80
  calls that span a Z80 run slice (most of them). The run is not certified.
- Nine planted defects DIVERGENT where planted: a hardware access out of order
  (in the call), a store to the wrong address (`work_ram`), an interrupt mask
  left up (SR at the C's return), half a DMA length (`vdp_registers`), a
  silent PSG volume (`audio` only), a wrong store inside the interrupted
  call, inside the frame-spanning call and after the TRAP (`work_ram`), and
  the hybrid run fed another pad state.
- Without the frame-end pauses the frame-spanning call's C runs ahead of the
  original and the first frame differs.
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
after that clock, the rest before the resume, each with the RAM refresh
counter the original's had when it began (the sync charges refresh delay,
and the clock it reaches decides how far the Z80 runs and so which sync
charges the 68000 for the Z80's bank-window reads). Without the syncs a
frame ended one instruction late (first title, frame 351); syncing at the
resume clock ran the Z80 too far ahead; without the refresh counter a bank
read's 56 clocks were charged at another sync (frame 485).

### First title

All 303 routines of the first title with C (23 hand-written 68000, 278
drafts, 2 hand-written Z80; kept in its private repository), with
interrupted, frame-spanning and TRAP calls substituted: over the 600-frame
boot scenario IDENTICAL on all 600 frames with 1,607 pauses. Every 68000 call
that returned was substituted; left to the original are 3 calls still open
at the end and the Z80 calls the original paused inside at the end of a Z80
run slice (72,982), so not certified. With interrupted calls only: 50,010
calls substituted and 533 interrupt pauses. Before interrupted calls were
substituted: IDENTICAL on all 600 boot frames (57,245 calls, counting
nested ones differently) and all 3,000 gameplay frames (176,640 calls), the
last difference on the way being open bus (the VDP status word's upper bits
are the 68000's prefetch register, which the C's read did not carry until
each recorded access did).

Hand-written C needs `NMD_AT` marks where interrupts land: without them a
paused C has run ahead of the original's interrupt point, and the handler
saw other registers (first title, frames 65 and 333).

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
- Calls that need a routine without C, or take an exception other than a
  TRAP, run as the original: the gate does not judge them. Nor does it
  judge Z80 calls that span a Z80 run slice, which on this core is most Z80
  calls: judging them needs the C suspended at the slice end, as the
  original is. A title's
  main loop and its interrupt handlers need work before they can be substituted.
- YM2612/PSG registers, the VDP FIFO and I/O state are compared only through
  sound, picture and RAM.
- One run, cold from power-on. Checkpoint starts, state loads during a call
  and resets are not exercised.
