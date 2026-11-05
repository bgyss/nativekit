"""Authored synthetic Mega Drive cartridges for NMDH core checks."""
import json
from pathlib import Path
import subprocess


def _assemble(assembler, directory, name, source_text):
    source = directory / (name + ".s")
    binary = directory / (name + ".bin")
    source.write_text(source_text)
    result = subprocess.run([str(assembler), "-Fbin", "-o", str(binary), str(source)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("vasm failed for " + name + ": " + result.stderr)
    return binary.read_bytes()


def build_m68k_fixture(directory, assembler):
    """Write a ROM with nested calls and copied work-RAM code; return its expectations."""
    directory = Path(directory)
    ram_code = _assemble(assembler, directory, "ram-code", """\
        addq.l #3,d0
        move.l #$5a5a5a5a,d2
        nop
ram_loop:
        bra.s ram_loop
""")
    if len(ram_code) % 2:
        raise RuntimeError("RAM fixture code must be word aligned")
    words_to_copy = len(ram_code) // 2
    main = _assemble(assembler, directory, "main", """\
        move.l #$11223344,d0
        jsr $00000280
        movea.l #$00000400,a0
        movea.l #$00ff0100,a1
        move.w #%d,d1
copy_loop:
        move.w (a0)+,(a1)+
        dbra d1,copy_loop
        movea.l #$00ff0100,a1
        jmp (a1)
""" % (words_to_copy - 1))
    subroutine = _assemble(assembler, directory, "subroutine", """\
        addq.l #1,d0
        jsr $000002a0
        rts
""")
    nested = _assemble(assembler, directory, "nested", """\
        addq.l #2,d0
        rts
""")

    if len(main) >= 0x80 or len(subroutine) >= 0x20 or len(nested) >= 0x20:
        raise RuntimeError("synthetic program overlaps a fixed fixture address")
    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x200).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT SYNTHETIC".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT SYNTHETIC".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x200 + len(main)] = main
    rom[0x280:0x280 + len(subroutine)] = subroutine
    rom[0x2A0:0x2A0 + len(nested)] = nested
    rom[0x400:0x400 + len(ram_code)] = ram_code

    rom_path = directory / "nmdh-m68k-synthetic.bin"
    rom_path.write_bytes(rom)
    scenario = {
        "version": 1,
        "region": "U",
        "model": "md1va3",
        "pad1": "3",
        "pad2": "3",
        "input": {},
        "required_capabilities": [
            "m68k_instruction", "m68k_state", "flow_edge", "fetch_provenance",
        ],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
    }
    scenario_path = directory / "nmdh-m68k-synthetic.json"
    scenario_path.write_text(json.dumps(scenario, sort_keys=True, separators=(",", ":")))
    expected = {
        "main_pc": 0x200,
        "first_bytes": list(main[:2]),
        "subroutine_pc": 0x280,
        "nested_pc": 0x2A0,
        "ram_source_pc": 0x400,
        "ram_pc": 0x00FF0100,
        "ram_bytes": list(ram_code),
        "m68k_code_bytes": len(main) + len(subroutine) + len(nested) + len(ram_code),
    }
    return rom_path, scenario_path, expected


def build_z80_fixture(directory, m68k_assembler, z80_assembler):
    """Write a synthetic 68000 loader that starts Z80 code from its real RAM map."""
    directory = Path(directory)
    z80_code = _assemble(z80_assembler, directory, "z80-code", """\
        ld sp,$1ff0
        ld a,$7f
        ld ($0110),a
        ld b,$02
        exx
        ld b,$5a
        exx
        ld ix,$1234
        call subroutine
        jr branch
subroutine:
        inc a
        xor a
        ret
branch:
        ld hl,loop
        jp (hl)
loop:
        inc b
        jr loop
""")
    if len(z80_code) >= 0x100:
        raise RuntimeError("Z80 fixture code exceeds its RAM window")

    m68k_lines = [
        "        move.w #0,$a11200",
        "        move.w #$0100,$a11100",
        "        move.w #$0100,$a11200",
    ]
    for offset, value in enumerate(z80_code):
        m68k_lines.append("        move.b #$%02x,$%08x" % (value, 0xA00000 + offset))
    m68k_lines.extend(("        move.w #0,$a11100",
                       "loop68: addq.l #1,d3",
                       "        bra.s loop68"))
    m68k_code = _assemble(m68k_assembler, directory, "m68k-z80-loader",
                          "\n".join(m68k_lines) + "\n")

    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x200).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT SYNTHETIC".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT SYNTHETIC".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x200 + len(m68k_code)] = m68k_code
    rom_path = directory / "nmdh-z80-synthetic.bin"
    rom_path.write_bytes(rom)

    scenario = {
        "version": 1,
        "region": "U",
        "model": "md1va3",
        "pad1": "3",
        "pad2": "3",
        "input": {},
        "required_capabilities": [
            "m68k_instruction", "m68k_state", "z80_instruction", "z80_state",
            "flow_edge", "fetch_provenance",
        ],
        "stop": {"cpu": "z80", "criterion": "frame_limit", "action": "stop"},
    }
    scenario_path = directory / "nmdh-z80-synthetic.json"
    scenario_path.write_text(json.dumps(scenario, sort_keys=True, separators=(",", ":")))

    subroutine = z80_code.find(bytes((0x3C, 0xAF, 0xC9)))
    if subroutine < 0:
        raise RuntimeError("Z80 subroutine bytes were not emitted as expected")
    prefix_pc = z80_code.find(bytes((0xDD, 0x21)))
    call_pc = z80_code.index(0xCD)
    if prefix_pc < 0:
        raise RuntimeError("Z80 IX prefix was not emitted as expected")
    expected = {
        "z80_code_bytes": len(z80_code),
        "m68k_code_bytes": len(m68k_code),
        "z80_entry_pc": 0,
        "z80_immediate_pc": 3,
        "wz_instruction_pc": 5,
        "alternate_pc": prefix_pc - 1,
        "prefix_pc": prefix_pc,
        "call_pc": call_pc,
        "subroutine_pc": subroutine,
        "xor_pc": subroutine + 1,
        "ret_pc": subroutine + 2,
        "return_pc": call_pc + 3,
        "branch_pc": call_pc + 3,
        "branch_target_pc": subroutine + 3,
        "computed_jump_pc": subroutine + 6,
        "loop_pc": subroutine + 7,
    }
    return rom_path, scenario_path, expected
