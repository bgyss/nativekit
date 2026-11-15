#!/usr/bin/env python3
"""Symbolic control flow over a decoded skeleton (symbolize.py, D08).

The per-call control cartridge is taken in, its skeleton symbolized with the
routines' names, and the copy rebuilt: it must be the image byte for byte,
name every routine it calls, and leave no branch, call or jump to code with
a raw address; a PC-relative reference into a raw span names a label that
cuts the span; the console's hardware addresses are equates. Two planted errors (each label one instruction late, and an
operand naming the instruction after its target) must break the rebuild.
"""
import importlib
import json
import pathlib
import re
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import calls_fixture  # noqa: E402
import census_fixture  # noqa: E402
import intake  # noqa: E402
import skeleton  # noqa: E402
import symbolize  # noqa: E402

RAW_FLOW = re.compile(r"^\t(?:b[a-z]{2,3}|bsr|bra|db[a-z]{1,2}|jsr|jmp)(?:\.[sw])?\s.*\$[0-9A-F]+")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(module, skeleton_dir, codemap, out, rom_path, names):
    report = module.symbolize(skeleton_dir, codemap, out, names)
    return report, skeleton.rebuild(out, rom_path)


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-symbolize-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path = calls_fixture.build(temp / "rom")
        capture = census_fixture.capture(rom_path, scenario_path, temp / "capture", frames=4)
        intake.run_intake(rom_path, temp / "intake", [capture], "call-control")
        codemap = json.loads((temp / "intake" / "codemap.json").read_text())
        names = {address: name for name, address in calls_fixture.ROUTINES.items()}
        report, rebuilt = run(symbolize, temp / "intake", codemap, temp / "sym", rom_path, names)
        require(rebuilt["identical"], "the symbolic copy does not rebuild the image: %r" % rebuilt)
        rom = report["rom.s"]
        text = (temp / "sym" / "asm" / "rom.s").read_text()
        for name in ("sum", "dispatch", "outer", "spin", "unroll"):
            require(re.search(r"^%s:$" % name, text, re.M) and
                    re.search(r"\b(?:bsr|jsr)(?:\.[sw])?\s+%s\b" % name, text),
                    "routine %s is not labelled and called by name" % name)
        # a direct transfer to code keeps no raw address
        # a PC-relative reference into a raw span names a label that cuts the span
        data = re.findall(r"\((dat_[0-9A-F]{6}),pc", text)
        require(data and all(re.search(r"^%s:$" % name, text, re.M) for name in data),
                "PC-relative data references are not labelled: %r" % data)
        # the console's addresses by name, with their equates
        require(re.search(r"^VDP_CTRL\tequ\t\$C00004$", text, re.M) and "(VDP_CTRL).l" in text and
                "(Z80_BUSREQ).l" in text and "($C00004).l" not in text,
                "hardware addresses are not named")
        raw = [line for line in text.split("\n") if RAW_FLOW.match(line) and "(" not in line]
        require(not raw, "branches to code left with raw addresses: %r" % raw[:5])
        require(rom["labels"] >= len(calls_fixture.ROUTINES) and rom["operands"] >= 20,
                "too little was named: %r" % rom)
        source = pathlib.Path(symbolize.__file__)
        original = source.read_text()
        plants = (
            # each label two bytes after its instruction's start
            ('''        if kind == "code" and (address in targets or address in entries):
            out.append(label(address) + ":")''',
             '''        if kind == "code" and (address - 2 in targets or address - 2 in entries):
            out.append(label(address - 2) + ":")'''),
            # an operand names the instruction after its target
            ('''label(target), body, count=1)''',
             '''label(min(t for t in coded if t > target)), body, count=1)'''),
        )
        try:
            for index, (old, new) in enumerate(plants):
                require(original.count(old) == 1, "plant %d no longer matches" % index)
                source.write_text(original.replace(old, new))
                planted = importlib.reload(symbolize)
                try:
                    _, broken = run(planted, temp / "intake", codemap, temp / ("plant%d" % index),
                                    rom_path, names)
                    caught = not broken["identical"]
                except (skeleton.RebuildError, symbolize.SymbolizeError):
                    caught = True
                require(caught, "planted symbolize error %d rebuilt identically" % index)
        finally:
            source.write_text(original)
            importlib.reload(symbolize)
    print("symbolize_check: ok (%d labels and %d operands named, routines by name; rebuild "
          "identical; no transfer to code left raw; %d data labels; %d planted errors break "
          "the rebuild)" % (rom["labels"], rom["operands"], len(set(data)), len(plants)))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("symbolize_check: " + str(exc))
