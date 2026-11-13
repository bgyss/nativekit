"""An authored cartridge for the intake controls, and its reference capture.

The 68000 program uploads Z80 program A verbatim from the ROM, takes one of
three entries of a computed call table, then uploads a different program B to
the same Z80 address from an inverted copy (so B has no verbatim ROM source).
It also holds a routine nothing refers to (dead code), a vector-only handler
and decoy bytes that would decode as code. Nothing here comes from a game.
"""
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
RUNNER = ROOT / "consoles/md/tools/decomp/run.py"
VASM = ROOT / "build/toolchains/vasm"
CORE = ROOT / "build/toolchains/blastem" / (
    "libblastem.dylib" if sys.platform == "darwin" else "libblastem.so")

Z80_A = """\
        org     0
        di
        ld      sp,$1FF0
        ld      hl,$1000
loopa:  inc     (hl)
        ld      a,(hl)
        and     3
        jr      z,loopa
        call    suba
        jr      loopa
suba:   ld      b,a
        ret
        xor     a
        ret
"""
Z80_B = """\
        org     0
        di
        ld      sp,$1FE0
        ld      ix,$1100
loopb:  inc     (ix+0)
        jp      next
next:   ld      a,(ix+0)
        ld      hl,loopb
        jp      (hl)
"""
M68K = """\
        org     $200
start:  move.w  #0,($A11200).l
        move.w  #$0100,($A11100).l
        move.w  #$0100,($A11200).l
        lea     ($400).w,a0
        lea     ($A00000).l,a1
        moveq   #{a_last},d1
copya:  move.b  (a0)+,(a1)+
        dbf     d1,copya
        move.w  #0,($A11200).l
        bsr.w   delay
        move.w  #$0100,($A11200).l
        move.w  #0,($A11100).l
        moveq   #1,d0
        add.w   d0,d0
        lea     (table,pc),a2
        move.w  (0,a2,d0.w),d0
        jsr     (0,a2,d0.w)
        bsr.w   delay
        move.w  #$0100,($A11100).l
grant:  btst    #0,($A11100).l
        bne.s   grant
        lea     ($480).w,a0
        lea     ($A00000).l,a1
        moveq   #{b_last},d1
copyb:  move.b  (a0)+,d2
        not.b   d2
        move.b  d2,(a1)+
        dbf     d1,copyb
        move.w  #0,($A11200).l
        bsr.w   delay
        move.w  #$0100,($A11200).l
        move.w  #0,($A11100).l
forever:
        bra.s   forever

        org     $300
delay:  move.w  #$0400,d3
wait:   dbf     d3,wait
        rts
dead:   moveq   #99,d7
        rts

        org     $320
table:  dc.w    case0-table,case1-table,case2-table
case0:  moveq   #10,d7
        rts
case1:  moveq   #11,d7
        rts
case2:  moveq   #12,d7
        rts

        org     $3F0
stray:  rte
        org     $3F8
decoy:  dc.w    $4E75,$7001,$4E75,$0000
"""


def _assemble(name, executable, source, directory, extra=()):
    asm = directory / (name + ".s")
    out = directory / (name + ".bin")
    asm.write_text(source)
    result = subprocess.run([str(VASM / executable), "-Fbin", *extra, "-quiet", "-o", str(out),
                             str(asm)], stdin=subprocess.DEVNULL, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("census fixture: %s did not assemble: %s" % (name, result.stderr))
    return out.read_bytes()


def build(directory):
    """Write the ROM and scenario; return (rom path, scenario path, facts)."""
    directory = pathlib.Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    program_a = _assemble("z80-a", "vasmz80_oldstyle", Z80_A, directory)
    program_b = _assemble("z80-b", "vasmz80_oldstyle", Z80_B, directory)
    if len(program_a) > 0x80 or len(program_b) > 0x80:
        raise RuntimeError("census fixture: a Z80 program outgrew its ROM slot")
    code = _assemble("m68k", "vasmm68k_mot",
                     M68K.format(a_last=len(program_a) - 1, b_last=len(program_b) - 1),
                     directory, ("-m68000", "-no-opt"))
    if len(code) != 0x200:
        raise RuntimeError("census fixture: 68000 image is %d bytes, not 512" % len(code))

    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x3F0).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT INTAKE CONTROL".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT INTAKE CONTROL".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x400] = code
    rom[0x400:0x400 + len(program_a)] = program_a
    rom[0x480:0x480 + len(program_b)] = bytes(value ^ 0xFF for value in program_b)
    rom_path = directory / "intake-control.bin"
    rom_path.write_bytes(rom)

    scenario = {
        "version": 1, "region": "U", "model": "md1va3", "pad1": "3", "pad2": "3", "input": {},
        "required_capabilities": [
            "m68k_instruction", "m68k_state", "z80_instruction", "z80_state",
            "flow_edge", "fetch_provenance", "master_clock", "bus_intervals",
        ],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
    }
    scenario_path = directory / "intake-control.json"
    scenario_path.write_text(json.dumps(scenario, sort_keys=True, separators=(",", ":")))
    # the Z80 extents that run: A stops at suba's ret, B at its jp (hl)
    facts = {
        "program_a": program_a[:program_a.index(b"\xC9") + 1],
        "program_b": program_b,
        "a_rom_offset": 0x400, "b_rom_offset": 0x480,
        "delay": 0x300, "dead": 0x30A, "table": 0x320,
        "case0": 0x326, "case1": 0x32A, "case2": 0x32E, "stray": 0x3F0, "decoy": 0x3F8,
    }
    return rom_path, scenario_path, facts


def capture(rom_path, scenario_path, directory, frames=3, extra=()):
    """Run the reference core over the ROM; return the capture directory.
    `extra` is passed to both runner calls (the evidence mode, for one)."""
    directory = pathlib.Path(directory)
    if not CORE.is_file():
        raise RuntimeError("census fixture: the pinned reference core is not built")
    common = [sys.executable, str(RUNNER), "--core", str(CORE), "--rom", str(rom_path),
              "--scenario", str(scenario_path), "--frames", str(frames), *extra]
    estimate = subprocess.run(common + ["--mode", "estimate", "--out", str(directory / "estimate")],
                              capture_output=True, text=True, timeout=450)
    if estimate.returncode:
        raise RuntimeError("census fixture: preflight failed: " + estimate.stderr.strip())
    run = subprocess.run(
        common + ["--mode", "capture", "--estimate-file",
                  str(directory / "estimate" / "estimate.json"),
                  "--out", str(directory / "capture")],
        capture_output=True, text=True, timeout=450)
    if run.returncode:
        raise RuntimeError("census fixture: capture failed: " + run.stderr.strip())
    return directory / "capture"
