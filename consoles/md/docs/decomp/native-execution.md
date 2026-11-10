# Native execution contract (D02)

How a game's own 68000 and Z80 logic, written as C, starts and keeps running
on nativemd. Added in contract **1.1.0**; a cart that does not ask for it
behaves exactly as under 1.0. Nothing here executes guest machine code: every
routine is the game's C, and time passes only in its bus accesses and waits.

## Opting in

```c
const nmd_cart cart = {
    .rom = rom, .rom_size = sizeof rom,
    .routines = routines, .routine_count = ...,   /* sorted by address */
    .z80_routines = z80_routines, .z80_routine_count = ...,
    .execution = NMD_EXECUTION_NATIVE,
};
return nmd_main(&cart, argc, argv);
```

`nmd_cart.execution` is appended to the struct. `NMD_EXECUTION_HOST` (zero)
is 1.0: the caller of `nmd_run_frame` drives the machine from its own stack,
and interrupt handlers return by restoring the whole saved CPU state.

## 68000 entry and resume

| Event | Behavior |
|---|---|
| `nmd_power` | Resets the machine, then loads `A7` (and `ssp`) from ROM `$000000`, `PC` from `$000004` (24 bits), `SR = $2700`. The vectors are read from the cart with no bus time. The routine table must hold a C routine at the reset PC. No game code runs yet. |
| `nmd_run_frame` | Switches to the 68000 coroutine (16 MiB host stack). The first frame enters the reset routine; later frames resume where the last one left. |
| Frame yield | At the first point where the 68000 could take an interrupt (after a bus access, outside a longword or `nmd_tas`, or inside `nmd_wait_until` / `nmd_wait_vblank` / `nmd_stop`) with the clock at or past the vertical blank's start. A waiting game yields exactly at that clock, before the blank's interrupt is taken; the interrupt is taken when the next frame resumes it. A yield can fall inside a handler. |
| Input | The kit sets the pads before each `nmd_run_frame`, so the interrupt of the blank that ends frame *n* reads frame *n+1*'s input. |
| Hardware waits | `nmd_wait_until`, `nmd_wait_vblank`, `nmd_stop`, unchanged; they yield at frame ends. |
| `nmd_restart(addr)` | Drops every C frame of the 68000's run and enters the table's routine at `addr` (a jump to code that never returns to its caller). Registers, `SR` and the guest stack are unchanged. |
| Reset repetition | `nmd_power` may be called again from the host at any time, including while the run is suspended inside an interrupt handler: the old run's host stack is abandoned and the next frame enters the reset routine. Two runs from power-on are identical. |

A game whose main logic makes no bus access and no wait never reaches a
yield: there is no instruction clock to end its frame. That is a property of
the cost model, not detected.

Fatal (exit 70), each held by `execution_check.py`:

- `native execution needs a ROM with reset vectors`
- `reset vector $%06X has no C routine`
- `the 68000 routine at $%06X returned to no caller`
- `nmd_run_frame from the game's 68000 routine`, `nmd_power from the game's 68000 routine`
- `nmd_restart outside the game's 68000 routine`, `nmd_restart to $%06X with no C routine`, `nmd_restart inside an indivisible 68000 operation`
- `interrupt vector $%02X points to $%06X with no C routine`
- `interrupt handler returned to $%06X with no C routine`
- `Z80 entry $%04X has no C routine`, `Z80 VINT vector $%04X has no C routine`

## Interrupt and return semantics

The audit at `4496637` found that both CPUs returned from a handler by
restoring saved host-side state. That hides what an original handler can do
to its own return, so native execution uses the guest stack instead.

**68000.** Acceptance: the pending bit clears; `SR` gets the S bit and the
level as its mask (trace bit cleared), which makes `A7` the supervisor stack
(`usp` keeps the user's); PC (long) then SR (word) are pushed with timed bus
writes; the vector is read with a timed long read; its routine is called.
When the routine returns, SR and PC are popped with timed bus reads and
loaded, as RTE does. Consequences:

- D0-D7/A0-A6 are **not** restored. A handler that needs them preserved saves
  them, as the original's `movem` does.
- A handler's edit of the stacked SR takes effect (mask, S bit, flags).
- The stacked PC is `nmd_cpu.pc` as the game's C last left it: nativemd has no
  program counter of its own, so those guest-memory bytes match the
  original only if the C maintains `pc`. If the popped PC differs from the
  pushed one, the interrupted C cannot be resumed there: it is treated as
  `nmd_restart` to that address (fatal when no routine is there).
- Nesting follows the SR mask: a level-6 interrupt is taken inside a level-4
  handler; a handler is not re-entered by its own level unless it lowers the mask.
- Exception processing itself (44 cycles on hardware) is charged only its five
  bus cycles in, three out, like every other 68000 operation in the cost model.

`nmd_set_sr` is the way to load SR when the S bit or the mask changes: it
exchanges `A7` with `usp`/`ssp` and takes any interrupt the new mask admits.
Assigning `nmd_cpu.sr` directly still works for flags; a lowered mask is then
noticed at the next bus access.

**Z80.** Acceptance (IFF1 set, not in a bus access, bus not requested, not in
reset): both IFFs clear; PC is pushed high byte then low with `zwr` below
`SP`; the IM 0/1 vector `$0038` or the IM 2 table entry is called. On return
PC is popped with `zrd`; IFFs stay as the handler set them (`EI` is
`nmd_z80.iff1 = nmd_z80.iff2 = 1`). A popped PC that differs restarts the Z80
at that routine, as `nmd_z80_restart` does. The acknowledge cycle's other
T-states are not charged.

## Game-facing ABI boundary

| CPU | State in the contract | Notes |
|---|---|---|
| 68000 | `d[8]`, `a[8]`, `pc`, `sr`, and since 1.1 `usp`, `ssp` | `a[7]` is the active stack pointer. `usp`/`ssp` hold the inactive one; MOVE USP is a plain field access. No prefetch, no pending-interrupt or STOP state: `nmd_stop` is a call. |
| Z80 | `a f b c d e h l`, `i r iff1 iff2 im`, `ix iy sp pc`, and since 1.1 `a2 f2 b2 c2 d2 e2 h2 l2` | The alternate set exists so `EX AF,AF'` / `EXX` in a handler are real exchanges and visible to a comparison. `f` is the packed flag byte. No WZ/MEMPTR and no `r` counting. HALT is `nmd_z80_wait_until`. |

Routines are `void (*)(void)` over that state and the bus calls. Wrappers that
carry registers belong at real boundaries (vectors, computed entries,
restarts); translated code that only shuffles a register file is an
intermediate result (shared execution contract).

No retail title's requirements were available in this checkout
(`build/decomp/<game-id>/` is absent), so these additions are justified from
the hardware and the reference snapshot layout in [abi-v1.md](abi-v1.md);
whether the first title needs more (trace mode, other exceptions, TRAP, Z80
NMI, IM 0 data-bus opcodes) is a D03/D06 finding.

## Observation path

| What | How | Timed bus access? |
|---|---|---|
| Work RAM (65,536 bytes) | `nmd_work_ram()`; the script's `dump NAME` writes `NAME.wram` | No |
| Z80 RAM (8,192 bytes) | `nmd_z80_ram()`; `dump NAME` writes `NAME.zram` | No |
| CPU registers | `nmd_cpu`, `nmd_z80` globals | No |
| Picture, sound | the kit's `shot NAME` and `--sound FILE` | No |
| VRAM, CRAM, VSRAM, sprite cache, YM/PSG register state, I/O port state | **not exported**; internal accessors only (`md_vdp_vram_byte` and friends) | D05 gap |

Observation never injects state: the pointers are `const` and the dump runs
between frames. `rd8`/`wr8` from the host stack still work in native mode but
advance the clock; a checker must not use them.

## Evidence and limits

`mise run test:md-execution` (14 cases, 10 fatal targets, 9 planted-error
detections across three planted runtime errors: skipped reset entry, state
lost across a yield, interrupt return ignoring the guest stack) and
`mise run test:md-consumer` (`md_native`: an authored cart through `nmd_main`
with scripted input, dumps, shots and sound, two identical runs, three planted
cart errors caught). Both are authored synthetic controls. They do not compare
against the reference core, do not qualify hardware accuracy, and do not make
the runtime ready for a retail game: reference-backed comparison of native
runs is D05/D06.
