# Native NES game contract

**Status:** initial contract for the user-selected Dragon Warrior (USA) (Rev A)
image. It defines the native C and cartridge interface the title decompilation
must target. It does not claim that the current `nnes` library implements the
complete contract.

## Preservation result

The game has two companion artifacts:

| Artifact | Required result |
|---|---|
| Symbolic 6502 assembly | Rebuild the complete selected iNES image byte for byte, including its header, PRG, and CHR regions. Executable regions use named instructions and labels; data regions retain typed structure or documented bytes. |
| Native C | Readable functions for all executable routines, with their observable registers, flags, stack effects, memory changes, and ordered hardware accesses checked against the original. |

Assembly is the byte-level preservation record. Native C is the program the
standalone game runs. Neither artifact may run a 6502 interpreter, JIT, or
emulator fallback.

## Game routine interface

Every native routine receives an `nnes *` context and reads/writes the
per-console 2A03 state through `nnes_cpu`. An entry wrapper documents the
original CPU address or banked code identity, input registers, output
registers/flags, stack effect, and any non-returning behavior.

Ordinary control flow is C control flow. A game routine issues the original's
observable reads and writes through `nnes`; it does not model opcode fetches or
charge a fixed cycle count for each C statement. Dummy accesses remain explicit
when they change a device/mapper result. RMW writes preserve the original-value
write followed by the modified-value write. Word and pointer helpers follow
   6502 bus order and wrap rules. The cartridge's operation-aware bus adapter
   receives the operation kind, cumulative CPU timestamp, RMW-pair flag, and
   prior CPU data-bus latch along with the address/value. Mapper serial ports
   can distinguish an RMW pair from spaced data stores, and open-bus devices
   can return the latched value explicitly.
An optional read-only CPU-bus observer records completed internal, PPU,
cartridge, and DMA operations for reference comparison without changing
emulation results.

Hardware wait loops use a ready callback and the scheduler. The scheduler
advances the PPU/APU, DMA, and event clocks, and delivers interrupts between
complete bus operations without splitting an RMW pair or multi-byte access.
The game C does not maintain its own instruction clock ledger.

## Banked routine identity

A routine key is `(code identity, CPU address)`, not a 16-bit address alone.
The cartridge mapper resolves the executable contents currently visible at a
CPU address for each native call or tail jump. Two physical PRG banks may both
map to `$8000` and must select different C routines. The fixed upper PRG window
must resolve to its physical bank independently of the current switchable bank.

The same identity mechanism covers executable RAM. A RAM code identity names
its segment and load generation or content digest so replacing uploaded code
cannot dispatch an older native translation. An unmapped address, unknown bank,
or missing routine returns an explicit error. It never falls back to guest
instruction execution.

For the selected revision, the user-selected source SHA-256 is
`676b89d76cf9e83eed76c0a67f81da906916f0bce321cf3a9a39ac78bc9c0845`.
NesCartDB identifies its Revision A cartridge as NES-SAROM-01 with MMC1B2,
64 KiB PRG ROM, 16 KiB CHR ROM, and 8 KiB battery-backed WRAM. The first Mesen
reset trace executes only in `$C000-$FFFF`. The game project now contains a
SAROM/MMC1B2 adapter. Synthetic board contracts cover PRG/CHR bank modes,
mirroring, serial writes, RMW filtering, and battery RAM; a local check against
the selected image confirms fixed bank 3, `$FF96` dispatch, a switchable-window
bank update, RAM enable/disable, and disabled-RAM reads from the CPU data-bus
latch. These checks do not yet compare the board's complete live state with
Mesen, so the adapter remains partially verified.

## Console-provided behavior

The console provides behavior reached by the selected game through stable
interfaces:

- CPU memory mapping, mirrored internal RAM, open bus, mapper register timing,
  cartridge PRG/CHR access, controller ports, and save RAM.
- PPU registers/latches, VRAM and OAM, rendering, vblank, NMI, and NTSC timing.
- APU channel registers, frame sequencer, DMC, audio samples, and IRQ behavior.
  The current `nnes` slice only supplies frame IRQ/status behavior at `$4015`
  and `$4017`, returns CPU open bus for write-only `$4000-$4014` reads, and
  seeds indexed dummy cycles from omitted address operands. It does not satisfy
  the channel, DMC, audio, or interrupt gates.
- OAM/DMC DMA, CPU stalls, and their ordered bus operations.
- A deterministic headless frame runner and an interactive host with live
  picture, sound, controller input, reset, and clean shutdown.

The game-specific adapter describes the exact SAROM board and image. It does
not replace hardware behavior with title-specific fake register values,
prerecorded audio/video, or snapshots. Unsupported hardware fails explicitly.

## Register and flag rules

The CPU state contains A, X, Y, S, P, and PC. P stores N/V/D/I/Z/C with U set
and B clear; PHP, BRK, and interrupt entry synthesize the appropriate pushed
bits. Decimal mode is visible to the game but does not change 2A03 ADC/SBC
arithmetic. Interrupt entry and RTI preserve the hardware stack order and
return address. Code that changes A/X/Y/S/P/PC does so through the native
register state and keeps each routine's proven clobbers.

## Timing and cost profiles

Observable bus operations, waits, DMA, and scheduled interrupts advance the
console. Device state is sampled after the base bus cycle; a versioned profile
then accounts for native C work between accesses and advances the basic PPU
clock before the next operation. The cumulative timestamp includes that weight.
Profiles are measured against reference spans that separate memory kinds. A
fitted profile is tied to the exact image, board, host, reference build, and
scenario. It supplements modeled hardware time and does not establish hardware
parity by itself. Original-mode behavior and any enhanced mode receive separate
comparisons.

## Verification contract

Routine comparisons bind the selected image hash, physical bank/window,
reference build/settings, scenario, and native build. They compare entry/exit
registers, flags, stack state where visible, memory changes, and ordered
observable accesses. Any excluded fetch, dummy, DMA, or I/O operation is
reported explicitly; missing or truncated events do not count as a match.

The full title suite covers reset, menus, gameplay routes, transitions,
retry/death, completion, controller variations, and persistence. It compares
RAM, important event boundaries, rendered frame pixels, audio samples, and
timing over declared horizons. The final runner must pass reset-to-gameplay,
menus, representative routes, transitions, retry, and completion with native C
only. Synthetic tests and a passing reference-emulator boot do not qualify the
game.

## Current status

`nnes` provides explicit CPU bus callbacks, RAM/controllers, CPU register and
ALU helpers, stack/control-flow operations, bank-aware native dispatch,
interrupt entry helpers, wait/cost accounting, and per-instance state. The
Dragon Warrior project adds a partially verified SAROM/MMC1B2 adapter, the
`$FF96` entry state, and native reset C through bank-2 `$BCB0/$BCB3/$BDE0/$BCCC`
and bank-2 `$BCD2`, then stops at the repeated `$BCCF` call in the no-input
branch. Its Start-input route recovers `$BCE1` through `$BD17`; the 138-event
controller prefix matches, while the longer route retains measured wait/NMI
residuals. The MesenCE run matches the initial sound, CHR, queue, and NMI slices;
through the `$BCCF` controller call 855/859 writes match, the no-input
controller path's 139 ordered read/write events match, and the separate Start
route's entry prefix matches through `$BCE1`. Nineteen APU/write-only-register bus
accesses match, and four values differ across two NMI return boundaries. The
library also has PPU registers, VRAM/OAM storage,
mapper mirroring, a basic NTSC vblank clock, and partial frame IRQ status at
`$4015`/`$4017`. An exact replay of the reset trace's pre-NMI PPU-register
stream matches the native PPU device, and the captured `$FE67` NMI path matches
its ordered Mesen bus sequence. This does not render frames or cover gameplay.
Pending PPU NMI is delivered at `nnes_wait_until` poll boundaries; general event
scheduling, full controller electrical behavior, PPU rendering/sprite evaluation,
odd-frame timing, APU channels/mixing, DMC DMA, and a host remain incomplete.
The Mesen comparison does not qualify the complete mapper or game.

The current title's ledger (`docs/nes-games/dragon-warrior-usa-rev-a/qualification.md`)
is kept with that title's recovered source in its own repository, outside
nativekit, which holds no game's code.
