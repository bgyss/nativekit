# nativemd Contract

## Version and scope

nativemd's public game contract is **1.0.0**, declared by `nmd/version.h`.
It models the base Sega Mega Drive / Genesis: the 68000-facing bus, a native
C routine model for interrupt handlers, a native Z80 routine model, Mode 5
VDP output and DMA, YM2612 and SN76489 sound, and two controller ports. It does
not execute 68000 or Z80 machine code and does not model Sega CD, 32X, or other
expansion hardware. Unsupported operations fail explicitly.

Games include `nmd/nmd.h`, initialize an `nmd_cart`, and call `nmd_main` for
the supported kit loop and host/script input path. `nmd_power` and
`nmd_run_frame` expose lower-level machine control for tests and embedding;
the public API does not provide a separate controller-mask setter for a custom
host. The `nmd_cart.routines` table maps 68000 interrupt vectors to C handlers;
`z80_routines` maps Z80 entry points to C routines. The machine bus functions
are `rd8`, `rd16`, `rd32`, `wr8`, `wr16`, and `wr32`; longwords are two
16-bit bus transfers.

Zero-initialized carts select NTSC, the md1va3 model, and six-button pads on
both ports. A null `cost_profile` adds no profiled access work.

## Address and device map

| 68000 address | Behavior |
|---|---|
| `$000000-$3FFFFF` | Cartridge ROM reads; accesses beyond `rom_size` and ROM writes fail. |
| `$A00000-$A01FFF` | Z80 RAM, owned by the 68000 only after BUSREQ acknowledgement. |
| `$A04000-$A04003` | YM2612 ports; 68000 access requires BUSREQ acknowledgement. |
| `$A06000` | Z80 serial bank register. |
| `$A10001` | Read-only version and region bits; writes are ignored. |
| `$A10003/$A10005` | Controller data ports 1 and 2. |
| `$A10009/$A1000B` | Controller control ports 1 and 2. |
| `$A11000` | Memory mode control. |
| `$A11100` | Z80 BUSREQ control and acknowledgement status on the high byte lane. |
| `$A11200` | Z80 reset control on the high byte lane. |
| `$A14000-$A14003` | TMSS signature words on md1va6; open bus on md1va3. |
| `$C00000-$DFFFFF` | Mirrored VDP data, control, and H/V counter ports. `$C00011` is PSG output. |
| `$E00000-$FFFFFF` | 64 KiB work RAM mirrored through the top 2 MiB. |

The Z80 has 8 KiB RAM at `$0000-$1FFF`, YM2612 ports at `$4000-$4003`, the
serial bank register at `$6000`, the PSG at `$7F10-$7F17` (or output port
`$7F`), and the 68000 bank window at `$8000-$FFFF`. Other unsupported bus
operations fail. On byte-wide I/O registers, the even/high byte lane reads
`0xFF` and ignores writes. Unsupported odd offsets in the `$A10000-$A1001F`
I/O window fail. The documented md1va3 TMSS addresses are the exception: they
read open bus and ignore writes.

## Models, region, and I/O

`NMD_MODEL_MD1VA3` has no TMSS. `NMD_MODEL_MD1VA6_TMSS` reports TMSS in the
version register and locks 68000 VDP accesses until the two signature words at
`$A14000` and `$A14002` contain `SE` and `GA` (`SEGA`). On md1va3 those
signature addresses read open bus and ignore writes. `$A10001` reports `0xA0`
for NTSC md1va3, `0xA1` for NTSC md1va6, and sets the PAL bit for `0xE0` or
`0xE1` respectively.

The controller data registers are `$A10003` and `$A10005`; control registers
are `$A10009` and `$A1000B`. The low byte lane is connected. `NMD_PORT_PAD3`
and `NMD_PORT_PAD6` select three- and six-button protocols. Input masks use
the `NMD_BUTTON_*` bits and update at frame boundaries. A TH change settles
after 112 master clocks. The six-button sequence uses a 56,000-master-clock
inactivity reset interval; that threshold is a release implementation choice,
not a hardware-qualified measurement.

## Timing and cost profiles

NTSC runs at 53,693,175 master clocks per second with 262 lines per frame;
PAL uses 53,203,395 master clocks per second and 312 lines. A 68000 byte or
word access consumes 28 master clocks; a longword consumes two such cycles.
Z80 T-states are 15 master clocks, with three-T memory accesses, four-T I/O
accesses, and a three-T bank-window wait. VDP, DMA, interrupts, and sound
advance on the shared master-clock scheduler.

`nmd_cost_profile` version 1 stores additional read/write weights by access
kind. `m68k_tenths` uses tenths of a master clock; `z80_tenths` uses tenths of
a Z80 T-state. `NMD_COST_PROFILE_VERSION` must match the profile's `version`.
`nmd_set_cpu_costs(0)` disables 68000 bus and profile costs while hardware
waits, interrupts, DMA, and Z80 costs continue. Enabling it again restores
68000 access costs.

`nmd-profile measure` fits both processors from the pinned BlastEm candidate
and writes `profile.txt`, `profile.c`, and `report.txt`. The candidate provides
per-CPU completion clocks, not exact access start/end pairs; the report
identifies its bus-time split and warns that dynamic device waits can remain
in fitted work. These profiles are candidate-derived estimates, not hardware
qualification. Reports contain ROM hashes, not ROM paths or contents.

## Errors, compatibility, and verification

Odd word/longword accesses, unsupported device operations, invalid profile
versions, and accesses outside the declared cartridge memory fail through
`nk_fatal` with exit status 70. The explicit exceptions are the disconnected
high I/O byte lane and md1va3's open TMSS ports described above. A game must
use the bus APIs and documented device routines; unsupported behavior is not
silently approximated.

The public C interface is C11. The separate consumer project under
`consoles/md/tests/consumer/` shows the supported `add_subdirectory` and
`nmd` link pattern. `mise run test:md-consumer` builds and runs it headless.
`mise run test:md-rom` compares 59 synthetic cases with the candidate; six
candidate differences are documented in `consoles/md/tests/rom/KNOWN`.
BlastEm traces and the generated golden master provide deterministic regression
evidence, not independent hardware qualification. Linux remains untested.
