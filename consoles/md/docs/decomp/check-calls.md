# Per-call verification of C routines (D04)

The Mega Drive counterpart of the SNES playbook's per-call gate (A5): a
routine's C is run on the registers the original had when it entered the
routine, and must do what the original did, call by call. The original runs
untouched on the pinned reference core; the check works from its recorded
events. It has no timing: that is the lockstep gate (D05).

## Commands

```sh
python3 consoles/md/tools/decomp/hybrid.py --out DIR [--catalog catalog.json] \
    [--define NAME]... SOURCE.c...
python3 consoles/md/tools/decomp/check_calls.py --rom ROM --stream STREAM.nmdh \
    --library DIR/hybrid.dylib --out calls.json [--contracts contracts.json]
python3 consoles/md/tools/decomp/check_calls.py --rom ROM --scenario SCENARIO.json \
    --core CORE --frames N [--first-frame F] --library DIR/hybrid.dylib --out calls.json
```

The second form keeps no stream: it runs the scenario on the reference core
and checks each call as it returns, so a whole scenario can be checked
without a full-event capture (about 17 MB a frame for the first title). It
gives the stream's verdicts.

`STREAM.nmdh` is a full capture's `capture.nmdh` or a light capture's
`window.nmdh` ([intake.md](intake.md#light-captures-and-the-evidence-digest)):
the events of a few frames are enough to judge the calls made in them.
`check_calls.py` exits 1 when any routine is DIVERGENT.

## The C

A routine is `void name(void)` over `nmd_cpu`/`nmd_z80` and the nmd bus calls
(`rd8`…`wr32`, `zrd`, `zwr`), as a native cart's routines are
([native-execution.md](native-execution.md)). The source exports its tables
under fixed names, with the cart's own row types:

```c
const nmd_routine     nmd_game_routines[];     const size_t nmd_game_routine_count;
const nmd_z80_routine nmd_game_z80_routines[]; const size_t nmd_game_z80_routine_count;
```

[`nmd_decomp.h`](../../tools/decomp/include/nmd_decomp.h) is header-only over
`nmd/machine.h` and `nmd/z80.h`, so the same source builds for the checker and
for a native cart; the control's C is compiled both ways. It holds:

- Flag results for the operations in the table below. Each is held against
  the reference core by `ops_check.py`; nothing else is claimed.
- Call and return over the guest stack: `NMD_JSR(return_address, routine)`
  pushes the return address and runs the routine's C; `nmd_rts()` is a
  routine's last statement (it reads the return address into `nmd_cpu.pc`).
  `NMD_Z80_CALL` and `nmd_z80_ret()` are the Z80's.
- `NMD_JSR_ORIGINAL(return_address, address)`, in the hybrid only: call a
  routine that has no C yet. The record stands in for it.

| CPU | Helper | Operations |
|---|---|---|
| 68000 | `nmd_tst8/16/32` | MOVE, AND, OR, EOR, NOT, TST, CLR, MOVEQ results: N Z, V C clear (held with TST) |
| | `nmd_add`, `nmd_sub`, `nmd_cmp`, `nmd_neg` 8/16/32 | ADD, SUB, CMP, NEG to a data register |
| | `nmd_shl`, `nmd_shr` 8/16/32 | LSL, ASL, LSR, ASR by a register count |
| | `nmd_rol` 8/16/32 | ROL, ROR by a register count |
| | `nmd_swap`, `nmd_btst` | SWAP, BTST on a data register |
| | `nmd_divs` | DIVS.W by a nonzero divisor, overflow included; C kept (below) |
| | `nmd_dbf`, `nmd_set8`, `nmd_set16` | DBF's count and loop test; a byte or word into a data register (no flags) |
| Z80 | `nmd_z_add8`, `nmd_z_sub8`, `nmd_z_cp8` | ADD, ADC, SUB, SBC, CP |
| | `nmd_z_and8`, `nmd_z_or8` | AND, OR, XOR |
| | `nmd_z_inc8`, `nmd_z_dec8` | INC, DEC |

`hybrid.py` compiles the sources with `-DNMD_HYBRID -finstrument-functions`
and links them with [`check/replay.c`](../../tools/decomp/check/replay.c) in
place of the nmd runtime. With `--catalog` every table address must be a
routine entry of the intake's catalog for that CPU. It writes `hybrid.json`
(source, replay and library hashes, defines, routines). Nothing of the
library is part of a game.

## What a call is and what is compared

A call begins at an instruction at a table routine's address (or at the
target of a taken `bsr`/`jsr`/`call`/`rst`) and ends at the `rts`/`ret` that
leaves the stack pointer where the entry found it, plus the return address.
From the stream the checker takes the registers before the entry instruction,
every bus access of that CPU that is not an instruction fetch, the calls made
inside, and the registers after the return.

| | Rule |
|---|---|
| ROM | The C reads the image. |
| Work RAM, Z80 RAM | A byte holds its entry value if the original read it before writing it. A C read of any other byte before the C wrote it is a difference. At the end every byte the original wrote must hold the original's last value, and the C must have written no other byte. Order is not compared. |
| Dead stack | Bytes between the lowest stack pointer of the call and the stack pointer at the return are not compared. |
| Hardware (every other address) | The C's accesses must be the original's, in order, with the same address, width and direction; a write must carry the same value; a read returns what the original read. A 68000 longword is two word transfers. |
| Registers | 68000: D0-D7, A0-A7, SR (flags and system byte), the inactive stack pointer, and PC as the return address. Z80: A F B C D E H L, the alternate set, IX IY SP, I, IFF1, IFF2, IM, and PC. F is packed from the core's raw flag fields. R is not compared. A contract can list registers to ignore. |
| Calls | The table routines the C entered, in order, must be the routines the original called, in order, at every depth. |

Differences are looked for in that order after the replay itself: a replay
stopped by a hardware or memory difference, then calls, a hardware access the
C did not make, registers, memory. The first is reported with the call's
number and the stream sequence of its entry.

## Verdicts

| Verdict | Meaning |
|---|---|
| `IDENTICAL` | At least one call was judged and every judged call matched. |
| `DIVERGENT` | A judged call differed. |
| `UNVERIFIED` | It was called, but no call could be judged, or every call that differed was interrupted. |
| `UNCOVERED` | No call of it is in the stream. |

A call an interrupt or exception was taken in is judged without the
handler's accesses: they are set aside from the call's, as are the calls the
handler made, and its RAM writes are not the routine's. A value the routine
reads after the handler wrote it is the value the original read. If such a
call matches it counts as identical; if it differs it is counted as
`interrupt` and not as divergent, since the handler may be the cause (the
SNES playbook's rule).

A call is counted and not judged when:

- `foreign_write`: the 68000 wrote Z80 RAM during a Z80 call.
- `irregular_return`: it, or a call inside it, did not return by its own `rts`/`ret`.
- `unsupported`: the C used `nmd_wait_vblank`, `nmd_stop`, a restart or a Z80 port.
- `fallback`: the C called a routine with no C; the record's writes, hardware
  accesses and exit registers stood in for it. Fallbacks are counted.

`calls.json` (schema `nmd-calls` 1) holds the stream's identities (ROM,
scenario, core, patches, ABI, runner), the ROM and library SHA-256, the
window if the stream is one, the contracts, and for each routine its verdict,
calls, identical, divergent, unverified by reason, fallbacks, the counts
compared (registers, hardware accesses, memory bytes, calls) and the first
difference. The checker refuses a stream made from another ROM and a stream
whose capabilities lack bus accesses or interrupt events.

## Evidence

`calls_check.py`, on an authored cartridge captured on the pinned reference
core (4 frames) with authored C:

- 2,685 calls replayed, 2,620 of them the Z80 routine. Fourteen 68000
  routines and the Z80 routine are IDENTICAL, among them routines with
  hardware accesses (VDP, pad, PSG, a DMA set-up, an interrupt enable), a
  computed call through a pointer table, a routine that calls another twice,
  a routine whose loop branches back to its own entry (one call, not one per
  pass), and the pair that passes a byte from the 68000 to the Z80 through Z80 RAM.
- Ten planted errors in the C are each DIVERGENT for their reason: a sum
  without its flags (`sr`), a load from the wrong address (`d0`), a hardware
  write of another value, another handler called (call target), a return
  that does not read its address (`pc`), a store to the wrong address
  (missing write), two hardware accesses swapped, an interrupt mask left up
  (`sr`), half a DMA length and a silent PSG volume (hardware values).
- A call the vertical interrupt overlapped is IDENTICAL without the
  handler's accesses, and UNVERIFIED (`interrupt`) with a planted error in
  it. A routine that needs the original for a callee is UNVERIFIED and a
  routine never called is UNCOVERED.
- Three refusals: a table address outside the catalog, the same stream with
  its bus accesses left out, a stream of another ROM.
- A light capture's window of frames 2 to 4 judges 1,642 calls, all identical.
- The C compiles without `NMD_HYBRID` against nmd's headers.
- The same scenario run live gives the stream's verdicts, and a planted
  error is DIVERGENT in it.

`ops_check.py`: 17,920 68000 and 2,880 Z80 instructions of the reference
core, 45 operations over operand pairs with the flags set both ways; every
helper gives the core's result and flags, and two planted helper errors are
caught as the operations planted.

## Known reference difference

The pinned core leaves C unchanged on DIVS.W (680 of 680 cases in `ops_check.py`,
overflow or not); the 68000 manual says DIVS always clears C. `nmd_divs`
follows the core, since every check measures equality with it. A routine
whose behaviour depends on C right after a division would differ on hardware.

## Limits

- Authored synthetic controls only; no routine of a title has been checked.
- The reference is the candidate core, not hardware.
- No timing: that is D05, where calls an interrupt overlaps are still left
  to the original.
- Routines that end in `rte`, that never return, or that return by editing
  the stack are not judged.
- RAM access order is not compared, only RAM effects.
- The table rows are written by hand and checked against the catalog; they
  are not generated from it, and ROM copies of one routine (aliases) need a
  row each.
- Fallback is 68000 only.
