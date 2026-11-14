#!/usr/bin/env python3
"""Draft C generated from the original's instructions (draft68k.py).

Every 68000 routine of the per-call control cartridge is drafted and the
drafts are checked per call against the original: each must reach the hand-
written C's verdict, and the drafts must compile. Generating proves nothing;
these verdicts are the drafts' only evidence. Two planted generator errors (a
draft that does not start at its entry when code lies below it, and a
condition code read the wrong way) must make a draft diverge.
"""
import json
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import calls_fixture  # noqa: E402
import census_fixture  # noqa: E402
import check_calls  # noqa: E402
import draft68k  # noqa: E402
import hybrid  # noqa: E402
import intake  # noqa: E402


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def verdicts(report):
    return {row["address"]: (row["verdict"], row["calls"], row["identical"])
            for row in report["routines"] if row["cpu"] == "m68k"}


def drafted_report(temp, label, rom_path, capture, codemap):
    out = temp / (label + ".c")
    drafted, refused = draft68k.write(rom_path.read_bytes(), codemap,
                                      sorted(calls_fixture.ROUTINES.values()), out, standalone=True)
    require(not refused, "drafts refused: %r" % refused)
    library = hybrid.build([out], temp / ("hybrid-" + label))
    return check_calls.run(rom_path, capture / "capture.nmdh", library), len(drafted)


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-draft-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path = calls_fixture.build(temp / "rom")
        capture = census_fixture.capture(rom_path, scenario_path, temp / "capture", frames=4)
        intake.run_intake(rom_path, temp / "intake", [capture], "call-control")
        codemap = json.loads((temp / "intake" / "codemap.json").read_text())
        hand = check_calls.run(rom_path, capture / "capture.nmdh",
                               hybrid.build([calls_fixture.SOURCE], temp / "hybrid-hand"))
        report, count = drafted_report(temp, "drafts", rom_path, capture, codemap)
        want, got = verdicts(hand), verdicts(report)
        require(got == want, "drafts' verdicts differ from the hand C's:\n  %r\n  %r" % (got, want))
        identical = sum(1 for verdict in got.values() if verdict[0] == "IDENTICAL")
        calls = sum(verdict[2] for verdict in got.values())
        source = pathlib.Path(draft68k.__file__)
        original = source.read_text()
        plants = (
            ("        if body[0] != entry:", "        if False:", calls_fixture.ROUTINES["below"]),
            ('"if (nmd_cc(%d)) %s" % (CONDITIONS[base[1:]],',
             '"if (!nmd_cc(%d)) %s" % (CONDITIONS[base[1:]],', calls_fixture.ROUTINES["send"]),
        )
        try:
            for index, (old, new, address) in enumerate(plants):
                require(original.count(old) == 1, "plant %d no longer matches" % index)
                source.write_text(original.replace(old, new))
                import importlib
                importlib.reload(draft68k)
                planted, _ = drafted_report(temp, "plant%d" % index, rom_path, capture, codemap)
                row = next(r for r in planted["routines"] if r["address"] == address)
                require(row["verdict"] == "DIVERGENT",
                        "planted generator error %d was not caught: %r" % (index, row))
        finally:
            source.write_text(original)
    print("draft_check: ok (%d routines drafted, %d IDENTICAL on %d calls, the same verdicts as "
          "the hand C; 2 planted generator errors caught)" % (count, identical, calls))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("draft_check: " + str(exc))
