"""An authored cartridge for the per-call checker, and its routine addresses.

The 68000 uploads a Z80 program, then six times: sums words, writes a VDP
register and reads the status, calls one of four handlers through a pointer
table, runs a routine that calls another twice, sends a byte to the Z80
through its RAM, and calls a routine that calls one with no C. It then
reads the pad, sets a PSG tone, copies RAM to VRAM by DMA, enables the
vertical interrupt in a routine and runs a routine long enough to be
interrupted. The Z80 loops calling a routine that adds the byte the 68000
sent to a total, writes the YM address port and reads the status. One
routine with C is never called. Nothing here comes from a game.
"""
import json
import pathlib

import census_fixture

Z80 = """\
        org     0
        di
        ld      sp,$1FF0
zloop:  call    fetch
        jr      zloop
        org     $20
fetch:  ld      a,($1F00)
        ld      hl,$1F01
        add     a,(hl)
        ld      (hl),a
        ld      ($4000),a
        ld      a,($4000)
        and     $80
        ret
"""
M68K = """\
        org     $200
start:  move.w  #0,($A11200).l
        move.w  #$0100,($A11100).l
        move.w  #$0100,($A11200).l
        lea     ($800).w,a0
        lea     ($A00000).l,a1
        moveq   #{z_last},d1
copy:   move.b  (a0)+,(a1)+
        dbf     d1,copy
        move.w  #0,($A11200).l
        bsr.w   delay
        move.w  #$0100,($A11200).l
        move.w  #0,($A11100).l
        moveq   #0,d6
        moveq   #0,d3
main:   lea     (words,pc),a0
        move.w  d6,d0
        addq.w  #2,d0
        bsr.w   sum
        move.w  d0,($FF0010).l
        move.w  d6,d0
        bsr.w   poke
        move.w  d6,d0
        and.w   #3,d0
        bsr.w   dispatch
        lea     (words,pc),a0
        bsr.w   outer
        move.b  d6,d0
        bsr.w   send
        bsr.w   relay
        addq.w  #1,d6
        cmp.w   #6,d6
        bne.s   main
        cmp.w   #7,d6
        bne.s   skip
        bsr.w   unused
skip:   move.l  #$12345678,($FF0100).l
        move.l  #$9ABCDEF0,($FF0104).l
        bsr.w   pad
        moveq   #5,d5
        bsr.w   count
        moveq   #2,d5
        bsr.w   below
        moveq   #0,d5
        bsr.w   tailc
        moveq   #2,d5
        bsr.w   tailc
        bsr.w   tone
        bsr.w   dma
        move.w  #$8A20,($C00004).l      ; a horizontal interrupt every 33 lines
        move.w  #$8014,($C00004).l
        bsr.w   irqon
        move.w  ($FF0040).l,d0          ; wait for a vertical interrupt: the horizontal
vwait:  cmp.w   ($FF0040).l,d0          ; ones start in the next frame
        beq.s   vwait
        bsr.w   busy
        bsr.w   spin
        move.w  #$2700,sr
forever:
        bra.s   forever
delay:  move.w  #$0400,d5
wait:   dbf     d5,wait
        rts
words:  dc.w    1,2,3,$7FFF,$8000,$FFFF,5,9,11,13

        org     $400
sum:    subq.w  #1,d0
        move.w  d0,d1
        moveq   #0,d0
suml:   add.w   (a0)+,d0
        dbf     d1,suml
        rts

        org     $420
poke:   or.w    #$8F00,d0
        move.w  d0,($C00004).l
        move.w  ($C00004).l,d1
        and.w   #$0200,d1
        rts

        org     $440
dispatch:
        add.w   d0,d0
        add.w   d0,d0
        lea     (dtab,pc),a1
        movea.l (0,a1,d0.w),a1
        jsr     (a1)
        rts
        org     $460
dtab:   dc.l    h0,h1,h2,h3
        org     $480
h0:     move.w  #$1111,($FF0020).l
        rts
h1:     move.w  #$2222,($FF0020).l
        rts
h2:     move.w  #$3333,($FF0020).l
        rts
h3:     move.w  #$4444,($FF0020).l
        rts

        org     $4C0
outer:  moveq   #2,d0
        bsr.w   sum
        move.w  d0,d2
        moveq   #3,d0
        bsr.w   sum
        add.w   d2,d0
        move.w  d0,($FF0030).l
        rts

        org     $4E0
send:   move.w  #$0100,($A11100).l
swait:  btst    #0,($A11100).l
        bne.s   swait
        move.b  d0,($A01F00).l
        move.w  #0,($A11100).l
        rts

        org     $510
spin:   move.w  #7999,d1
spinl:  addq.w  #1,($FF0050).l
        dbf     d1,spinl
        rts

        org     $530
unused: moveq   #99,d7
        rts

        org     $540
vint:   addq.w  #1,($FF0040).l
        rte
stray:  rte

        org     $550
relay:  bsr.w   nocode
        addq.w  #1,d3
        rts
        org     $560
nocode: move.w  #$5555,($FF0060).l
        moveq   #7,d4
        rts

        org     $580
pad:    move.b  #$40,($A10009).l
        move.b  #$40,($A10003).l
        move.b  ($A10003).l,d0
        move.b  d0,($FF0070).l
        rts

        org     $5A0
tone:   move.b  #$8E,($C00011).l
        move.b  #$0F,($C00011).l
        move.b  #$90,($C00011).l
        rts

        org     $5C0
irqon:  move.w  #$8164,($C00004).l
        move.w  #$2000,sr
        rts

        org     $5D0
count:  subq.w  #1,d5
        bne.s   count
        rts

        org     $5E0
dma:    lea     ($C00004).l,a5
        move.w  #$8114,(a5)
        move.w  #$8F02,(a5)
        move.w  #$9308,(a5)
        move.w  #$9400,(a5)
        move.w  #$9580,(a5)
        move.w  #$9680,(a5)
        move.w  #$977F,(a5)
        move.w  #$4000,(a5)
        move.w  #$0080,(a5)
        rts

        org     $700
hint:   addq.w  #1,($FF0080).l
        rte

        org     $720
busy:   move.w  #2499,d1
busyl:  addq.w  #1,($FF0090).l
        dbf     d1,busyl
        rts

        org     $7E0
back:   rts
below:  tst.w   d5
        beq.s   back
        moveq   #1,d6
        rts

        org     $7F0
tailc:  tst.w   d5
        bne.w   count
        moveq   #3,d6
        rts

        org     $7FE
        dc.w    0
"""
ROUTINES = {"sum": 0x400, "poke": 0x420, "dispatch": 0x440, "handler0": 0x480,
            "handler1": 0x48A, "handler2": 0x494, "handler3": 0x49E, "outer": 0x4C0,
            "send": 0x4E0, "spin": 0x510, "unused": 0x530, "relay": 0x550,
            "pad": 0x580, "tone": 0x5A0, "irqon": 0x5C0, "dma": 0x5E0, "count": 0x5D0,
            "below": 0x7E2, "tailc": 0x7F0, "busy": 0x720}
NOCODE = 0x560
Z_FETCH = 0x20
LOOPS = 6
SOURCE = pathlib.Path(__file__).resolve().parent / "calls" / "routines.c"


def build(directory, capabilities=None, pressed=("B",)):
    """Write the ROM and scenario; return (rom path, scenario path). `pressed`
    is held on pad 1 for the first four frames."""
    directory = pathlib.Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    program = census_fixture._assemble("z80", "vasmz80_oldstyle", Z80, directory)
    code = census_fixture._assemble("m68k", "vasmm68k_mot", M68K.format(z_last=len(program) - 1),
                                    directory, ("-m68000", "-no-opt"))
    if len(code) != 0x600 or len(program) > 0x100:
        raise RuntimeError("calls fixture: 68000 image is %d bytes, Z80 %d" % (len(code), len(program)))
    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x548).to_bytes(4, "big")
    rom[0x78:0x7C] = (0x540).to_bytes(4, "big")         # level 6: the vertical interrupt
    rom[0x70:0x74] = (0x700).to_bytes(4, "big")         # level 4: the horizontal interrupt
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT CALL CONTROL".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT CALL CONTROL".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x800] = code
    rom[0x800:0x800 + len(program)] = program
    rom_path = directory / "call-control.bin"
    rom_path.write_bytes(rom)
    scenario = {
        "version": 1, "region": "U", "model": "md1va3", "pad1": "3", "pad2": "3",
        "input": {str(frame): {"1": list(pressed)} for frame in range(1, 5)} if pressed else {},
        "required_capabilities": capabilities or [
            "m68k_instruction", "m68k_state", "z80_instruction", "z80_state", "flow_edge",
            "reset_exception_interrupt", "fetch_provenance", "master_clock", "bus_intervals",
        ],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
    }
    scenario_path = directory / "call-control.json"
    scenario_path.write_text(json.dumps(scenario, sort_keys=True, separators=(",", ":")))
    return rom_path, scenario_path
