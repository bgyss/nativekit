#!/usr/bin/env python3
"""Build deterministic 68000 test cartridges from cases.py operations."""
import pathlib
import struct
import subprocess

from cases import Operation, READ, TAS, WRITE


MAGIC = b"NMDX"
VERSION = 1
Z80_VERSION = 2
HEADER = struct.Struct(">4sHH")
Z80_HEADER = struct.Struct(">4sHHHH")
RECORD = struct.Struct(">BBHII")
ROM_SIZE = 32768

INTERPRETER = [
    "        lea     steps(pc),a0",
    "        adda.w  #8,a0",
    "next:   moveq   #0,d7",
    "        move.b  (a0)+,d7",
    "        beq.s   done",
    "        moveq   #0,d6",
    "        move.b  (a0)+,d6",
    "        move.w  (a0)+,d5",
    "        movea.l (a0)+,a1",
    "        move.l  (a0)+,d0",
    "        cmpi.b  #1,d7",
    "        beq.s   read_op",
    "        cmpi.b  #2,d7",
    "        beq.s   write_op",
    "        cmpi.b  #3,d7",
    "        beq.s   tas_op",
    "        cmpi.b  #4,d7",
    "        beq.s   poll_op",
    "        bra.s   done",
    "read_op:",
    "        cmpi.b  #1,d6",
    "        beq.s   read8",
    "        cmpi.b  #2,d6",
    "        beq.s   read16",
    "        move.l  (a1),d1",
    "        bra.s   next",
    "read8:  move.b  (a1),d1",
    "        bra.s   next",
    "read16: move.w  (a1),d1",
    "        bra.s   next",
    "write_op:",
    "        cmpi.b  #1,d6",
    "        beq.s   write8",
    "        cmpi.b  #2,d6",
    "        beq.s   write16",
    "        move.l  d0,(a1)",
    "        bra.s   next",
    "write8: move.b  d0,(a1)",
    "        bra.s   next",
    "write16:",
    "        move.w  d0,(a1)",
    "        bra.s   next",
    "tas_op: tas     (a1)",
    "        bra.s   next",
    "poll_op:",
    "        cmpi.b  #1,d6",
    "        beq.s   poll8",
    "poll_read: move.w (a1),d1",
    "        and.w   d5,d1",
    "        cmp.w   d0,d1",
    "        bne.s   poll_read",
    "        bra.s   next",
    "poll8:  move.b  (a1),d1",
    "        and.b   d5,d1",
    "        cmp.b   d0,d1",
    "        bne.s   poll8",
    "        bra.s   next",
    "done:   bra.s   done",
    "steps:",
]


def encode_steps(case, operations=None):
    operations = tuple(case.operations if operations is None else operations)
    if case.z80_fixture_id:
        data = bytearray(Z80_HEADER.pack(MAGIC, Z80_VERSION, len(operations),
                                         case.z80_fixture_id, case.z80_entry))
    else:
        data = bytearray(HEADER.pack(MAGIC, VERSION, len(operations)))
    for op in operations:
        data.extend(RECORD.pack(op.code, op.width, op.compare_mask & 0xFFFF, op.addr & 0xFFFFFFFF,
                                op.value & 0xFFFFFFFF))
    return bytes(data)


def _dc_bytes(data):
    lines = []
    for offset in range(0, len(data), 16):
        values = ",".join(f"${b:02X}" for b in data[offset:offset + 16])
        lines.append("        dc.b    " + values)
    return lines


def _assembler_source(case, operations):
    if case.direct:
        lines = []
        for op in operations:
            if op.code == WRITE:
                size = {1: "b", 2: "w", 4: "l"}[op.width]
                lines.append(f"        move.{size} #${op.value:08X},${op.addr:08X}")
            elif op.code == READ:
                size = {1: "b", 2: "w", 4: "l"}[op.width]
                lines.append(f"        move.{size} ${op.addr:08X},d0")
            elif op.code == TAS and op.width == 1:
                lines.append(f"        tas     ${op.addr:08X}")
            else:
                raise RuntimeError(f"direct ROM case {case.name} has an unsupported step")
        lines.extend(("loop:   bra.s loop", ""))
        return "\n".join(lines)
    header_size = Z80_HEADER.size if case.z80_fixture_id else HEADER.size
    interpreter = [line.replace("#8,a0", f"#{header_size},a0") for line in INTERPRETER]
    return "\n".join(interpreter + _dc_bytes(encode_steps(case, operations))) + "\n"


def build_case(case, out_dir, assembler, z80_assembler=None):
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    operations = list(case.operations)
    if case.z80_program:
        if not case.z80_fixture_id:
            raise RuntimeError("a Z80 assembly fixture needs a nonzero fixture id")
        if z80_assembler is None:
            raise RuntimeError("a Z80 assembler is required for this case")
        z80_source = out_dir / (case.name + ".z80.s")
        z80_binary = out_dir / (case.name + ".z80.bin")
        z80_source.write_text("\n".join(case.z80_program) + "\n")
        result = subprocess.run([str(z80_assembler), "-Fbin", "-o", str(z80_binary),
                                 str(z80_source)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("vasm Z80 failed: " + result.stderr.strip())
        z80_code = z80_binary.read_bytes()
        if not z80_code or len(z80_code) > 0x2000:
            raise RuntimeError("Z80 fixture must fit in the 8 KiB Z80 RAM")
        if not 0 <= case.z80_load_index <= len(operations):
            raise RuntimeError("Z80 load index is outside the case operation list")
        copy_steps = [Operation(WRITE, 1, 0xA00000 + offset, value)
                      for offset, value in enumerate(z80_code)]
        operations[case.z80_load_index:case.z80_load_index] = copy_steps
    elif case.z80_fixture_id:
        raise RuntimeError("a Z80 fixture id requires an assembly program")

    source = out_dir / (case.name + ".s")
    program = out_dir / (case.name + ".program.bin")
    rom_path = out_dir / (case.name + ".bin")
    steps_path = out_dir / (case.name + ".steps")
    source.write_text(_assembler_source(case, operations))
    result = subprocess.run([str(assembler), "-Fbin", "-o", str(program), str(source)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("vasm failed: " + result.stderr.strip())
    code = program.read_bytes()
    if len(code) + 0x200 > 0x4000:
        raise RuntimeError("test interpreter overlaps the ROM data fixture")

    rom_size = case.rom_size or ROM_SIZE
    if rom_size < 0x200 or rom_size > 0x400000:
        raise RuntimeError("synthetic ROM size must be between 512 bytes and 4 MiB")
    rom = bytearray(rom_size)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x00000200).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT BUS TEST".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT BUS TEST".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    for offset, value in case.rom_data:
        if not 0 <= offset < rom_size or not 0 <= value <= 0xFF:
            raise RuntimeError("synthetic ROM data patch is outside the ROM")
        rom[offset] = value
    rom[0x200:0x200 + len(code)] = code
    rom[0x4000:0x4008] = bytes((0x01, 0x23, 0x04, 0x56, 0x07, 0x89, 0x0A, 0xBC))
    rom_path.write_bytes(rom)
    steps_path.write_bytes(encode_steps(case, operations))
    return rom_path, steps_path
