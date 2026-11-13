#!/usr/bin/env python3
"""The evidence digest: what a capture's events reduce to for the code map.

`codemap.Ingest` turns instruction, flow and bus events into three sets and a
sequence of RAM program generations; NMDE v1 (`evidence.py`) stores that
result. The controls here hold that the reduction is the one `codemap.build`
performs, that the file is framed and read fail-closed, and that an intake
over a digest gives the code map of an intake over the events it came from.
The cartridges are authored; the captures run on the pinned reference core.
"""
import hashlib
import json
import pathlib
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import census_fixture  # noqa: E402
import codemap  # noqa: E402
import evidence  # noqa: E402
import trace as nmdh_trace  # noqa: E402

METADATA = {
    "abi_version": 1, "capabilities": 0xFFF, "options": {"region": "U"},
    "identities": {field: "%064x" % (index + 1)
                   for index, field in enumerate(nmdh_trace.IDENTITY_FIELDS)},
}
SETS = {"traced_rom": [0x200, 0x204], "rom_edges": [[0x210, 0x300]], "reads": [[0x400, 2]]}


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


def expect_refusal(label, action, fragment):
    try:
        action()
    except (evidence.DigestError, nmdh_trace.NMDHError) as exc:
        require(fragment in str(exc), "%s refused for another reason: %s" % (label, exc))
        return
    raise AssertionError("planted " + label + " was read")


def raw_digest(path, records, metadata=METADATA, status=0, count=None, tail=b""):
    """Frame records (JSON values or ready bytes) as an NMDE file by hand."""
    metadata_bytes = nmdh_trace.canonical_json(metadata)
    body = nmdh_trace.HEADER.pack(evidence.MAGIC, evidence.VERSION, 0, len(metadata_bytes))
    body += metadata_bytes
    for record in records:
        if not isinstance(record, bytes):
            record = zlib.compress(nmdh_trace.canonical_json(record), 6)
        body += nmdh_trace.RECORD_LENGTH.pack(len(record)) + record
    footer = nmdh_trace.FOOTER.pack(evidence.FOOTER_MAGIC, len(records) if count is None else count,
                                    status, b"\0" * 3, hashlib.sha256(body).digest())
    path.write_bytes(body + footer + tail)
    return path


def digest_round_trip(temp):
    """Generations and sets come back as written; a generation costs its changed bytes."""
    z80 = bytearray(codemap.Z80_RAM_SIZE)
    z80[0:4] = b"\xF3\x21\x00\x10"
    z80_generations = [("z80", bytes(z80), {0: 1, 1: 3}, {(4, 0)})]
    z80[2:4] = b"\x34\x12"
    z80_generations.append(("z80", bytes(z80), {1: 3}, set()))
    work = bytearray(range(256)) * (codemap.WORK_RAM_SIZE // 256)
    work_generations = []
    for step in range(3):      # 68000 code in work RAM: a 64 KB image, 5 bytes changed a time
        for index in range(5):
            work[0x8000 + step * 16 + index] ^= 0xFF
        work_generations.append(("m68k", bytes(work), {0x8000: 2, 0x8002: 4},
                                 {(0xFF8002, 0xFF8000)}))
    written = [z80_generations[0], work_generations[0], z80_generations[1],
               work_generations[1], work_generations[2]]
    path = temp / "round.nmde"
    writer = evidence.DigestWriter(path, METADATA, 1 << 20)
    for generation in written:
        writer.generation(*generation)
    require(writer.finish({0x200, 0x204}, {(0x210, 0x300)}, {(0x400, 2)}) == path and
            path.is_file() and not pathlib.Path(str(path) + ".partial").exists(),
            "the writer did not leave the finished digest at its path")
    digest = evidence.read_digest(path)
    require(digest.metadata == METADATA, "metadata changed in the round trip")
    require(digest.traced_rom == {0x200, 0x204} and digest.rom_edges == {(0x210, 0x300)} and
            digest.reads == {(0x400, 2)}, "the sets changed in the round trip")
    read = list(digest.generations())
    require(read == written, "the generations changed in the round trip")
    require(list(digest.generations()) == written, "a second reading differs")
    size = path.stat().st_size
    require(size < 4096, "five generations with 12 changed bytes cost %d bytes" % size)
    return size


def digest_mutations(temp):
    """Each damaged or malformed digest is refused, for its own reason."""
    generation = {"cpu": "z80", "delta": [[0, "f3"]], "executed": [[0, 1]], "edges": []}
    good = raw_digest(temp / "good.nmde", [generation, SETS])
    digest = evidence.read_digest(good)
    require(len(list(digest.generations())) == 1 and digest.traced_rom == {0x200, 0x204},
            "the hand-framed control digest does not read")
    data = good.read_bytes()
    footer = nmdh_trace.FOOTER.size
    first = nmdh_trace.HEADER.size + len(nmdh_trace.canonical_json(METADATA))
    length = nmdh_trace.RECORD_LENGTH.unpack(data[first:first + 4])[0]
    flipped = bytearray(data)
    flipped[first + 6] ^= 0x40
    cases = [
        ("flipped record byte", bytes(flipped), "footer count or digest mismatch"),
        ("removed record", data[:first] + data[first + 4 + length:],
         "footer count or digest mismatch"),
        ("trailing bytes", data + b"\0", "trailing bytes after NMDE footer"),
        ("truncated file", data[:-footer - 3], "NMDE"),
    ]
    for label, content, fragment in cases:
        path = temp / "mutant.nmde"
        path.write_bytes(content)
        expect_refusal(label, lambda: evidence.read_digest(path), fragment)
    framed = [
        ("changed footer count", dict(records=[generation, SETS], count=3),
         "footer count or digest mismatch"),
        ("record that is not canonical JSON",
         dict(records=[zlib.compress(b'{"cpu": "z80"}', 6), SETS]), "not canonical JSON"),
        ("record that is not zlib", dict(records=[b"not zlib", SETS]), "invalid NMDE record"),
        ("generation after the sets", dict(records=[SETS, generation]),
         "record after the sets record"),
        ("no sets record", dict(records=[generation]), "no sets record"),
        ("delta past the RAM", dict(records=[dict(generation, delta=[[0x1FFF, "0000"]]), SETS]),
         "delta run exceeds the RAM"),
        ("unknown CPU", dict(records=[dict(generation, cpu="sh2"), SETS]),
         "invalid NMDE generation"),
        ("generation that executed nothing", dict(records=[dict(generation, executed=[]), SETS]),
         "invalid NMDE generation"),
        ("unsorted set", dict(records=[generation, dict(SETS, traced_rom=[0x204, 0x200])]),
         "invalid NMDE sets"),
        ("incomplete digest", dict(records=[generation, SETS], status=1), "digest is incomplete"),
        ("local path in metadata",
         dict(records=[generation, SETS], metadata=dict(METADATA, options={"rom_path": "x"})),
         "local paths are forbidden"),
    ]
    for label, arguments, fragment in framed:
        path = raw_digest(temp / "mutant.nmde", **arguments)
        expect_refusal(label, lambda: evidence.read_digest(path), fragment)
    return len(cases) + len(framed)


def digest_byte_cap(temp):
    path = temp / "capped.nmde"
    writer = evidence.DigestWriter(path, METADATA, 700)
    try:
        for step in range(64):
            image = bytearray(codemap.Z80_RAM_SIZE)
            image[step * 8:step * 8 + 8] = hashlib.sha256(bytes([step])).digest()[:8]
            writer.generation("z80", bytes(image), {0: 1}, set())
        writer.finish(set(), set(), set())
    except nmdh_trace.CaptureLimitError:
        require(not path.exists(), "a digest over its byte cap was left at its path")
        return
    raise AssertionError("a digest over its byte cap was written")


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-evidence-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path, _ = census_fixture.build(temp / "census")
        capture = census_fixture.capture(rom_path, scenario_path, temp / "census")
        rom = rom_path.read_bytes()
        generations = ingest_matches_build(rom, capture)
        size = digest_round_trip(temp)
        refused = digest_mutations(temp)
        digest_byte_cap(temp)
    print("evidence_check: ok (ingest: %d RAM generations, as the code map counts them; "
          "digest round trip in %d bytes, %d damaged digests refused, byte cap held)"
          % (generations, size, refused))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("evidence_check: " + str(exc))
