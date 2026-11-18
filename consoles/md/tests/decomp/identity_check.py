#!/usr/bin/env python3
"""A RAM program whose numeric operands another CPU rewrites is one program
with variants; any other difference keeps two generations apart.

The cartridge is authored. Its 68000 uploads one Z80 program verbatim, lets
it run, then rewrites the low byte of an `ld hl,nn` immediate and sets a RAM
flag that makes the program run a tail it had not reached, then rewrites an
opcode (`inc b` to `dec b`). The first two generations must be one identity
(two variants, one rewritten byte, the longer extent, the ROM source of the
unpatched bytes); the third must stay its own. Decoder-level controls hold
the rule's other refusals, and two planted errors (a rule that merges
anything of equal layout, and no rule) must each fail the control.
"""
import json
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import census_fixture  # noqa: E402
import codemap  # noqa: E402
import intake  # noqa: E402

Z80 = """\
        org     0
        di
        ld      sp,$1FF0
loop:   ld      hl,$1000
        inc     (hl)
        inc     b
        ld      a,($1100)
        or      a
        jr      z,loop
        inc     c
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
        move.b  #0,($A01100).l
        move.w  #0,($A11200).l
        move.w  #$0400,d3
wait0:  dbf     d3,wait0
        move.w  #$0100,($A11200).l
        move.w  #0,($A11100).l
        move.w  #$0400,d3
wait1:  dbf     d3,wait1
        move.w  #$0100,($A11100).l
hold1:  btst    #0,($A11100).l
        bne.s   hold1
        move.b  #$02,($A00000+{immediate}).l
        move.b  #1,($A01100).l
        move.w  #0,($A11100).l
        move.w  #$0400,d3
wait2:  dbf     d3,wait2
        move.w  #$0100,($A11100).l
hold2:  btst    #0,($A11100).l
        bne.s   hold2
        move.b  #$05,($A00000+{opcode}).l
        move.w  #0,($A11100).l
forever:
        bra.s   forever
        org     $3F0
stray:  rte
"""


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def build(directory):
    directory.mkdir(parents=True, exist_ok=True)
    program = census_fixture._assemble("z80", "vasmz80_oldstyle", Z80, directory)
    immediate = program.index(bytes((0x21, 0x00, 0x10))) + 1
    opcode = program.index(bytes((0x04, 0x3A)))
    code = census_fixture._assemble(
        "m68k", "vasmm68k_mot",
        M68K.format(last=len(program) - 1, immediate=immediate, opcode=opcode),
        directory, ("-m68000", "-no-opt"))
    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x3F0).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT IDENTITY CONTROL".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT IDENTITY CONTROL".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x200 + len(code)] = code
    rom[0x400:0x400 + len(program)] = program
    rom_path = directory / "identity-control.bin"
    rom_path.write_bytes(rom)
    scenario = {
        "version": 1, "region": "U", "model": "md1va3", "pad1": "3", "pad2": "3", "input": {},
        "required_capabilities": [
            "m68k_instruction", "m68k_state", "z80_instruction", "z80_state",
            "flow_edge", "fetch_provenance", "master_clock", "bus_intervals",
        ],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
    }
    scenario_path = directory / "identity-control.json"
    scenario_path.write_text(json.dumps(scenario, sort_keys=True, separators=(",", ":")))
    return rom_path, scenario_path, program, immediate, opcode


def judge(programs, program, immediate, opcode):
    """The identities the cartridge must produce, from code map spaces."""
    require(len(programs) == 2, "expected two Z80 identities, got %d" % len(programs))
    family, other = sorted(programs, key=lambda space: -len(space["variant_sha256"]))
    patched = bytearray(program)
    patched[immediate] = 0x02
    require(family["variant_sha256"] == sorted(codemap.sha256(data)
                                               for data in (program, patched)),
            "the operand-patched generation is not a variant of the uploaded program")
    require(family["patched"] == [[immediate, 1]],
            "the rewritten bytes are %r, not the one immediate byte" % family["patched"])
    require(family["size"] == len(program) and family["occurrences"] == 2,
            "the family does not span the tail only its second variant ran")
    require(family["sha256"] == codemap.sha256(program) and
            family["source_rom_offset"] == 0x400,
            "the family is not named by its ROM-sourced variant")
    patched[opcode] = 0x05
    # the third generation never ran the two start-up instructions before the loop
    require(other["base"] == immediate - 1 and
            other["variant_sha256"] == [codemap.sha256(patched[immediate - 1:])] and
            not other["patched"] and other["source_rom_offset"] is None,
            "the opcode-patched generation is not its own identity")


def rule_controls():
    """The refusals, on bytes: jp $0004 / one unreached byte / ld a,5 / ret."""
    base = bytes.fromhex("C30400" "00" "3E05" "C9")
    executed = {0: 3, 4: 2, 6: 1}

    def generation(data):
        return bytes(data) + bytes(codemap.Z80_RAM_SIZE - len(data)), dict(executed), set()

    def admitted(index, value):
        other = bytearray(base)
        other[index] = value
        family = codemap._Family("z80", 0, generation(base))
        return family.admit(generation(other)), family

    merged, family = admitted(5, 0x09)
    require(merged and family.finish(b"").patched == [5], "a rewritten immediate was not merged")
    require(not admitted(1, 0x03)[0], "a rewritten jump target was merged")
    require(not admitted(3, 0xFF)[0], "a differing non-instruction byte was merged")
    require(not admitted(4, 0x06)[0], "a differing register (ld b for ld a) was merged")
    require(not admitted(6, 0x00)[0], "a differing opcode (nop for ret) was merged")
    apart = codemap._Family("z80", 0, generation(base))
    far = (bytes(0x100) + base + bytes(codemap.Z80_RAM_SIZE - 0x100 - len(base)),
           {0x100 + offset: length for offset, length in executed.items()}, set())
    require(not apart.admit(far), "programs at different addresses were merged")


def main():
    rule_controls()
    with tempfile.TemporaryDirectory(prefix="nmd-identity-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path, program, immediate, opcode = build(temp / "fixture")
        capture = census_fixture.capture(rom_path, scenario_path, temp / "fixture")
        rom = rom_path.read_bytes()
        counts, _, rebuilt = intake.run_intake(rom_path, temp / "work", [capture],
                                               "identity-control")
        require(rebuilt["identical"], "the skeleton did not rebuild the image")
        document = json.loads((temp / "work" / "codemap.json").read_text())
        programs = [space for space in document["spaces"] if space["kind"] == "ram"]
        judge(programs, program, immediate, opcode)
        family = max(counts["spaces"][1:], key=lambda space: space["variants"])
        require(family["variants"] == 2 and family["patched_operand_bytes"] == 1 and
                family["included_in_image"],
                "the census does not report the family: %r" % family)
        text = (temp / "work" / "asm" / (family["id"] + ".s")).read_text()
        require(text.count("operand rewritten at run time") == 1 and
                "one of 2 variants" in text, "the skeleton does not mark the rewritten operand")

        # planted errors: the same evidence under a broken rule must fail
        def planted(label, name, owner, replacement):
            saved = getattr(owner, name)
            setattr(owner, name, replacement)
            try:
                spaces = codemap.build(rom, [codemap.load_evidence(capture, codemap.sha256(rom))])
                judge([space.to_json() for space in spaces[1:]], program, immediate, opcode)
            except AssertionError:
                return
            finally:
                setattr(owner, name, saved)
            raise AssertionError("planted " + label + " passed the control")

        planted("merge of any equal layout", "_shape", codemap, lambda text: "")
        planted("absent merge rule", "admit", codemap._Family, lambda self, generation: False)
    print("identity_check: ok (operand-patched generation merged as 1 of 2 variants with 1 "
          "rewritten byte, longer extent and ROM source kept; opcode-patched generation kept "
          "apart; 5 rule refusals; 2 planted errors caught)")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("identity_check: " + str(exc))
