# Native NES / Famicom

An initial NES console library, `nnes`, for games recovered as native C,
following the SNES implementation's cartridge/console separation. It has no
dependency on the other consoles.

**Status: a hardware foundation, not yet a kit console.** `nnes` builds inside
nativekit and its contract checks run from the repository root, but it does not
yet fill the kit's console table (`nk_console`) or enter through the kit's main:
it has no picture, sound output or frame runner to hand the kit. Its state is
instance-owned (`nnes_console *`), unlike the other consoles' single machine;
the adapter to the kit is the next step once PPU rendering and a frame runner
exist. It came from a standalone prototype (`platforms/nes`), moved here
unchanged apart from paths.

## Build and run

From the repository root:

```sh
mise run test:nes          # the headless build, then every nnes contract check
```

or, the library alone, with no kit and no other console:

```sh
cmake -S consoles/nes -B build/nes -DNNES_EXAMPLES=ON
cmake --build build/nes
ctest --test-dir build/nes --output-on-failure
./build/nes/nnes_native_cart
```

Select a working C11 compiler with `-DCMAKE_C_COMPILER=...` if necessary. Tests
are enabled by default; disable them with `-DNNES_TESTS=OFF` for a library-only
build. Consumers link the `nnes` target and include `nnes/nes.h`.

## How a decompiled game plugs in

As with SNES, the game supplies native C routines indexed by original addresses,
cartridge data, and board-specific decoding. The console supplies hardware
behavior. `nnes_start` reads the little-endian reset vector at $FFFC/$FFFD from
the cartridge and dispatches the registered C routine. Ordinary flow stays C;
computed calls use `nnes_call`, while tail jumps use `nnes_jump_routine` or
`nnes_jump_routine_pushed` when values were left on the stack. Missing routines
are errors, with no interpreter fallback. Each routine receives its `nnes *`
console context and accesses that instance's register file through `nnes_cpu`;
`nnes_routine` entries now include a code identity as well as a CPU address.
The cartridge's pure `code_identity` callback resolves the code currently
mapped at that address, so two physical banks can register different native
routines at the same 16-bit CPU address. An absent callback selects the
unbanked identity zero. `nnes/machine.h` provides bus, status-flag, ALU-result, indexed-pointer,
branch-condition/target, and stack helpers. This keeps the SNES shape of
register state, explicit bus accesses, and address-based C dispatch while
allowing separate console instances. See the runnable
[synthetic cartridge](examples/native_cart.c).

The versioned CPU work profile and reference trace format are described in
[NES CPU cost profiles](docs/cost-profile.md). The repository includes a fitter and
an optional Mesen collector that requires a source patch to preserve CPU
operation types. The full collection/normalize/fit path has been exercised on
MesenCE source commit `9a05146d` for Dragon Warrior (USA) Rev A; that single
no-input profile does not establish general NES timing or game qualification.

This library does not perform decompilation. Recovering the NES program as C,
preserving its memory accesses and control flow, and supplying its data remain
the game project's job, just as they do for SNES. No commercial game code or
assets are included. The public API is provisional while hardware support grows.

## Implemented

- Instance-owned, deterministic zero-initialized 2 KiB RAM mirrored through $1FFF.
- Two standard joypads: live A reads with high strobe, falling-edge capture,
  eight serial buttons in A/B/Select/Start/Up/Down/Left/Right order, then ones.
  Standard-pad reads preserve CPU open-bus bits 7–5 and drive the lower pad
  lines; Famicom expansion devices, microphone/Zapper lines, and analog bus
  decay are not implemented.
- Cartridge read/write callbacks for $4020–$FFFF; errors propagate to the caller.
- CPU-visible PPU registers with $2000-$3FFF mirroring, the shared $2005/$2006
  latch, buffered PPUDATA reads, address increments, CHR mapper callbacks,
  nametable mirroring, palette aliases, and OAM backing. CPU time advances a
  basic NTSC vblank clock; pending PPU NMI is dispatched at wait-poll
  boundaries.
- OAM DMA from implemented CPU bus pages, using tagged DMA reads and PPU OAM
  writes. It copies 256 bytes and accounts for 513/514 CPU cycles. DMC DMA and
  DMA collision behavior remain unsupported.
- Partial APU hardware: the NTSC frame IRQ flag, `$4015` read/clear and
  open-bus bit, `$4017` IRQ inhibit/mode state, and CPU open-bus reads for
  write-only `$4000-$4014` registers. Channel length counters,
  pulse/triangle/noise/DMC units, sample generation/mixing, and CPU IRQ delivery
  remain unsupported.
- An optional operation-aware cartridge callback receives read/write/dummy,
  opcode/operand, the previous CPU data-bus latch, and CPU-time metadata. RMW
  old/new writes carry a shared pair flag; legacy read/write callbacks remain
  available.
- An optional read-only CPU-bus observer records completed internal-RAM, PPU,
  cartridge, and DMA accesses with operation labels and timestamps. It does not
  change emulation results.
- Native routine registration and startup through the cartridge reset vector;
  copied routine tables are sorted and dispatched by code identity and address.
- Optional pure cartridge code-identity lookup for banked or RAM-loaded code;
  duplicate CPU addresses are valid when their code identities differ.
- Native restart dispatch that abandons the current routine chain and resets
  the stack pointer to $FF.
- Explicit NMI/IRQ/BRK entry through cartridge vectors, IRQ masking, native
  handler dispatch, and RTI register/stack restoration at the handler return.
  Interrupt entry is rejected during an open RMW pair and from a cartridge bus
  callback so it stays between completed bus operations.
- Per-console CPU-bus-cycle and charged CPU-cycle counters, including the seven
  reset bus cycles and the seven bus cycles in explicit NMI/IRQ/BRK entry.
- `nnes_get_access_counts`/`nnes_reset_access_counts` snapshots of native-C
  profile-eligible per-kind read/write totals for comparing decomp runs with
  reference traces; reset/interrupt, wait-poll, and explicit dummy accesses
  remain in `nnes_bus_cycles` but are excluded from these counts.
- Optional copied per-read/write cost profiles, measured in tenths of a CPU
  cycle by RAM, I/O, cartridge RAM, and ROM access kind; enabled by default and
  controlled with `nnes_set_cpu_costs`. Cartridge `access_kind` can classify
  mapper RAM; without it cartridge accesses default to ROM. Weights apply to
  game-facing accesses; reset and interrupt sequencer accesses use bus cost only.
- Per-console A/X/Y/S/P/PC registers, little-endian word accesses, zero-page
  pointer resolution and complete byte read/write helpers for `($zp,X)` and
  `($zp),Y`, indexed page-cross reporting, absolute-indexed data reads/writes
  with the required dummy bus cycles and operand-latch seeding for omitted
  instruction fetches, absolute and indirect native JMP dispatch,
  indirect-JMP page wrapping, and zero-page, absolute-indexed, and indirect-
  indexed RMW helpers that preserve the 6502's old-value dummy write and final
  data write. Indexed RMW helpers also emit their mandatory addressing dummy
  read. Dummy reads and writes count base bus
  cycles and side effects without native-work profile charges.
- 6502 P flag set/clear helpers (B stays stack-only and U stays set), load
  N/Z, ADC/SBC/AND/ORA/EOR/CMP/BIT, shift/rotate results, and a helper to
  commit an ALU result to a native destination and P.
- Stable undocumented ANC/ALR/ARR/AXS, LAX/LAS/SAX, and
  SLO/RLA/SRE/RRA/DCP/ISC results are available without interpreting opcode
  streams.
- Relative branch conditions, targets, and taken-branch dummy reads; a next-PC
  dummy read for
  implied/accumulator instructions; and stack byte/word plus
  PHA/PLA/PHP/PLP/RTI/RTS helpers. RTS-based computed jumps can tail-dispatch
  their stack-derived native target. Instruction-shaped stack helpers read the
  byte after the current opcode for the implied-instruction dummy cycle.
- A poll-based `nnes_wait_until` helper with explicit cycle charges for the
  native C work in each poll turn. It keeps polling until ready or an access
  returns an error, matching the original wait loop behavior.
- Explicit errors for unfinished hardware; failed reads preserve output values.
  Value-discarding ordinary reads remain profile-eligible, distinct from
  unprofiled dummy reads.

Console instances own copies of descriptors and routine tables. Callback user
data is borrowed and must remain valid. Instances have no shared mutable state;
calls on the same instance require caller synchronization. Native routines may
call other registered routines, and callbacks may use bus access. The caller
must prevent unbounded recursion; destruction inside callbacks is outside this
contract.
The CPU data-bus latch is updated by successful accesses and is passed to
operation-aware cartridge callbacks. SAROM uses it when PRG RAM is disabled.
The PPU keeps its own register data latch. Controller reads still expose only
their serial bit, and open-bus decay/electrical behavior remains incomplete.

## Work still required for the SNES-style game workflow

This is a foundation, not a playable or behavior-qualified console. It has CPU-
visible PPU registers, VRAM/OAM backing, mapper mirroring, a basic NTSC vblank
clock, wait-boundary NMI delivery, and OAM DMA. It has no PPU renderer, sprite
evaluation, odd-frame timing, or general NMI scheduler. APU channels, DMC DMA,
a host frame loop, audio output, persistence, full instruction-level operation
sequencing, and complete cycle timing remain open. A supplied cost profile
adds native-C work around each access to the one-cycle bus cost and advances basic PPU time. `consoles/nes/tools/nnes-profile` fits
profiles from normalized reference traces. An optional Mesen2 source patch and
Lua collector are documented in [cost-profile.md](docs/cost-profile.md); that
collector has not been exercised live. Profiles retain a DMC DMA timing caveat
until `nnes` models the DMC channel and its DMA interactions.
`nnes_bus_cycles` counts modeled bus accesses and `nnes_cpu_cycle_tenths` also
includes profile/explicit charges. Those CPU times advance the basic PPU and
APU frame-status clocks, but not APU channel or renderer state. PPU NMI is delivered after a
wait-readiness callback; IRQ/BRK remain explicit, and no general event scheduler
exists. The restart helper is only available while `nnes_start` is running
an entry routine and called directly by a native routine, outside a cartridge
callback; there is no general host frame runner yet.
`nnes_wait_until` advances CPU, PPU, and APU frame-status time for supplied
poll work and bus reads, applies profile weights to those bus reads, and omits
poll reads from the access-count snapshot. It delivers a pending PPU NMI after
each complete poll callback, but does not advance APU channels or schedule
other events. A zero poll charge requires the ready callback to perform a bus
access while unready.
A poll that never becomes ready can keep waiting indefinitely.
`$4015` frame status and `$4017` IRQ inhibition have a partial implementation;
other APU channel registers and DMC DMA still return `NNES_UNSUPPORTED`.
Unsupported PPU mapper memory also fails explicitly. The caller must check
errors and stop rather than ignore missing hardware. `nnes_call` dispatches a
native C entry directly; `nnes_jsr_call` models a translated JSR's stack dummy
read/pushes and the target RTS pulls around native dispatch.

Cartridge callbacks are a connector. A game-specific SAROM adapter now exists
for Dragon Warrior and is covered by synthetic contracts plus a selected-image
integration check; its live mapper behavior remains unqualified. Bank-aware
native routine identities are available to board adapters. Famicom expansion
controllers, expansion audio, and the disk system are deferred.

Next slices should recover the selected reset's `$FD3A` BRK/IRQ handler and
compare its live mapper/APU state against a longer Mesen trace, then implement
PPU rendering, APU channels, DMC DMA, and a host frame runner. Games should keep
the same recovery method: original address-tagged C, explicit bus operations
and waits, cartridge data, and synthetic plus oracle-backed hardware tests.
Full game compatibility requires evidence beyond these unit tests.

The [first native NES game goal prompt](../../docs/superpowers/plans/2026-11-02-nes-first-native-game-goal.md)
specifies the missing console work, complete assembly/C recovery, and evidence
required to qualify a chosen title using the SNES preservation workflow.

## Hardware references

The initial address routing, controller serial protocol, indirect JMP wrap, and
RMW bus behavior follow the [NESdev CPU memory map](https://www.nesdev.org/wiki/CPU_memory_map),
[standard controller documentation](https://www.nesdev.org/wiki/NES_controller),
and [6502 instruction reference](https://www.nesdev.org/wiki/Instruction_reference).
The initial PPU register and vblank implementation follows the [PPU programmer
reference](https://www.nesdev.org/wiki/PPU_programmer_reference) and [NTSC frame
timing notes](https://www.nesdev.org/wiki/PPU_frame_timing); renderer behavior,
odd-frame timing, and NMI delivery are not implemented yet.
The OAM DMA halt, optional alignment, and 256 read/write pairs follow the
[NESdev DMA reference](https://www.nesdev.org/wiki/DMA); DMC interaction is
not implemented.
These references guide implementation; the tests here are synthetic contract
checks and do not establish hardware or emulator parity.

The repository's [license](../../LICENSE) and [notice](../../NOTICE) apply.


The current platform contracts pass 5/5 in `build/nes-goal`. They establish
synthetic behavior only; they do not establish full NES hardware parity.
