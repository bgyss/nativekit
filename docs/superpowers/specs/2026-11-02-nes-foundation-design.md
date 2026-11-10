# NES / Famicom foundation

> **Moved 2026-11-09.** This was written for the standalone prototype. The library is now
> `consoles/nes/`, its tools `consoles/nes/tools/`, and its game contract
> `consoles/nes/docs/contract.md`; paths below are the prototype's (`platforms/nes`, `tools/`).

The requested NES implementation lives in this repository and develops independently
of Mega Drive / Genesis. Follow the existing native-game-C model: no CPU interpreter,
ROM execution, or game-specific fallback.

## Approach

Use a standalone C11 library under `platforms/nes`, with its own CMake project,
`nnes` symbols and headers, instance-owned state, and synthetic tests. Folding it
into the SNES library would couple console state and build options; a separate
repository would contradict the requested location. The standalone directory is
the implementation choice for this first slice.

Implement 2 KiB mirrored CPU work RAM, two standard serial joypads, cartridge bus
callbacks for addresses at or above $4020, and address-to-native-function dispatch. Startup reads the cartridge reset vector
and enters its registered native C routine, as in the SNES cartridge workflow.
A runnable synthetic cartridge demonstrates this without game code or assets.
Return explicit errors for unfinished hardware and missing routines. Failed reads
leave their output untouched. Cartridge descriptors and routine tables are copied
at creation; callback user data remains caller owned. No globals hold console state.
Callbacks may access the bus and native routines may call other registered
routines. Callers prevent unbounded recursion. Destruction during callbacks is
outside this initial contract. Creation rejects duplicate or null routines.

This slice does not implement PPU, APU, DMA, clocks, interrupts, mappers, cartridge
image parsing, save states, host presentation, or game compatibility. It makes no
hardware-equivalence or cycle-accuracy claim. Unsupported register ranges must not
silently return invented hardware results. Controller reads expose the serial bit
only; electrical open-bus behavior is deferred.

## Acceptance

A headless standalone CMake build and CTest run exercise RAM mirroring, both
controllers, live strobing and latched reads, cartridge routing/error propagation,
native dispatch and validation, and isolation between two console instances.
The decompilation method remains native recovered C plus explicit hardware accesses
and cartridge data. The initial API is provisional: 6502 register/flag/stack
helpers analogous to SNES machine.h, bank-aware routine identities, and the
timed hardware/frame runner are staged separately. The machine helper follow-up
adds the first item; the latter two remain future work.

Existing SNES sources and build settings remain intact apart from a README link.
