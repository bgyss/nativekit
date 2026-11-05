# NMDH v1 Reference Execution ABI

NMDH v1 is the test-only instruction/state and controlled-execution interface
for the pinned BlastEm build. It is separate from the existing NMDT v2 access
trace. NMDT event IDs, payloads, timestamps, files, and consumers stay
unchanged.

## C event ABI

The public header and implementation are
[`decomp_abi.h`](../../tests/oracle/decomp_abi.h) and
[`decomp_abi.c`](../../tests/oracle/decomp_abi.c). They are copied only into
the candidate test-core build. All NMDH integers are unsigned, fixed-width
values; `struct_size` must equal the v1 C event structure size. The callback is
synchronous and receives a copied event with inline payload storage. It must
copy any values it needs beyond that call. No pointer into a mutable CPU or
machine context is included in an event.

The ABI version is `1`. CPU IDs are `1` for the 68000 and `2` for the Z80.
Event IDs are:

| ID | Event |
|---:|---|
| 1 | `INSTRUCTION` |
| 2 | `FLOW_EDGE` |
| 3 | `BUS_ACCESS` |
| 4 | `RESET` |
| 5 | `EXCEPTION` |
| 6 | `INTERRUPT` |
| 7 | `STOP` |
| 8 | `REPLACE` |

Each event begins with `struct_size`, kind, CPU, flags, sequence, common
master-clock value, epoch, current PC and next PC. The 256-byte inline payload
union holds the relevant CPU snapshot or typed bus, flow, interrupt, or stop
data. The instruction payload has capacity for up to 16 fetched bytes, the
leading opcode word and the matching CPU state snapshot. The current 68000 path
reports its actual prefetched opcode word; extension-word provenance is not
inferred from a bus access. An instruction event describes the just-completed
instruction: its `pc` and fetched bytes identify the pre-instruction address
and encoding, while `next_pc` and the snapshot reflect the post-instruction
boundary before another guest instruction. Instruction snapshots include the
CPU hidden/prefetch and pending interrupt fields needed by the execution
boundary. The pinned context fields and their semantics are audited against
live synthetic execution before a capability is claimed. The Z80 computed-goto hook publishes the preceding instruction when the next opcode has been fetched, but before that opcode body executes; its snapshot includes the fetch-side PC/R changes.

For Z80 state, `main` and `alt` use the pinned context order `[B,C,D,E,H,L,F,A]`.
Flag fields are the candidate core's raw representations, not a packed F byte:
`zflag`, `pvflag`, `nflag`, `chflags` and `last_flag_result` are preserved as
stored so replay checks can compare the exact internal state.

The callback returns `NMD_DECOMP_CONTINUE` (`0`), `NMD_DECOMP_STOP` (`1`) or
`NMD_DECOMP_REPLACE` (`2`). STOP and REPLACE are valid only on an
`INSTRUCTION` event; the core rejects any other action as a trace error and
fails closed with STOP. REPLACE is only a synchronized decision-point handoff.
It does not mutate CPU, memory, or clock state and does not resume reference
execution. C-side effects and resumption are later D04/D05 work.

Reset, exception and interrupt events use the shared interrupt payload:
`vector`, `reason`, `phase` (`1=entry`, `2=return`) and the stack pointer.
Both CPUs report reset and interrupt entry/return. The M68K also reports
exception entry/return, including software traps. The Z80 has no architected
exception-entry path, so no Z80 `EXCEPTION` event is emitted.

The `STOP` and `REPLACE` event payload is a complete handoff record. It contains
the requested stop criterion and value, action reason, triggering CPU's
instruction count, synchronized master clock, copies of both CPU snapshots,
and the VDP, YM, PSG and IO scheduler clocks. The CPU-local `cycles` fields
remain in their own units; `z80_master_clock` is the Z80 cycle converted to
the common master clock. The event's master clock and `stop_clock` name the
same scheduler boundary. Criterion IDs are 1=PC, 2=return sentinel,
3=interrupt, and 4=instruction budget; frame limit is a natural post-frame
return and is recorded by the runner without a CPU callback action.

The ABI latches the first stop action and its CPU/clock. The generated CPU
loops allow the other CPU to finish up to the requested boundary, then stop
both cores before another instruction. The Genesis scheduler advances the
devices to a stable common clock, copies both CPU states, and publishes the
handoff event before `retro_run` returns. A completed REPLACE handoff stays
latched; D01 has no resume operation.

`nmd_decomp_error()` is sticky until `nmd_decomp_reset()`. Invalid event sizes,
event/CPU IDs, callback actions, or sequence overflow set it. A runner must
reject the associated capture. Only one callback may be registered per run;
a second registration is rejected until reset. ABI version mismatch and null
callback registration are rejected without changing the current registration.

## Capability bits

`nmd_decomp_capabilities()` advertises only compiled and exercised paths. Bits
are independent and may be combined:

| Bit | Capability |
|---:|---|
| `1 << 0` | 68000 instruction boundaries |
| `1 << 1` | 68000 state snapshots |
| `1 << 2` | Z80 instruction boundaries |
| `1 << 3` | Z80 state snapshots |
| `1 << 4` | Dynamic flow edges |
| `1 << 5` | Reset and interrupt entry/return for both CPUs; M68K exception entry/return |
| `1 << 6` | Observed instruction opcode/prefetch bytes marked as code fetch; extension-word provenance is not claimed by this bit |
| `1 << 7` | CPU bus start/completion intervals |
| `1 << 8` | Shared master-clock epoch |
| `1 << 9` | Synchronized machine checkpoint |
| `1 << 10` | Controlled stop |
| `1 << 11` | REPLACE handoff |

These bits do not claim idle-cycle or per-microcycle coverage. Those details
remain explicit gaps unless separate synthetic controls prove them.

## Callback state and safe points

Sequence numbers start at one for each reset capture and increase monotonically
across both CPUs. Clock and epoch values use the synchronized Genesis master
clock. A STOP/REPLACE action is honored after the current instruction and
before the next guest instruction; the scheduler must then synchronize both
CPUs, VDP, sound and IO before returning to the runner. The returned snapshot
includes the triggering CPU's immutable instruction event and both machine
CPU states at the synchronized handoff clock. The other CPU may make
pre-boundary progress while it catches up. STOP and REPLACE on the same
scenario/trigger produce identical machine snapshots.

Libretro checkpoint capture/restore is permitted only after a completed frame
or a synchronized hook stop. No save occurs during a callback or between a
bus access start and completion. Restoring to replay a checkpoint uses normal
reference execution; that does not resume a REPLACE decision point.

## NMDH v1 file envelope

The file uses little-endian integers. The 12-byte fixed header is `NMDH`,
u16 version (`1`), u16 flags (`0`), and u32 metadata length, followed by at
most 16,384 bytes of canonical UTF-8 JSON identity/capability metadata. Each
event record is a u32 length followed by at most 4,096 bytes of canonical
UTF-8 JSON. Event JSON has `struct_size`, `kind`, `cpu`, `flags`, `sequence`,
`master_clock`, `epoch`, `pc`, `next_pc` and an event-specific `payload`. The
48-byte footer is `NHEN`, u64 record count, u8 completion status, three zero
reserved bytes and SHA-256 of the complete header and records before the
footer.

Completion status `0` means complete. Status `1` is timeout, `2` storage
limit, `3` core error and `4` cancellation; these statuses are never accepted
as complete evidence. The reader rejects unsupported versions, bad lengths,
unknown events/capabilities, digest or count mismatches, nonzero reserved
bytes, missing/incomplete footers and trailing data. Missing footer means the
worker was interrupted or failed. D01 does not split a capture into chunks.

The metadata JSON contains `abi_version`, the 64-bit `capabilities` mask,
`identities` with `rom_sha256`, `scenario_sha256`, `core_sha256`,
`patch_sha256`, `abi_sha256` and `runner_sha256`, plus pinned core revision and
machine `options`. It never records local paths. The runner
scenario is version 1 and names region, model, both controller types, frame
inputs, required capabilities, and a CPU stop criterion/action. Estimate mode
captures a bounded three-frame sample and reports projected seconds and bytes
for the requested frame count. Capture mode requires an estimate with the same
ROM/scenario/core/patch/ABI/runner identities and frame count. The default
limits are 420 seconds and 268,435,456 bytes for the capture; a timeout or
storage overflow leaves a `.partial` file and an explicit incomplete status.
The scenario capability list must include both instruction and state bits for
its stop CPU. Non-frame stops also require controlled-stop; interrupt stops
require interrupt events; REPLACE requires the handoff bit. The runner also
keeps the existing NMDT v2 bus stream in a separate file; the NMDT format is
unchanged.

## Status

This interface and file format are under D01 implementation and synthetic
verification. Passing BlastEm tests qualify candidate-core consistency only;
they do not establish hardware accuracy or game compatibility.
