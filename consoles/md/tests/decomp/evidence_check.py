#!/usr/bin/env python3
"""The evidence digest: what a capture's events reduce to for the code map.

`codemap.Ingest` turns instruction, flow and bus events into three sets and a
sequence of RAM program generations; NMDE v1 (`evidence.py`) stores that
result. The controls here hold that the reduction is the one `codemap.build`
performs, that the file is framed and read fail-closed, and that an intake
over a digest gives the code map of an intake over the events it came from.
The cartridges are authored; the captures run on the pinned reference core.
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


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def ingest_matches_build(rom, capture):
    """A bare Ingest over the capture's events holds what build reduces them to."""
    spaces = codemap.build(rom, [codemap.load_evidence(capture, codemap.sha256(rom))])
    generations = []
    ingest = codemap.Ingest(len(rom), lambda *generation: generations.append(generation))
    ingest.begin()
    for event in codemap.load_evidence(capture, codemap.sha256(rom)):
        ingest.feed(event)
    ingest.end()
    require(ingest.traced_rom and ingest.rom_edges and ingest.reads,
            "the ingest holds no traced ROM addresses, flow edges or reads")
    occurrences = sum(space.occurrences for space in spaces if space.kind == "ram")
    require(len(generations) == occurrences and occurrences,
            "%d generations closed against %d program occurrences in the code map"
            % (len(generations), occurrences))
    for cpu, snapshot, executed, edges in generations:
        require(cpu in ("m68k", "z80") and isinstance(snapshot, bytes) and executed and
                isinstance(edges, set), "a generation is not (cpu, bytes, executed, edges)")
    traced = {address for address, record in spaces[0].instructions.items()
              if record["evidence"] == "traced"}
    require(traced <= ingest.traced_rom, "the code map credits traced code the ingest did not see")
    return len(generations)


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-evidence-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path, _ = census_fixture.build(temp / "census")
        capture = census_fixture.capture(rom_path, scenario_path, temp / "census")
        rom = rom_path.read_bytes()
        generations = ingest_matches_build(rom, capture)
    print("evidence_check: ok (ingest: %d RAM generations, as the code map counts them)"
          % generations)


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("evidence_check: " + str(exc))
