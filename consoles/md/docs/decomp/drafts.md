# Draft C from the original's instructions (D06/D07, T2)

`draft68k.py` writes C for 68000 routines from their decoded instructions, so
a title's whole code map can be carried into C at once and then held, routine
by routine, by the [per-call check](check-calls.md) and [lockstep](lockstep.md).
The drafts are machine-shaped on purpose: one statement group per original
instruction over `nmd_cpu` and the bus calls, flags through `nmd_decomp.h`,
the original address as a label where a branch lands. They are the T2 "draft"
of the SNES method, never a certification: passing those checks is their only
evidence, and a person rewrites them as plain typed C (D09).

```sh
python3 consoles/md/tools/decomp/draft68k.py --rom ROM --codemap codemap.json \
    --out drafts.c [--have ADDRESS=name,...] [--table NAME] [--standalone] ADDRESS...
```

## What a draft is

- **Body.** What the entry reaches by fall-through and branches, laid out in
  address order; when code below the entry belongs to it, the draft starts
  with a jump to the entry.
- **Calls.** BSR/JSR call the other routine's draft or hand-written C with
  `NMD_JSR` (return address pushed on the guest stack); a computed JSR, or a
  call to an address with no C, goes through `jsr_address`. A branch or jump
  into another routine's entry is a tail call: its C runs and its RTS is this
  routine's. A computed JMP inside the routine switches over the code map's
  jump-table targets; any other address goes through `jmp_address`. A
  `JMP (base,PC,Dn)` whose one target starts a straight run of instructions
  ending in a transfer is an unrolled loop entered part way (Duff's device):
  the run is drafted into the routine itself and the switch has a label for
  every instruction of it (first title: two copy loops of 160 and 256 moves,
  entered by count; with only the run's start the C diverged once the count
  changed, and natively stopped at `nmd_restart`).
- **TRAP** calls `trap_call(number, return_pc)`: the exception frame pushed,
  the vector's C run, the frame popped.
- **Bus order** is the 68000's: a long is two word transfers, high word
  first; a read-modify-write reads then writes; CLR does not read, as the
  pinned core.
- **Refused**, with the reason listed and no C written: TRAPV, CHK, STOP,
  RESET, ILLEGAL, the BCD and X-extended arithmetic, MOVEP, ROXL/ROXR, RTR, CMPM.

`--have` names routines that already have hand-written C; they are called,
never drafted. `--standalone` also exports the game's table and the
call-by-address helpers, for controls.

## Evidence

`draft_check.py`: every 68000 routine of the per-call control cartridge
drafted; the drafts reach the hand-written C's verdict on every routine (17
IDENTICAL on 58 calls), and three planted generator errors (a draft that does
not start at its entry when code lies below it, a conditional tail call
whose return is outside the condition, a branch condition read the wrong
way) make a draft DIVERGENT.

First title (its private repository): 278 routines entered by a call or a jump
table and 4 interrupt handlers drafted, none refused. Over the 600-frame boot
scenario, the 131 routines reached are all IDENTICAL on every judged call
(94,577 of 94,702 calls). Over the 3,000-frame gameplay scenario (options
screen and first-stage play): 219 routines reached, 214 IDENTICAL, none
DIVERGENT, 488,710 of 488,918 calls identical; the 5 UNVERIFIED are calls a
TRAP into the music engine or a wait on memory the vertical interrupt
changes keeps from being judged. Three generator defects were found on the
title, all fixed and pinned by draft_check: a draft did not start at its
entry when code lay below it; a conditional tail call returned whatever the
condition; and a loop waiting on memory an interrupt changes spun for ever in
a replay (the replay now stops a call after four times the original's
accesses plus 4,096, as a difference).

Lockstep with the drafts over the boot scenario: 350 of 600 frames equal with
14,738 calls substituted; at frame 351 the CPU regions (registers and clocks)
differ while RAM and VDP memory do not, the open timing class of
[lockstep.md](lockstep.md#first-title).

## Limits

- Drafts translate what the decoder decodes; code no trace or table reaches
  is not drafted.
- 68000 only; the Z80 programs still need hand-written C.
- Routines that end in RTE are drafted for the native build only: the
  checker and lockstep do not judge interrupt handlers.
