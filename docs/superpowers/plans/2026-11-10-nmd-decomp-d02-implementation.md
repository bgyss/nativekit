# nativemd D02 Native Reset and Execution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task by task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Give a game a supported `nmd_main` path that enters its native reset routine and resumes its 68000 main logic each frame beside the Z80 and hardware.

**Architecture:** One appended opt-in cartridge field, `nmd_cart.execution`. Zero keeps the 1.0 host-driven behavior bit for bit. `NMD_EXECUTION_NATIVE` runs the routine at the ROM's reset vector on a 68000 coroutine (the kit's `nk/coro.h`, as the SNES console and the MD Z80 already do); `nmd_run_frame` switches to it and the first 68000 interrupt-check point at or after the vertical blank switches back. The same opt-in selects guest-stack-visible interrupt returns on both CPUs. No instruction execution is added anywhere.

**Tech Stack:** C11, kit coroutines and `nk/jump.h`, CMake, Python 3 check runners, `mise` tasks.

**Spec:** [D02 milestone contract](2026-11-04-nativemd-decomp/d02-native-reset-and-execution.md); [shared execution contract](2026-11-04-nativemd-decomp/README.md#shared-execution-contract).

**Execution method:** Native single-session execution, the shared execution contract's default.

**Outcome (2026-11-10):** executed. Deviations: Tasks 1-3 landed as one commit because their `dispatch.c` changes interleave; the checks were written with the runtime, so "verify it fails" is met by the planted-error controls (`md_m68k_plant` in test builds, `NMD_NATIVE_PLANT` in the cart) rather than by red runs; `nmd_work_ram`/`nmd_z80_ram` moved from Task 4 to Task 1 so the unit check could observe without bus accesses. Results are in the [milestone file](2026-11-04-nativemd-decomp/d02-native-reset-and-execution.md#results).

## Refreshed Baseline

- Branch `nativemd`, HEAD `4496637` (clean) at plan creation; D00 and D01 complete.
- No `build/decomp/<game-id>/` workspace is present in this checkout, so the selected title's private facts are unavailable. Every "needed by the title" decision below is justified from the hardware and marked title-unverified.

## Audit (D02 work items 1, 3, 4)

| Area | Finding at `4496637` | Decision |
|---|---|---|
| `nmd_power` (`md_console.c`, `bus.c`) | Zeroes `nmd_cpu`, sets `sr = $2700`; never reads the vectors at `$000000/$000004`. | Native mode loads SSP and PC from the ROM and requires a C routine at the reset PC. |
| `nmd_run_frame` | `md_advance_to(next vblank)` then dispatches IRQs from the caller's stack. No 68000 main exists. | Native mode switches to the 68000 coroutine instead. |
| SNES path (`consoles/snes/src/console/dispatch.c`) | `cpu_main` on a coroutine, reset vector looked up, `cpu_restart` drops native frames, a returning top-level routine is fatal. | Same shape for MD. |
| 68000 interrupt (`take_interrupt`) | Pushes PC and SR to the guest stack, but returns by restoring the whole saved `nmd_cpu`: handler edits of the stacked frame and of D/A registers are discarded, stack reads of RTE are never made, and `irq_delivering` forbids all nesting. | Native mode returns by popping SR and PC from the guest stack, restores nothing else, nests by SR mask. Host mode unchanged. |
| 68000 state | D0-D7, A0-A7, PC, SR. No inactive stack pointer, so a user/supervisor switch loses a stack. | Append `usp`, `ssp`; add `nmd_set_sr`. |
| Stacked PC | `nmd_cpu.pc` is only what C last stored; it is not the original's return address. | Documented limit: the frame's PC bytes are the game's own value; a return is "elsewhere" when the popped PC differs from the pushed one. |
| Z80 interrupt (`deliver_vint`) | Pushes nothing to the guest stack; restores PC and both IFFs after the handler. | Native mode pushes PC, pops it on return, leaves IFFs to the handler (EI). Host mode unchanged. |
| Z80 state | No alternate register set. | Append `a2 f2 b2 c2 d2 e2 h2 l2` (EX AF,AF' / EXX). |
| Observation | `nmd_console` has no `regions`; scripts' `dump` writes nothing. | Work RAM and Z80 RAM as kit regions; untimed, read-only. VRAM/CRAM/VSRAM stay an explicit D05 gap. |

## Global Constraints

- No 68000/Z80 interpreter, guest JIT, instruction dispatch loop, or title-specific behavior.
- A zero-initialized `nmd_cart.execution` must behave exactly as 1.0: `test:md-interrupt`, `test:md-z80`, `test:md-bus`, `test:md-io`, `test:md-rom`, `golden:md-compare` and the existing consumer keep passing unchanged (the consumer's version assert moves to the new minor).
- Additions only, appended: contract version becomes **1.1.0** (`version.h`, `CMakeLists.txt`, `contract.md`).
- C11, four-space indentation, snake_case, function braces on their own line, `nmd_` public prefix, `md_` internal; nothing console-specific in `kit/`.
- Every unsupported or missing target is `nk_fatal` (exit status 70), never a guess.
- One thread, deterministic schedule; identical runs produce identical dumps.
- Each new gate is shown to fail on a planted error before it is relied on.
- Fixtures are authored synthetic C; no ROM bytes or recovered code.
- Do not mark D02 complete unless every acceptance criterion holds; synthetic execution does not make the runtime game-ready.

## Review Focus

- A game's main that never reaches a bus access or wait cannot yield: documented, not detected (no instruction clock exists to detect it).
- `nmd_power` again while the 68000 coroutine is suspended deep inside an interrupt handler must abandon that stack cleanly: Task 1 `repower` case.
- An interrupt handler that leaves a different return PC, or a corrupted SR, on the guest stack: Task 2 `return_elsewhere` and `bad_return` cases.
- `nmd_run_frame`, `nmd_power` or `nmd_restart` called from the wrong stack (the game's own, or the host's): Task 1/2 fatal cases.
- A host-mode cart whose reset vector happens to name a routine must not start running it: Task 1 `host_mode_untouched` case.

---

### Task 1: Reset entry and frame resume

**Files:**
- Modify: `consoles/md/include/nmd/cart.h`, `consoles/md/include/nmd/version.h`, `consoles/md/CMakeLists.txt` (version), `consoles/md/src/console/dispatch.c`, `consoles/md/src/console/md_internal.h`, `consoles/md/src/front/md_console.c`
- Create: `consoles/md/tests/unit/execution_check.c`, `consoles/md/tests/execution_check.py`
- Modify: `consoles/md/tests/CMakeLists.txt`, `mise.toml` (`test:md-execution`), `consoles/md/tests/consumer/main.c` (version assert only)

**Interfaces produced:**
- `typedef enum { NMD_EXECUTION_HOST = 0, NMD_EXECUTION_NATIVE = 1 } nmd_execution;` and `nmd_execution execution;` appended to `nmd_cart`.
- Internal: `void md_m68k_power(const nmd_cart *cart);` (abandons any suspended run, validates and loads the reset state), `void md_m68k_run_frame(void);`, `int md_m68k_native(void);`.
- Fatal texts: `reset vector $%06X has no C routine`, `the 68000 routine at $%06X returned to no caller`, `nmd_run_frame from the game's 68000 routine`, `nmd_power from the game's 68000 routine`, `native execution needs a ROM with reset vectors`.

- [x] **Step 1:** Write `execution_check.c` with argv-selected cases (`main` runs the passing ones; fatal cases are selected by name and run by `execution_check.py`, which expects exit 70 and the message): `boots` (reset routine observed with `a7` = ROM SSP, `pc` = ROM reset PC, `sr == 0x2700`), `resumes` (main increments a C local and a work-RAM word once per `nmd_wait_vblank`; after 5 `nmd_run_frame` calls the word is 5 and the local survived), `host_mode_untouched` (same ROM and table, `execution` zero: routine never called), `repower` (power, 3 frames, power, 3 frames: equal clocks and RAM digests both times, including a power while suspended in a handler), fatals `missing_reset`, `returns`, `run_frame_inside`, `power_inside`, `short_rom`.
- [x] **Step 2:** Build; verify the check fails to compile/run for the missing field.
- [x] **Step 3:** Implement. The coroutine's top is a `port_setjmp` loop; the yield point is the top of `md_irq_dispatch_pending` after the defer-depth test: when on the 68000 coroutine and `md_clock >= frame_target`, switch to the host; on resume, if abandon was requested, `port_longjmp` to the top, which parks. Vectors are read from `cart->rom` directly (untimed). Coroutine stack 16 MiB, created once.
- [x] **Step 4:** `mise run test:md-execution` passes; `mise run test:md-interrupt test:md-z80 test:md-bus test:md-io test:md-console` still pass.
- [x] **Step 5:** Commit `nativemd: enter the reset routine on a 68000 coroutine`.

### Task 2: Guest-visible 68000 interrupt return, stack modes, restart

**Files:** `consoles/md/include/nmd/machine.h`, `consoles/md/src/console/dispatch.c`, `execution_check.c`, `execution_check.py`

**Interfaces produced:**
- `nmd_cpu_state` gains trailing `uint32_t usp; uint32_t ssp;` (the inactive stack pointer of each mode).
- `void nmd_set_sr(uint16_t sr);` swaps `a[7]` on an S-bit change, then makes the interrupt check.
- `_Noreturn void nmd_restart(uint32_t addr);` native mode, 68000 coroutine only, not inside an indivisible operation: drops every native frame and enters the table's routine at `addr`.
- Fatals: `interrupt handler returned to $%06X with no C routine`, `nmd_restart to $%06X with no C routine`, `nmd_restart outside the game's 68000 routine`.

- [x] **Step 1:** Add cases: `irq_runs` (VINT handler runs during main's wait, counts in RAM), `irq_keeps_registers` (handler changes `d[0]`; main sees the change; host-mode twin sees it restored), `irq_frame` (handler reads its frame with `rd16/rd32` at `a[7]`: pushed SR and PC; after return `a[7]` is back), `irq_sr_edit` (handler rewrites the stacked SR's mask to 7; main then sees mask 7), `irq_nests` (level-4 handler waits for a vblank; level 6 runs inside it; order 4-in, 6, 4-out), `irq_user_mode` (main in user mode via `nmd_set_sr`: handler runs on `ssp`, main's `a[7]` and `usp` intact after), `return_elsewhere` (handler rewrites stacked PC to another routine: that routine runs, main's frame is gone), `restart` (deep C call restarts at a routine), fatals `bad_return`, `restart_missing`, `restart_host`.
- [x] **Step 2:** Run; verify the new cases fail.
- [x] **Step 3:** Implement in `take_interrupt` behind `md_m68k_native()`; host mode keeps the saved-state restore and the `irq_delivering` gate.
- [x] **Step 4:** `mise run test:md-execution test:md-interrupt` pass.
- [x] **Step 5:** Commit `nativemd: return from 68000 interrupts through the guest stack`.

### Task 3: Z80 ABI state and interrupt return

**Files:** `consoles/md/include/nmd/z80.h`, `consoles/md/src/z80/z80.c`, `execution_check.c`

**Interfaces produced:** `nmd_z80_state` gains trailing `uint8_t a2, f2, b2, c2, d2, e2, h2, l2;`. Native mode: VINT acceptance clears both IFFs, pushes PC high then low with `zwr` below `sp`, runs the vector's routine, pops PC with `zrd`; a popped PC that differs restarts the Z80 at that routine (missing: the existing `Z80 entry $%04X has no C routine`); IFFs are left as the handler set them.

- [x] **Step 1:** Add cases: `z80_runs` (Z80 routine at `$0000` started after the 68000 releases reset, writes a counter to Z80 RAM across frames), `z80_vint_frame` (IM 1 handler at `$0038` finds the pushed PC at `sp`, `sp` restored after; without EI no second VINT, with EI one per frame), `z80_alt` (alternate set survives a handler), and fatal `z80_missing_vint`.
- [x] **Step 2:** Run; verify failure.
- [x] **Step 3:** Implement behind `active_cart->execution == NMD_EXECUTION_NATIVE`.
- [x] **Step 4:** `mise run test:md-execution test:md-z80` pass.
- [x] **Step 5:** Commit `nativemd: Z80 alternate registers and stacked interrupt return`.

### Task 4: Observation path and the native cartridge through `nmd_main`

**Files:**
- Modify: `consoles/md/include/nmd/nmd.h`, `consoles/md/src/console/bus.c`, `consoles/md/src/front/md_console.c`
- Create: `consoles/md/tests/consumer/native.c`, modify `consoles/md/tests/consumer/CMakeLists.txt`, `consoles/md/tests/consumer/run.py`

**Interfaces produced:** `const uint8_t *nmd_work_ram(void);` (65,536 bytes), `const uint8_t *nmd_z80_ram(void);` (8,192 bytes); console regions `.wram`, `.zram` for the script's `dump`.

- [x] **Step 1:** Extend `run.py`: build `md_native`, run it headless with a script that holds different buttons over time and dumps at three points, and assert from the dumps: boot marker, frame counter equal to frames run, the per-frame pad history equal to the script's buttons, VINT count, Z80 command/acknowledge counters advancing, PSG writes made by the Z80; a second run's dumps are byte-identical; the `--sound` capture is not silent. Then three planted runs (`NMD_NATIVE_PLANT=skip-reset|lose-state|bad-return`) must each be reported as failing by the same assertions (or by exit 70 with the expected message).
- [x] **Step 2:** Run; verify it fails without the cartridge.
- [x] **Step 3:** Write `native.c`: authored reset routine (stack, pad setup, Z80 load/release through BUSREQ/RESET), main loop over `nmd_wait_vblank`, VINT handler reading the pad with the TH protocol, Z80 routine at `$0000` with an IM 1 handler, all as C over the public API. Add the two accessors and regions.
- [x] **Step 4:** `mise run test:md-consumer` passes; the unchanged `md_consumer` still runs 5 idle frames.
- [x] **Step 5:** Commit `nativemd: a native cartridge through nmd_main, observed by dumps`.

### Task 5: Contract, documents, full validation

**Files:** `consoles/md/docs/contract.md`, create `consoles/md/docs/decomp/native-execution.md`, `consoles/md/docs/nativemd.md`, `consoles/md/docs/decomp/workflow-parity.md`, `consoles/md/tests/README.md`, `AGENTS.md`, `docs/superpowers/plans/2026-11-04-nativemd-decomp/README.md`, `.../d02-native-reset-and-execution.md`

- [x] **Step 1:** Write the execution contract (entry, yield rule, waits, stop, restart, re-power, interrupt and stack semantics for both CPUs, ABI boundary, observation path and its gaps, fatal list, version decision 1.1.0, evidence limits).
- [x] **Step 2:** Run `mise run test:md-console test:md-interrupt test:md-io test:md-z80 test:md-bus test:md-consumer test:md-execution test:kit`, plus `test:md-rom`, `golden:md-compare` if a baseline exists, and `git diff --check`; record commands and exit statuses in the milestone file.
- [x] **Step 3:** Set D02 status from the evidence (complete only if every acceptance line holds) and commit `Docs: D02 native execution contract and evidence`.
