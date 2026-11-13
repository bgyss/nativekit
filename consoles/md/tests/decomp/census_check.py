#!/usr/bin/env python3
"""Intake controls on authored cartridges: the code map's identities, the
skeleton's rebuild gate, the census and catalog, and the errors each must
catch. The traced control runs on the pinned reference core.
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
import skeleton  # noqa: E402


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def expect_failure(label, action, fragment):
    try:
        action()
    except (skeleton.RebuildError, codemap.CodeMapError) as exc:
        require(fragment in str(exc), "%s failed for another reason: %s" % (label, exc))
        return
    raise AssertionError("planted " + label + " passed its gate")


def edit(path, old, new):
    text = path.read_text()
    require(text.count(old) == 1, "control edit target %r not unique in %s" % (old, path.name))
    path.write_text(text.replace(old, new))
    return text


def traced_control(temp):
    rom_path, scenario_path, facts = census_fixture.build(temp / "fixture")
    capture = census_fixture.capture(rom_path, scenario_path, temp / "fixture")
    rom = rom_path.read_bytes()
    out = temp / "work"
    counts, routines, rebuilt = intake.run_intake(rom_path, out, [capture], "intake-control")
    require(rebuilt["identical"] and rebuilt["length"] == len(rom) and
            rebuilt["sha256"] == codemap.sha256(rom) and rebuilt["differing_bytes"] == 0,
            "decoded skeleton did not rebuild the image")
    document = json.loads((out / "codemap.json").read_text())
    spaces = {space["id"]: space for space in document["spaces"]}
    rom_space = spaces["rom"]

    def class_at(space, address):
        for start, length, name in space["classes"]:
            if start <= address - space["base"] < start + length:
                return name
        raise AssertionError("no class at $%X" % address)

    def evidence_at(space, address):
        return next((i["evidence"] for i in space["instructions"] if i["at"] == address), None)

    # two programs at one Z80 address stay two identities
    programs = [space for space in document["spaces"] if space["kind"] == "ram"]
    require(len(programs) == 2 and all(p["cpu"] == "z80" and p["base"] == 0 for p in programs),
            "expected two Z80 program generations at $0000, got %r"
            % [(p["id"], p["base"]) for p in programs])
    by_hash = {program["sha256"]: program for program in programs}
    program_a = by_hash.get(codemap.sha256(facts["program_a"]))
    program_b = by_hash.get(codemap.sha256(facts["program_b"]))
    require(program_a is not None and program_b is not None and program_a is not program_b,
            "the two uploaded programs were merged or misidentified")
    require(program_a["source_rom_offset"] == facts["a_rom_offset"],
            "program A's verbatim ROM source was not found")
    require(program_b["source_rom_offset"] is None,
            "program B has no verbatim ROM source but one was claimed")
    require(any(u["observed_targets"] for u in program_b["unresolved"]),
            "program B's computed jump has no observed target")

    # dead code, the untaken table entries and decoy bytes are not code
    for name in ("dead", "case0", "case2", "decoy"):
        require(class_at(rom_space, facts[name]) in ("unknown", "read"),
                "%s was classified as code without evidence" % name)
    orphans = {candidate["at"] for candidate in rom_space["orphan_candidates"]}
    require(facts["dead"] in orphans and facts["case2"] in orphans,
            "dead code is not listed as an orphan candidate")
    require(evidence_at(rom_space, facts["case1"]) == "traced",
            "the computed call's taken entry is not traced code")
    unresolved = {u["at"]: u["observed_targets"] for u in rom_space["unresolved"]}
    require(list(unresolved.values()) == [[facts["case1"]]],
            "computed call not recorded as unresolved with its one observed target")
    require(evidence_at(rom_space, facts["stray"]) == "vector",
            "the vector-only handler is not vector evidence")
    require(class_at(rom_space, facts["table"] + 2) == "read" and
            class_at(rom_space, facts["table"]) == "unknown",
            "only the table entry the trace read may be marked read")
    require(not rom_space["conflicts"] and not rom_space["undecodable"],
            "unexpected conflicts or undecodable entries")

    # census and catalog
    rom_counts = next(space for space in counts["spaces"] if space["id"] == "rom")
    require(rom_counts["decoded_code_bytes"]["traced"] > 100 and
            rom_counts["decoded_code_bytes"]["vector"] == 2 and
            rom_counts["raw_code_bytes"] == 0 and
            rom_counts["included_program_bytes"] == len(facts["program_a"]) and
            rom_counts["unknown_bytes"] > 30000,
            "census byte ownership is wrong: %r" % rom_counts)
    tiers = {(r["space"], r["at"]): r["tier"] for r in routines["routines"]}
    require(tiers[("rom", facts["delay"])] == "T1" and tiers[("rom", facts["case1"])] == "T1",
            "traced, labelled entries are not T1")
    require(tiers[("rom", facts["stray"])] is None, "an entry no trace ran was given a tier")
    text = (out / "asm" / "rom.s").read_text()
    require(rom[0x200:0x204].hex() not in text.lower() and "dc.b" not in text,
            "the image skeleton carries ROM bytes instead of baserom references")

    # the gate re-run from the written files alone
    skeleton.rebuild(out, rom_path)

    # planted errors, each restored afterwards
    asm = out / "asm"
    saved = edit(asm / "rom.s", "\tmoveq #11,d7\n", "\tmoveq #12,d7\n")
    expect_failure("changed 68000 instruction", lambda: skeleton.rebuild(out, rom_path),
                   "image differs: 1 bytes")
    (asm / "rom.s").write_text(saved)

    z80_source = asm / (program_a["id"] + ".s")
    saved = edit(z80_source, "\tand $3\n", "\tand $7\n")
    expect_failure("changed Z80 instruction", lambda: skeleton.rebuild(out, rom_path),
                   "does not rebuild to its captured bytes")
    z80_source.write_text(saved)

    dead_line = '\tincbin\t"baserom.bin",%d,32\t; not code\n' % facts["dead"]
    saved = edit(asm / "rom.s", dead_line,
                 '\tincbin\t"baserom.bin",%d,31\n\tdc.b\t$%02X\n'
                 % (facts["dead"], rom[facts["dead"] + 31] ^ 0x01))
    expect_failure("changed data byte", lambda: skeleton.rebuild(out, rom_path),
                   "image differs: 1 bytes, first at $%X" % (facts["dead"] + 31))
    (asm / "rom.s").write_text(saved)

    saved = edit(asm / "rom.s", dead_line,
                 '\tincbin\t"baserom.bin",%d,31\n' % facts["dead"])
    expect_failure("changed placement", lambda: skeleton.rebuild(out, rom_path), "image differs")
    (asm / "rom.s").write_text(saved)

    other = bytearray(rom)
    other[0x7000] ^= 0xFF
    other_path = temp / "other.bin"
    other_path.write_bytes(other)
    expect_failure("different source ROM", lambda: skeleton.rebuild(out, other_path),
                   "not the image this skeleton was made from")
    expect_failure("capture of a different ROM",
                   lambda: codemap.load_evidence(capture, codemap.sha256(other)),
                   "different ROM")
    expect_failure("missing capture", lambda: codemap.load_evidence(temp / "absent",
                                                                    codemap.sha256(rom)),
                   "manifest unreadable")
    skeleton.rebuild(out, rom_path)   # and the restored skeleton holds again

    # raw preservation: rebuilds, and earns nothing
    raw_counts, raw_routines, raw_rebuilt = intake.run_intake(
        rom_path, temp / "raw", [capture], "intake-control", raw_only=True)
    require(raw_rebuilt["identical"] and raw_counts["mode"] == "raw preservation only",
            "raw preservation did not rebuild or is mislabelled")
    require(all(not any(space["decoded_code_bytes"].values()) for space in raw_counts["spaces"]
                if space["id"] == "rom"),
            "raw preservation was credited with decoded code")
    return rom_counts, len(programs)


def static_control(temp):
    """No trace: a jump into the middle of another instruction, and an
    instruction the assembler would encode differently."""
    rom = bytearray(0x400)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    rom[8:12] = (0x202).to_bytes(4, "big")       # a vector into the overlap
    rom[12:16] = (0x220).to_bytes(4, "big")
    rom[0x100:0x104] = b"SEGA"
    rom[0x200:0x20A] = bytes.fromhex("6004" "203C4E714E71" "4E75")
    #   $200 bra.s $206 | $202 move.l #$4E714E71,d0 | $206 is a nop inside it | $208 rts
    rom[0x220:0x226] = bytes.fromhex("0000FF12" "4E75")   # ori.b with a non-zero high byte
    rom_path = temp / "static.bin"
    rom_path.write_bytes(rom)
    counts, routines, rebuilt = intake.run_intake(rom_path, temp / "static", [], "static-control")
    space = counts["spaces"][0]
    require(rebuilt["identical"], "static control did not rebuild")
    require(space["conflicts"] == 1, "the overlapping interpretations were not kept as a conflict")
    require(space["raw_code_bytes"] == 6 + 4,
            "conflicted and noncanonical code must stay raw: %d" % space["raw_code_bytes"])
    require(space["decoded_code_bytes"]["traced"] == 0 and
            all(routine["tier"] is None for routine in routines["routines"]),
            "untraced code was credited as traced or given a tier")
    text = (temp / "static" / "asm" / "rom.s").read_text()
    require("noncanonical encoding" in text and "overlapping interpretations" in text,
            "raw code lines do not say why they are raw")


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-intake-") as temp:
        temp = pathlib.Path(temp)
        rom_counts, programs = traced_control(temp)
        static_control(temp)
    print("census_check: ok (traced control: %d decoded traced code bytes, %d Z80 program "
          "generations at one address, rebuild identical; 7 planted errors caught; raw "
          "preservation uncredited; overlap and noncanonical code kept raw)"
          % (rom_counts["decoded_code_bytes"]["traced"], programs))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("census_check: " + str(exc))
