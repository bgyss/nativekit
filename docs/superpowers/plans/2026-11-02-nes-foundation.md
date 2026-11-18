# NES foundation implementation plan

> **Moved 2026-11-09.** This was written for the standalone prototype. The library is now
> `consoles/nes/`, its tools `consoles/nes/tools/`, and its game contract
> `consoles/nes/docs/contract.md`; paths below are the prototype's (`platforms/nes`, `tools/`).

**Goal:** Start an independently buildable native NES / Famicom library here.

**Architecture:** Isolated C11 `platforms/nes` library with opaque console instances.
Hardware bus access and native dispatch are explicit public operations.

**Spec:** [Foundation design](../specs/2026-11-02-nes-foundation-design.md).

## Tasks

- [x] Write public API and synthetic contract tests; observe failure before implementation.
- [x] Implement RAM, standard controller serial protocol, cartridge routing and dispatch.
- [x] Build standalone, run CTest with sanitizers, and build existing SNES targets.
- [x] Add usage and limitations; review diff and dependency boundaries.

## Decisions

Proceed within the user's explicit authorization to start implementation. Use the
existing worktree and execute inline. No merge, publication, or external dependency
is needed. Controller electrical behavior and timing remain explicitly deferred.

## Verification

Apple Clang Debug with address/undefined-behavior sanitizers and Release builds
passed both CTests. The existing null-host/software SNES library, test targets
and example built successfully. Clang static analysis and local Markdown links
passed. Independent review found no actionable correctness defects; added
startup failure/ownership coverage and clarified nested native calls afterward.
The default Nix GCC failed its SDK compiler probe; no compiler configuration in
the repository was changed. Hardware oracle comparisons have not run.

## Follow-up: NES machine helpers

The next requested slice added `nnes_cpu_state`, `nnes_cpu`, and
`platforms/nes/include/nnes/machine.h`. The implementation provides explicit
error-returning byte/word bus access, zero-page indirect addressing, per-console
6502 registers, N/Z updates, and stack byte/word push/pull helpers. The native
cartridge example demonstrates indirect-Y page-cross access, a relative branch,
load/ANC/INC status results, a discarded normal read, a composite SLO RMW pair,
and dispatch through an original routine address, an indirect JMP pointer with
zero-page wrap, and an RTS-based computed jump.
The header also provides P flag set/clear, load N/Z, ADC/SBC/AND/ORA/EOR/
CMP/BIT and shift/rotate results, plus stable undocumented ANC/ALR/ARR/AXS,
LAX/LAS/SAX, and combined RMW operations SLO/RLA/SRE/RRA/DCP/ISC.
`nnes_alu8_commit` applies an ALU
result and its P update together. Branch-condition/target helpers model taken
branch dummy cycles; indexed data helpers model page-cross and zero-page dummy
reads, the indirect-JMP wrap, and byte RMW pairs enforce adjacency. Dispatch
has call, tail-jump, and
pre-pushed-byte forms and uses sorted binary-search lookup. Explicit NMI/IRQ
entry reads cartridge vectors, honors IRQ masking, invokes native handlers, and
restores through the interrupt stack frame. Byte RMW writes send the old value
and then the modified value, with other bus operations rejected between them.
Explicit dummy reads/writes preserve bus side effects and base cycles without
native-work profile charges; an RMW's old-value write uses dummy semantics.
`nnes_rd8_discard` preserves ordinary read accounting and side effects when the
decompiled instruction does not retain the value.
Complete `($zp,X)` and `($zp),Y` byte helpers compose pointer resolution with
the operation-specific dummy accesses, while low-level resolvers remain
available when decompiled code reuses an effective address or needs its
page-cross result. Absolute,X and absolute,Y byte helpers also compose address
calculation with read/write dummy-cycle sequencing. The indexed RMW wrapper
returns the resolved address for the matching write, preserving the adjacent
read/write pair contract. `nnes_jump_absolute` updates PC and dispatches an
absolute JMP target; `nnes_jump_indirect` combines NMOS pointer wrapping with
that native tail dispatch. `nnes_jump_rts` handles stack-computed jumps that
push target-1 and finish with RTS.
Indirect-X and indirect-Y RMW helpers compose pointer resolution and required
dummy reads, returning the resolved address for the matching write.
Zero-page indexed RMW has the same resolved-address variant.
Direct zero-page indexed and 16-bit indexed data helpers now emit the dummy
reads required by their read/write/page-crossing form; indexed RMW helpers emit
the unconditional addressing dummy read before opening the RMW pair.
The trace normalizer removes tagged dummy cycles so fitted costs do not absorb
bus work that the machine helpers model directly.
Dispatch remains native C and does not simulate surrounding JSR/RTS stack effects. This follow-up was
compiled, but its tests were not run or expanded.

## Follow-up: reset, interrupt, and restart boundaries

The machine contract now counts each modeled CPU bus access, including seven
reset bus cycles and the seven cycles of explicit IRQ/NMI/BRK entry. Pull
instructions model the next-PC and dummy stack-page reads before incrementing
S. `nnes_dummy_next_pc` exposes the implied-instruction dummy read to other
native translations. RTS also models its final return-address dummy read and
PC increment for stack-computed jumps. BRK pushes its two-byte resume PC and B bit; native
handlers restore through `nnes_rti`.
Public `nnes_rti` and interrupt return share one stack transition, with
profiled accesses enabled only for the native game-side helper.
Explicit interrupt entry is rejected from cartridge bus callbacks and open RMW
pairs, preserving operation ordering; two-byte bus helpers therefore cannot
be split by the current explicit-only interrupt mechanism.
`nnes_cpu_restart` uses the `nnes_start` boundary to abandon the active native
routine chain, set S to `$FF`, and dispatch another registered entry. It rejects
calls outside a native routine or from inside a cartridge callback. The library
and example were compiled and Clang static analysis passed; tests were not run.


## Follow-up: native wait and CPU-cycle accounting

Added a total CPU-cycle counter alongside represented bus cycles, an explicit
`nnes_charge_cpu_cycles` API for native C work, and `nnes_wait_until` with a
ready callback and required per-turn cycle charge. Bus reads/writes count as one
cycle, and wait-body charges advance only the CPU counter. The helper currently
does not pump PPU/APU or deliver scheduled events. Library/example compile and
Clang static analysis pass; tests were not run.

## Follow-up: NES CPU cost profiles

Added a versioned per-read/write profile measured in tenths of a CPU cycle,
classified by WRAM, I/O, cartridge RAM, and ROM. Cartridge descriptors copy the
profile; an optional pure address-kind callback distinguishes mapper RAM from
PRG ROM, defaulting to ROM. Access costs are enabled by default and can be
switched off per console; the base bus cycle and explicit charges remain. The
synthetic example includes illustrative weights. No profiler currently extracts
game weights. Build and static analysis are run separately; tests were not run.

A follow-up separated console-owned reset/interrupt bus accesses from game C
accesses: both advance the bus and total CPU counters, but only game-side accesses
receive the cartridge's native-work profile weights. Cost profiles are copied,
version-checked, and classified by a pure cartridge address-kind callback; costs
start enabled and may be disabled per console. Build and static analysis pass;
tests were not run.

Per-kind native-C read/write counts can now be snapshotted and reset between
capture windows. Console-owned reset/interrupt accesses bypass profile weights;
wait-poll reads retain weights but are omitted from profile counts. Both still
advance the total bus-cycle counter. The API refuses to split an open RMW pair so
the old-value and new-value writes remain in one window.

## Follow-up: cost-profile trace fitter

Added `tools/nnes-profile fit`, which validates versioned data-only trace windows,
fits non-negative read/write weights by memory kind, and writes the C profile,
JSON record, and fit report. The normalized trace contract excludes poll, opcode,
operand, and dummy accesses, removes tagged DMA bus-access cycles, and records
that untagged DMA stall cycles may remain. Python syntax and Markdown checks
passed; no tests were run.

## Follow-up: typed reference-trace normalization

`tools/nnes-profile normalize` now filters a versioned raw CPU-operation trace
into the fitter's data-only window format. It requires operation-type labels and
timestamps on a cycle counter that includes DMA stalls, removes cycles from
tagged DMA bus operations, filters repeated same-address poll reads and
oversized gaps, then emits explicit DMA timing caveats with every profile.

An optional Mesen2 source patch adds the operation type as the third Lua memory
callback argument and exposes the enum to scripts. The Lua collector writes raw
NES operation traces for `normalize`. Its patch hunks were checked against the
current Mesen2 master source, but the patch has not been applied to or built
with Mesen2; the collector has not been exercised in a live emulator session
and no ROM trace/profile has been generated. The `nnes` library itself has no
Mesen dependency. No tests were run.
