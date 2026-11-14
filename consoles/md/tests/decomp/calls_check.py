#!/usr/bin/env python3
"""Per-call verification of C routines against original execution (D04).

An authored cartridge runs on the pinned reference core; `hybrid.py` builds
its authored C into the checker's library; `check_calls.py` replays every
recorded call of every routine through the C and compares registers, flags,
memory effects, hardware accesses and nested call targets. The controls hold
that correct C is IDENTICAL on both CPUs with real call and access counts,
that four planted errors each fail for their own reason, and that a call an
interrupt overlapped, a call that needed the original, a routine never
called and a stream without bus accesses are never IDENTICAL.
"""
import json
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
import intake  # noqa: E402
import trace as nmdh_trace  # noqa: E402


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def verdicts(rom_path, capture, library, out):
    report = check_calls.run(rom_path, capture / "capture.nmdh", library, out)
    require(json.loads(out.read_text()) == report, "the verdict file is not the report")
    return report, {(r["cpu"], r["address"]): r for r in report["routines"]}


def correct(temp, rom_path, capture, catalog):
    library = hybrid.build([calls_fixture.SOURCE], temp / "hybrid", catalog=catalog)
    report, by = verdicts(rom_path, capture, library, temp / "calls.json")
    identities = report["identities"]
    require(report["schema"] == "nmd-calls" and identities["rom_sha256"] and
            identities["library_sha256"] and identities["core_sha256"] and
            identities["scenario_sha256"], "the verdicts do not name what they were made from")

    def routine(name):
        return by[("m68k", calls_fixture.ROUTINES[name])]

    loops = calls_fixture.LOOPS
    for name, calls in (("sum", 3 * loops), ("poke", loops), ("dispatch", loops),
                        ("outer", loops), ("send", loops)):
        row = routine(name)
        require(row["verdict"] == "IDENTICAL" and row["calls"] == calls and
                row["identical"] == calls and row["divergent"] == 0,
                "%s is not IDENTICAL on %d calls: %r" % (name, calls, row))
    require(routine("sum")["compared"]["registers"] > 0 and
            routine("poke")["compared"]["hardware_accesses"] == 2 * loops and
            routine("outer")["compared"]["memory_bytes"] >= 2 * loops and
            routine("dispatch")["compared"]["calls"] == loops,
            "a compared count is missing: %r" % report["routines"])
    for name in ("pad", "tone", "irqon", "dma", "count", "below"):
        require(routine(name)["verdict"] == "IDENTICAL" and routine(name)["calls"] == 1,
                "%s is not IDENTICAL on its call: %r" % (name, routine(name)))
    handlers = [routine("handler%d" % index) for index in range(4)]
    require([row["calls"] for row in handlers] == [2, 2, 1, 1] and
            all(row["verdict"] == "IDENTICAL" for row in handlers),
            "the dispatched handlers are not judged apart: %r" % handlers)
    # mixed CPUs: the byte the 68000 sent is what the Z80 routine reads
    fetch = by[("z80", calls_fixture.Z_FETCH)]
    require(fetch["verdict"] == "IDENTICAL" and fetch["identical"] > 100 and
            fetch["compared"]["hardware_accesses"] >= 2 * fetch["identical"] and
            routine("send")["compared"]["hardware_accesses"] >= 4 * loops,
            "the Z80 routine or the send to it is not IDENTICAL: %r" % fetch)
    # an interrupted call is judged without the handler's accesses
    spin, relay, unused = routine("spin"), routine("relay"), routine("unused")
    require(spin["verdict"] == "IDENTICAL" and spin["calls"] == 1 and spin["identical"] == 1,
            "an interrupted call was not judged apart from its handler: %r" % spin)
    # never IDENTICAL: a call that used the original, no call at all
    require(relay["verdict"] == "UNVERIFIED" and relay["calls"] == loops and
            relay["unverified"] == {"fallback": loops} and relay["fallbacks"] == loops,
            "a call the original stood in for was judged IDENTICAL: %r" % relay)
    require(unused["verdict"] == "UNCOVERED" and unused["calls"] == 0,
            "a routine no call reached is not UNCOVERED: %r" % unused)
    return fetch["identical"], sum(row["calls"] for row in report["routines"]), report


def planted(temp, rom_path, capture):
    """Each planted error fails its routine for the reason planted."""
    cases = (
        ("PLANT_FLAGS", "sum", "register", "sr"),
        ("PLANT_LOAD", "sum", "register", "d0"),
        ("PLANT_STORE", "poke", "hardware", "value"),
        ("PLANT_TARGET", "dispatch", "call", "target"),
        ("PLANT_RETURN", "outer", "register", "pc"),
        ("PLANT_MEMORY", "outer", "memory", "missing"),
        ("PLANT_ORDER", "poke", "hardware", "access"),
        ("PLANT_IRQ", "irqon", "register", "sr"),
        ("PLANT_DMA", "dma", "hardware", "value"),
        ("PLANT_SAMPLE", "tone", "hardware", "value"),
    )
    for define, name, kind, detail in cases:
        library = hybrid.build([calls_fixture.SOURCE], temp / ("hybrid-" + define),
                               defines=[define])
        _, by = verdicts(rom_path, capture, library, temp / (define + ".json"))
        row = by[("m68k", calls_fixture.ROUTINES[name])]
        first = row.get("first_difference") or {}
        require(row["verdict"] == "DIVERGENT" and row["divergent"] > 0 and
                first.get("kind") == kind and detail in json.dumps(first),
                "%s: %s is not DIVERGENT by %s (%s): %r" % (define, name, kind, detail, row))
        others = [r for r in by.values() if r["verdict"] == "DIVERGENT" and r is not row]
        allowed = {"PLANT_FLAGS": {"outer"}, "PLANT_LOAD": {"outer"}}.get(define, set())
        require({r["name"] for r in others} <= allowed,
                "%s made other routines diverge: %r" % (define, [r["name"] for r in others]))
    return len(cases)


def interrupted_difference(temp, rom_path, capture):
    """A difference in an interrupted call may be the interrupt's: UNVERIFIED."""
    library = hybrid.build([calls_fixture.SOURCE], temp / "hybrid-spin", defines=["PLANT_SPIN"])
    _, by = verdicts(rom_path, capture, library, temp / "spin.json")
    spin = by[("m68k", calls_fixture.ROUTINES["spin"])]
    require(spin["verdict"] == "UNVERIFIED" and spin["unverified"] == {"interrupt": 1} and
            spin["divergent"] == 0,
            "a difference in an interrupted call was not UNVERIFIED: %r" % spin)


def refusals(temp, rom_path, capture, library):
    """A table address the catalog does not hold, and a stream with no bus accesses."""
    try:
        hybrid.build([calls_fixture.SOURCE], temp / "hybrid-wrong",
                     catalog={"routines": [{"space": "rom", "cpu": "m68k", "at": 0x400}]})
    except hybrid.HybridError as exc:
        require("not a catalog entry" in str(exc), "hybrid refused for another reason: %s" % exc)
    else:
        raise AssertionError("a routine table the catalog does not hold was built")
    # the same run with its bus accesses left out
    metadata, _ = nmdh_trace._scan(capture / "capture.nmdh")
    blind = temp / "blind.nmdh"
    writer = nmdh_trace.NMDHWriter(blind, dict(metadata, capabilities=metadata["capabilities"]
                                               & ~(1 << 7)), 1 << 30)
    sequence = 0
    for event in nmdh_trace.read_nmdh(capture / "capture.nmdh"):
        if event["kind"] != 3:
            sequence += 1
            writer.write_event(dict(event, sequence=sequence))
    writer.finish()
    try:
        check_calls.run(rom_path, blind, library, temp / "blind.json")
    except check_calls.CheckError as exc:
        require("bus accesses" in str(exc), "refused for another reason: %s" % exc)
    else:
        raise AssertionError("a stream without bus accesses was checked")
    try:
        check_calls.run(rom_path.parent / "z80.bin", capture / "capture.nmdh", library,
                        temp / "other.json")
    except check_calls.CheckError as exc:
        require("different ROM" in str(exc), "refused for another reason: %s" % exc)
    else:
        raise AssertionError("a stream of another ROM was checked")


def window(temp, rom_path, scenario_path, library):
    """A light capture's frame window is checked the same way: calls that
    begin and end inside it are judged, and none differs."""
    light = census_fixture.capture(rom_path, scenario_path, temp / "light", frames=4,
                                   extra=("--evidence", "light", "--window", "2:4"))
    report = check_calls.run(rom_path, light / "window.nmdh", library, temp / "window.json")
    rows = {(r["cpu"], r["address"]): r for r in report["routines"]}
    fetch = rows[("z80", calls_fixture.Z_FETCH)]
    require(report["window"]["first_frame"] == 2 and fetch["verdict"] == "IDENTICAL" and
            fetch["identical"] > 100 and
            not any(r["verdict"] == "DIVERGENT" for r in report["routines"]),
            "a frame window's calls are not judged as the full stream's: %r" % fetch)
    return fetch["identical"]


def live(temp, rom_path, scenario_path, library, stream_report):
    """With no stream file: the scenario run live gives the stream's verdicts."""
    report = check_calls.run_live(census_fixture.CORE, rom_path, scenario_path, 4, library,
                                  temp / "live.json")
    def verdicts(rows):
        return {(r["cpu"], r["address"]): (r["verdict"], r["calls"], r["identical"],
                                           r["divergent"]) for r in rows}
    require(verdicts(report["routines"]) == verdicts(stream_report["routines"]),
            "a live run's verdicts differ from the stream's")
    planted = hybrid.build([calls_fixture.SOURCE], temp / "hybrid-live-plant",
                           defines=["PLANT_FLAGS"])
    report = check_calls.run_live(census_fixture.CORE, rom_path, scenario_path, 4, planted,
                                  temp / "live-plant.json")
    row = next(r for r in report["routines"] if r["name"] == "sum")
    require(row["verdict"] == "DIVERGENT", "a planted error passed a live run: %r" % row)


def native_source(temp):
    """The same C compiles without the hybrid's macro, against nmd's headers alone."""
    result = subprocess.run(
        [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
         "-I" + str(ROOT / "consoles/md/include"),
         "-I" + str(ROOT / "consoles/md/tools/decomp/include"), str(calls_fixture.SOURCE)],
        capture_output=True, text=True)
    require(result.returncode == 0, "the C does not compile for a native cart: " + result.stderr)


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-calls-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path = calls_fixture.build(temp / "rom")
        capture = census_fixture.capture(rom_path, scenario_path, temp / "capture", frames=4)
        _, catalog, _ = intake.run_intake(rom_path, temp / "intake", [capture], "call-control")
        z80_calls, calls, stream_report = correct(temp, rom_path, capture, catalog)
        errors = planted(temp, rom_path, capture)
        interrupted_difference(temp, rom_path, capture)
        refusals(temp, rom_path, capture, temp / "hybrid" / hybrid.LIBRARY)
        windowed = window(temp, rom_path, scenario_path, temp / "hybrid" / hybrid.LIBRARY)
        native_source(temp)
        live(temp, rom_path, scenario_path, temp / "hybrid" / hybrid.LIBRARY, stream_report)
    print("calls_check: ok (%d calls replayed through C, %d of the Z80 routine; 68000, Z80 and "
          "the byte sent between them IDENTICAL; %d planted errors DIVERGENT for their reason; "
          "an interrupted call judged apart from its handler, UNVERIFIED when it differs; fallback and uncalled routines never IDENTICAL; 3 refusals; "
          "%d calls IDENTICAL from a frame window; the same verdicts from a live run; "
          "the C compiles for a native cart)"
          % (calls, z80_calls, errors, windowed))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("calls_check: " + str(exc))
