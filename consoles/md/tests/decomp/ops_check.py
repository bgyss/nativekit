#!/usr/bin/env python3
"""nmd_decomp.h's flag helpers against the reference core's own execution.

An authored cartridge runs each claimed 68000 and Z80 operation over pairs
of operands with the flags set both ways. For every such instruction the
capture holds the registers before and after; the helper is given the same
operands and must give the same result and the same flags. The vectors are
the reference core's, not the helper's or the decoder's. Nothing here comes
from a game.
"""
import ctypes
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import calls_fixture  # noqa: E402
import census_fixture  # noqa: E402
import check_calls  # noqa: E402
import hybrid  # noqa: E402
import m68k_dis  # noqa: E402
import trace as nmdh_trace  # noqa: E402
import z80_dis  # noqa: E402

SHIM = pathlib.Path(__file__).resolve().parent / "ops" / "shim.c"
VALUES = (0, 1, 2, 7, 8, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000,
          0xFFFFFFFF, 0x12345678, 33)
Z_VALUES = (0x00, 0x01, 0x0F, 0x10, 0x7F, 0x80, 0x81, 0xFF, 0x55, 0xAA, 0x28, 0xD7)
M68K_OPS = ("add", "sub", "cmp", "tst", "neg", "lsl", "asl", "lsr", "asr", "rol", "ror")
SIZES = (("b", 8), ("w", 16), ("l", 32))
Z80_OPS = ("add a,b", "adc a,b", "sub b", "sbc a,b", "cp b", "and b", "or b", "xor b",
           "inc a", "dec a")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def m68k_tests():
    """(source line, op index, bits) for each instruction under test."""
    tests = []
    for index, name in enumerate(M68K_OPS):
        for suffix, bits in SIZES:
            operands = "d0" if name in ("tst", "neg") else "d3,d0"
            tests.append(("%s.%s %s" % (name, suffix, operands), index, bits))
    tests.append(("swap d0", len(M68K_OPS), 32))
    tests.append(("btst d3,d0", len(M68K_OPS) + 1, 32))
    return tests


def sources():
    lines = []
    for text, _, _ in m68k_tests():
        for ccr in (0x00, 0x1F):
            lines += ["        move.l  d2,d0", "        move.w  #$%02X,ccr" % ccr, "        " + text]
    m68k = """\
        org     $200
start:  move.w  #0,($A11200).l
        move.w  #$0100,($A11100).l
        move.w  #$0100,($A11200).l
        lea     ($1000).w,a0
        lea     ($A00000).l,a1
        move.w  #{z_last},d1
copy:   move.b  (a0)+,(a1)+
        dbf     d1,copy
        move.w  #0,($A11200).l
        move.w  #$0400,d5
wait:   dbf     d5,wait
        move.w  #$0100,($A11200).l
        move.w  #0,($A11100).l
        lea     (vals,pc),a2
outer:  move.l  (a2)+,d2
        lea     (vals,pc),a3
inner:  move.l  (a3)+,d3
%s
        cmpa.l  #valsend,a3
        bne.w   inner
        cmpa.l  #valsend,a2
        bne.w   outer
done:   bra.s   done
vals:   dc.l    %s
valsend:
""" % ("\n".join(lines), ",".join("$%X" % value for value in VALUES))
    tests = []
    for text in Z80_OPS:
        for carry in ("or a", "scf"):
            tests += ["        ld      a,d", "        ld      b,e", "        " + carry,
                      "        " + text]
    z80 = """\
        org     0
        di
        ld      sp,$1FF0
        ld      hl,vals
outer:  ld      d,(hl)
        inc     hl
        push    hl
        ld      hl,vals
inner:  ld      e,(hl)
        inc     hl
%s
        ld      a,l
        cp      valsend&255
        jp      nz,inner
        pop     hl
        ld      a,l
        cp      valsend&255
        jp      nz,outer
done:   jr      done
vals:   db      %s
valsend:
""" % ("\n".join(tests), ",".join("$%02X" % value for value in Z_VALUES))
    return m68k, z80


def build_rom(directory):
    directory.mkdir(parents=True, exist_ok=True)
    m68k, z80 = sources()
    program = census_fixture._assemble("z80", "vasmz80_oldstyle", z80, directory)
    code = census_fixture._assemble("m68k", "vasmm68k_mot", m68k.format(z_last=len(program) - 1),
                                    directory, ("-m68000", "-no-opt"))
    require(len(code) <= 0xE00 and len(program) <= 0x400,
            "ops fixture: 68000 image %d bytes, Z80 %d" % (len(code), len(program)))
    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    stray = 0x200 + len(code) + (len(code) & 1)
    rom[stray:stray + 2] = b"\x4E\x73"                  # rte
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = stray.to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT OPS CONTROL".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT OPS CONTROL".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x200 + len(code)] = code
    rom[0x1000:0x1000 + len(program)] = program
    path = directory / "ops-control.bin"
    path.write_bytes(rom)
    scenario = calls_fixture.build(directory / "scenario")[1]
    return path, scenario, bytes(rom), program


def build_shim(directory, defines=()):
    directory.mkdir(parents=True, exist_ok=True)
    library = directory / hybrid.LIBRARY
    command = [os.environ.get("CC", "cc"), "-std=c11", "-O1", "-fPIC", "-shared", "-Wall",
               "-Wextra", "-Werror", *("-I" + str(path) for path in hybrid.INCLUDES),
               *("-D" + name for name in defines), "-o", str(library), str(SHIM)]
    result = subprocess.run(command, capture_output=True, text=True)
    require(result.returncode == 0, "ops shim did not build: " + result.stderr)
    shim = ctypes.CDLL(str(library))
    shim.ops_m68k.restype = ctypes.c_uint32
    shim.ops_m68k.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32,
                              ctypes.POINTER(ctypes.c_uint16)]
    shim.ops_z80.restype = ctypes.c_uint8
    shim.ops_z80.argtypes = [ctypes.c_int, ctypes.c_uint8, ctypes.c_uint8,
                             ctypes.POINTER(ctypes.c_uint8)]
    return shim


def vectors(rom, program, stream):
    """Every instruction under test with the registers around it:
    ("m68k"|"z80", op, bits, text, before, after)."""
    m68k = {text: (op, bits) for text, op, bits in m68k_tests()}
    z80 = {text: index for index, text in enumerate(Z80_OPS)}
    last = {1: None, 2: None}
    found = []
    for event in nmdh_trace.read_nmdh(stream):
        if event["kind"] != 1:
            continue
        cpu, pc = event["cpu"], event["pc"]
        if cpu == 1:
            after = check_calls.m68k_registers(event["payload"]["state"], 0)
            insn = m68k_dis.decode(rom, pc, pc)
            if insn and insn.text in m68k and last[1] is not None and pc >= 0x200:
                found.append(("m68k", *m68k[insn.text], insn.text, last[1], after))
        else:
            after = check_calls.z80_registers(event["payload"]["state"], 0)
            insn = z80_dis.decode(program, pc, pc) if pc < len(program) else None
            if insn and insn.text in z80 and last[2] is not None and pc > 0x0E:
                found.append(("z80", z80[insn.text], 8, insn.text, last[2], after))
        last[cpu] = after
    return found


def compare(shim, found):
    """(checked, {text: first mismatch})."""
    wrong = {}
    for cpu, op, bits, text, before, after in found:
        if cpu == "m68k":
            sr = ctypes.c_uint16(before["sr"])
            result = shim.ops_m68k(op, bits, before["d0"], before["d3"], ctypes.byref(sr))
            got, expected = (result, sr.value & 0x1F), (after["d0"], after["sr"] & 0x1F)
            operands = (before["d0"], before["d3"], before["sr"] & 0x1F)
        else:
            flags = ctypes.c_uint8(before["f"])
            result = shim.ops_z80(op, before["a"], before["b"], ctypes.byref(flags))
            got, expected = (result, flags.value), (after["a"], after["f"])
            operands = (before["a"], before["b"], before["f"])
        if got != expected:
            wrong.setdefault(text, "%s with %s gave result $%X flags $%02X, the core $%X $%02X"
                             % ((text, tuple("$%X" % value for value in operands)) + got + expected))
    return len(found), wrong


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-ops-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario, rom, program = build_rom(temp / "rom")
        capture = census_fixture.capture(rom_path, scenario, temp / "capture", frames=12,
                                         extra=("--max-bytes", "1000000000"))
        found = vectors(rom, program, capture / "capture.nmdh")
        m68k_count = sum(1 for row in found if row[0] == "m68k")
        z80_count = len(found) - m68k_count
        require(m68k_count == len(VALUES) ** 2 * len(m68k_tests()) * 2,
                "the capture holds %d of the 68000 vectors" % m68k_count)
        require(z80_count == len(Z_VALUES) ** 2 * len(Z80_OPS) * 2,
                "the capture holds %d of the Z80 vectors" % z80_count)
        checked, wrong = compare(build_shim(temp / "shim"), found)
        require(not wrong, "helpers differ from the reference core:\n  " +
                "\n  ".join(wrong[text] for text in sorted(wrong)))
        for define, expected in (("PLANT_M68K", {"sub.b d3,d0", "sub.w d3,d0", "sub.l d3,d0"}),
                                 ("PLANT_Z80", {"cp b"})):
            _, planted = compare(build_shim(temp / define, [define]), found)
            require(set(planted) == expected,
                    "%s was caught as %r" % (define, sorted(planted)))
    print("ops_check: ok (%d 68000 and %d Z80 instructions of the reference core, %d operations, "
          "same result and flags from the helpers; 2 planted helper errors caught)"
          % (m68k_count, z80_count, len(m68k_tests()) + len(Z80_OPS)))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("ops_check: " + str(exc))
