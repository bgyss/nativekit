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
import identity_check  # noqa: E402
import intake  # noqa: E402
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


def code_map(rom_path, out, captures):
    """Run the intake; return codemap.json's bytes after an identical rebuild."""
    _, _, rebuilt = intake.run_intake(rom_path, out, captures, "evidence-control")
    require(rebuilt["identical"], "the skeleton did not rebuild the image")
    return (out / "codemap.json").read_bytes()


def stream_and_digest_agree(temp, name, rom_path, capture):
    """An intake over a capture's digest gives the code map of one over its events."""
    light = temp / (name + "-digest")
    require(intake.main(["digest", "--rom", str(rom_path), "--capture", str(capture),
                         "--out", str(light)]) == 0, "intake.py digest failed")
    manifest = json.loads((light / "manifest.json").read_text())
    source = json.loads((capture / "manifest.json").read_text())
    require(manifest["evidence"] == "light" and manifest["outputs"] == ["capture.nmde"] and
            manifest["nmde_bytes"] == (light / "capture.nmde").stat().st_size and
            manifest["events"] == source["events"] and
            len(manifest["digest_of"]["nmdh_footer_sha256"]) == 64 and
            not (light / "capture.nmdh").exists(),
            "the digest directory's manifest does not describe it: %r" % manifest)
    full = code_map(rom_path, temp / (name + "-from-stream"), [capture])
    reduced = code_map(rom_path, temp / (name + "-from-digest"), [light])
    require(full == reduced, name + ": the digest's code map differs from the stream's")
    return light, full


def planted_digest_errors(temp, rom_path, capture, light, full, other_light):
    rom = rom_path.read_bytes()
    digest = evidence.read_digest(light / "capture.nmde")

    def rewrite(directory, traced_rom):
        directory.mkdir()
        (directory / "manifest.json").write_bytes((light / "manifest.json").read_bytes())
        writer = evidence.DigestWriter(directory / "capture.nmde", digest.metadata, 1 << 24)
        for generation in digest.generations():
            writer.generation(*generation)
        writer.finish(traced_rom, digest.rom_edges, digest.reads)
        return directory

    same = rewrite(temp / "rewritten", digest.traced_rom)
    require(code_map(rom_path, temp / "from-rewritten", [same]) == full,
            "a digest rewritten unchanged gives another code map")
    short = rewrite(temp / "short", digest.traced_rom - {max(digest.traced_rom)})
    require(code_map(rom_path, temp / "from-short", [short]) != full,
            "a digest missing one traced address gives the same code map")

    other_rom = temp / "other.bin"
    other_rom.write_bytes(rom[:-1] + bytes([rom[-1] ^ 1]))
    for label, action, fragment in (
            ("digest with another ROM", lambda: intake.run_intake(other_rom, temp / "x1", [light]),
             "capture was made from a different ROM"),
            ("digest of another ROM under this manifest",
             lambda: (
                 (same / "capture.nmde").write_bytes((other_light / "capture.nmde").read_bytes()),
                 intake.run_intake(rom_path, temp / "x2", [same])),
             "digest was made from a different ROM")):
        try:
            action()
        except codemap.CodeMapError as exc:
            require(fragment in str(exc), "%s refused for another reason: %s" % (label, exc))
        else:
            raise AssertionError("planted " + label + " passed the intake")
    (same / "capture.nmde").write_bytes((light / "capture.nmde").read_bytes()[:-1])
    try:
        intake.run_intake(rom_path, temp / "x3", [same])
    except codemap.CodeMapError as exc:
        require("NMDE" in str(exc), "a truncated digest was refused for another reason: %s" % exc)
    else:
        raise AssertionError("a truncated digest passed the intake")


def repeats(temp, rom_path, capture, light, full):
    """A run given as its stream and its digest counts as the run given twice."""
    twice = code_map(rom_path, temp / "stream-twice", [capture, capture])
    mixed = code_map(rom_path, temp / "stream-and-digest", [capture, light])
    require(twice == mixed, "a stream with its own digest differs from the stream twice")
    require(twice != full, "a repeated run does not count its RAM programs' occurrences twice")
    once, again = json.loads(full), json.loads(twice)
    for space in again["spaces"]:
        if space["kind"] == "ram":
            space["occurrences"] //= 2
    require(once == again, "a repeated run changes more than occurrences")


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
        light, full = stream_and_digest_agree(temp, "census", rom_path, capture)
        id_rom, id_scenario, *_ = identity_check.build(temp / "identity")
        id_capture = census_fixture.capture(id_rom, id_scenario, temp / "identity")
        id_light, _ = stream_and_digest_agree(temp, "identity", id_rom, id_capture)
        planted_digest_errors(temp, rom_path, capture, light, full, id_light)
        repeats(temp, rom_path, capture, light, full)
    print("evidence_check: ok (ingest: %d RAM generations, as the code map counts them; "
          "digest round trip in %d bytes, %d damaged digests refused, byte cap held; "
          "two controls' code maps equal from stream and digest; 4 planted digest errors "
          "caught; a repeated run counted twice)" % (generations, size, refused))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("evidence_check: " + str(exc))
