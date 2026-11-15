#!/usr/bin/env python3
"""Lockstep: all C routines substituted at once on the live machine (D05).

The per-call control cartridge runs twice on the pinned reference core, once
untouched and once with its authored C run in place of the routines on both
CPUs, each hardware access made at the clock the original's began. After
every frame the picture, the sound and each region of the comparison
snapshot must be equal. The controls hold that correct C is IDENTICAL with
real frames, calls and accesses; that calls an interrupt overlapped, calls
calls that need a routine with no C are left to the original and keep the run
from being certified; that a call horizontal interrupts land in, one that
runs past the end of a frame, and one that polls the VDP through a vertical
blank across a frame's end, are substituted, pausing where the original was
interrupted or the frame ended; that a call with a TRAP in it is substituted,
the machine taking the TRAP and running its handler; that nine planted
defects (access order, memory, interrupt mask, DMA length, a sound sample,
inside an interrupted call, inside a frame-spanning call, after a TRAP,
another frame's input) are each DIVERGENT where planted; and that a run that
overruns its time stops with no worker left behind.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import calls_fixture  # noqa: E402
import census_fixture  # noqa: E402
import hybrid  # noqa: E402
import lockstep  # noqa: E402

FRAMES = 4


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def lock(temp, name, rom, scenario, defines=(), **options):
    library = hybrid.build([calls_fixture.SOURCE], temp / ("hybrid-" + name), defines=defines,
                           runtime="live")
    return lockstep.run(census_fixture.CORE, rom, scenario, FRAMES, library, temp / name,
                        **options)


def correct(temp, rom, scenario):
    report = lock(temp, "correct", rom, scenario)
    rows = {(row["cpu"], row["name"]): row for row in report["routines"]}
    require(report["verdict"] == "IDENTICAL" and report["frames_compared"] == FRAMES and
            report["divergence"] is None, "correct C is not IDENTICAL: " + lockstep.summary(report))
    loops = calls_fixture.LOOPS
    for name, calls in (("sum", 3 * loops), ("poke", loops), ("dispatch", loops),
                        ("outer", loops), ("send", loops), ("pad", 1), ("tone", 1),
                        ("irqon", 1), ("dma", 1)):
        row = rows[("m68k", name)]
        require(row["calls"] == calls and not row["left_to_original"] and row["substituted"] > 0,
                "%s was not substituted on every call: %r" % (name, row))
    # sum and the handlers also run inside outer's and dispatch's C
    require(rows[("m68k", "sum")]["substituted"] == loops and
            rows[("m68k", "handler0")]["substituted"] == 0,
            "nested calls were substituted apart from their callers")
    # interrupted while it ran: the C paused where the original was, the machine took the
    # horizontal interrupt, and the C went on
    busy = rows[("m68k", "busy")]
    require(busy["substituted"] == 1 and not busy["left_to_original"] and
            report["totals"]["pauses"] >= 3,
            "an interrupted call was not substituted with its pauses: %r %r"
            % (busy, report["totals"]))
    # a TRAP inside: the C paused at it, the machine took it and ran the handler (which
    # has no C), and the C went on with the registers the handler left
    trapper = rows[("m68k", "trapper")]
    require(trapper["substituted"] == 1 and not trapper["left_to_original"],
            "a call with a TRAP in it was not substituted: %r" % trapper)
    # polls the VDP across a frame's end with horizontal interrupts landing in it: the
    # same registers on every pass
    vbwait = rows[("m68k", "vbwait")]
    require(vbwait["substituted"] == 1 and not vbwait["left_to_original"],
            "a polling call that spans a frame was not substituted: %r" % vbwait)
    fetch = rows[("z80", "z_fetch")]
    require(fetch["substituted"] > 1000 and report["totals"]["hardware_accesses"] > 2000,
            "the Z80 routine was not substituted with real accesses: %r" % fetch)
    # left to the original, by reason; such a run is not certified
    # runs past the end of a frame: the C paused where the original stood when the
    # frame ended and went on in the next
    spin, relay = rows[("m68k", "spin")], rows[("m68k", "relay")]
    require(spin["substituted"] == 1 and not spin["left_to_original"] and
            report["totals"]["pauses"] >= 5,
            "a call that spans frames was not substituted with its pauses: %r %r"
            % (spin, report["totals"]))
    require(relay["left_to_original"] == {"fallback": loops},
            "a call that needs a routine with no C was substituted: %r" % relay)
    require(report["calls_left_to_original"] > loops and report["certified"] is False,
            "a run with calls left to the original was certified")
    identities = report["identities"]
    require(all(identities[name] for name in ("rom_sha256", "core_sha256", "library_sha256",
                                              "scenario_sha256", "runner_sha256")),
            "the report does not name what it compared")
    return report


def planted(temp, rom, scenario):
    cases = (
        ("PLANT_ORDER", "hardware", "order", None),
        ("PLANT_MEMORY", "frame", None, "work_ram"),
        ("PLANT_IRQ", "register", "another value", None),     # SR at the C's return
        # the words past the shortened length are zero in RAM and in VRAM alike: only
        # the length register left behind shows it
        ("PLANT_DMA", "frame", None, "vdp_registers"),
        ("PLANT_SAMPLE", "frame", None, "audio"),
        ("PLANT_BUSY", "frame", None, "work_ram"),             # inside an interrupted call
        ("PLANT_SPIN", "frame", None, "work_ram"),             # inside a frame-spanning call
        ("PLANT_TRAP", "frame", None, "work_ram"),             # after a TRAP
    )
    for define, kind, what, region in cases:
        report = lock(temp, define, rom, scenario, defines=[define])
        difference = report["divergence"] or {}
        require(report["verdict"] == "DIVERGENT" and report["certified"] is False and
                difference.get("kind") == kind and
                (what is None or what in difference["what"]) and
                (region is None or region in difference["regions"]),
                "%s is not DIVERGENT by %s %s: %s" % (define, kind, region or what,
                                                    lockstep.summary(report)))
    sample = lock(temp, "sample-again", rom, scenario, defines=["PLANT_SAMPLE"])
    require(sample["divergence"]["regions"] == ["audio"],
            "a sample defect shows in more than the sound: %r" % sample["divergence"])
    # the hybrid run fed another pad state: the frames no longer agree
    _, other = calls_fixture.build(temp / "other-input", pressed=("C",))
    report = lock(temp, "input", rom, scenario, hybrid_scenario=other)
    difference = report["divergence"] or {}
    require(report["verdict"] == "DIVERGENT" and (
                difference.get("kind") == "frame" and "work_ram" in difference["regions"] or
                difference.get("kind") == "register" and difference.get("name") == "pad"),
            "another frame input is not DIVERGENT: " + lockstep.summary(report))
    return len(cases) + 1


def overrun(temp, rom, scenario):
    """Out of time: no verdict, and no worker left running."""
    report = lock(temp, "overrun", rom, scenario, timeout=0.3)
    require(report["verdict"] == "INCONCLUSIVE" and "did not finish" in report["reason"] and
            report["certified"] is False, "an overrun gave a verdict: %r" % report)
    left = subprocess.run(["pgrep", "-f", str(temp / "overrun")], capture_output=True, text=True)
    require(not left.stdout.strip(), "a lockstep worker is still running: " + left.stdout)


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-lockstep-") as temp:
        temp = pathlib.Path(temp)
        rom, scenario = calls_fixture.build(temp / "rom")
        report = correct(temp, rom, scenario)
        defects = planted(temp, rom, scenario)
        overrun(temp, rom, scenario)
    print("lockstep_check: ok (%d frames equal with %d calls substituted on both CPUs and %d "
          "hardware accesses made at the original's clocks; %d calls left to the original, "
          "not certified; %d planted defects DIVERGENT where planted; an overrun leaves no worker)"
          % (report["frames_compared"], report["calls_substituted"],
             report["totals"]["hardware_accesses"], report["calls_left_to_original"], defects))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("lockstep_check: " + str(exc))
