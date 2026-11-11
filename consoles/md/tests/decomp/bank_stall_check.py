#!/usr/bin/env python3
"""A Z80 that reads its bank window while the 68000 is stalled on a VDP
transfer must not make the reference capture fail.

The unpatched core set the Z80 clock to the 68000's clock on such a read
even when the Z80 was already ahead, so its bus events ran backward and
the NMDT stream latched an error (first seen on a title, near its first
gameplay frames). Patch 0003 only ever delays the Z80. The cartridge is
authored: the 68000 repeats a two-word 68000-to-VRAM DMA, so its stalls
end at every phase of a Z80 loop that makes one bank-window read per pass.
The rewind needs a stall to end between an instruction's opcode fetch and
its bank read.
"""
import json
import pathlib
import struct
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import census_fixture  # noqa: E402

FRAMES = 3
RECORD = struct.Struct("<QBBBBII")
HEADER = struct.calcsize("<4sHHQ")
FOOTER = struct.calcsize("<4sQ")

Z80 = """\
        org     0
        di
        ld      sp,$1FF0
loop:   ld      a,($8000)
        nop
        nop
        nop
        inc     b
        jr      loop
"""
M68K = """\
        org     $200
start:  move.w  #0,($A11200).l
        move.w  #$0100,($A11100).l
        move.w  #$0100,($A11200).l
        lea     ($400).w,a0
        lea     ($A00000).l,a1
        moveq   #{last},d1
copy:   move.b  (a0)+,(a1)+
        dbf     d1,copy
        move.w  #0,($A11200).l
        move.w  #$0400,d3
wait:   dbf     d3,wait
        move.w  #$0100,($A11200).l
        move.w  #0,($A11100).l
        lea     ($C00004).l,a5
        move.w  #$8114,(a5)
        move.w  #$8F02,(a5)
again:  move.w  #$9302,(a5)
        move.w  #$9400,(a5)
        move.w  #$9500,(a5)
        move.w  #$9680,(a5)
        move.w  #$977F,(a5)
        move.l  #$40000080,(a5)
        bra.s   again
        org     $3F0
stray:  rte
"""


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def build(directory):
    directory.mkdir(parents=True, exist_ok=True)
    program = census_fixture._assemble("z80", "vasmz80_oldstyle", Z80, directory)
    code = census_fixture._assemble("m68k", "vasmm68k_mot", M68K.format(last=len(program) - 1),
                                    directory, ("-m68000", "-no-opt"))
    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x3F0).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT BANK STALL CONTROL".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT BANK STALL CONTROL".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x200 + len(code)] = code
    rom[0x400:0x400 + len(program)] = program
    rom_path = directory / "bank-stall.bin"
    rom_path.write_bytes(rom)
    scenario = {
        "version": 1, "region": "U", "model": "md1va3", "pad1": "3", "pad2": "3", "input": {},
        "required_capabilities": [
            "m68k_instruction", "m68k_state", "z80_instruction", "z80_state",
            "flow_edge", "fetch_provenance", "master_clock", "bus_intervals",
        ],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
    }
    scenario_path = directory / "bank-stall.json"
    scenario_path.write_text(json.dumps(scenario, sort_keys=True, separators=(",", ":")))
    return rom_path, scenario_path


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-bank-stall-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path = build(temp)
        capture = census_fixture.capture(rom_path, scenario_path, temp, frames=FRAMES)
        manifest = json.loads((capture / "manifest.json").read_text())
        require(manifest["status"] == "complete" and manifest["frames_run"] == FRAMES,
                "capture did not complete")
        data = (capture / "capture.nmdt").read_bytes()[HEADER:-FOOTER]
        bank_reads = dma_words = 0
        last_z80 = 0
        for clock, device, operation, _, _, address, _ in RECORD.iter_unpack(data):
            if device == 2:
                require(clock >= last_z80, "Z80 bus clock ran backward at %d" % clock)
                last_z80 = clock
                bank_reads += operation == 2 and address >= 0x8000
            elif device == 3 and operation == 4:
                dma_words += 1
        # the control only means something if both sides of the collision ran
        require(bank_reads > 1000, "the Z80 made only %d bank-window reads" % bank_reads)
        require(dma_words > 1000, "the 68000 ran only %d DMA words" % dma_words)
    print("bank_stall_check: ok (%d frames complete; %d Z80 bank-window reads against %d "
          "68000-to-VRAM DMA words, Z80 clock never backward)" % (FRAMES, bank_reads, dma_words))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("bank_stall_check: " + str(exc))
