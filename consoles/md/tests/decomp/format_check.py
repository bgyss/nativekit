#!/usr/bin/env python3
"""Check NMDH v1 framing, identity validation and incomplete captures."""
import importlib.util
import pathlib
import struct
import tempfile

root = pathlib.Path(__file__).resolve().parents[4]
module_path = root / "consoles/md/tools/decomp/trace.py"
if not module_path.is_file():
    raise SystemExit("format_check: NMDH writer/reader are not implemented")
spec = importlib.util.spec_from_file_location("nmdh_trace", module_path)
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)

HEADER = struct.Struct("<4sHHI")
RECORD_LENGTH = struct.Struct("<I")
FOOTER = struct.Struct("<4sQB3s32s")

metadata = {
    "abi_version": 1,
    "capabilities": 3,
    "identities": {
        "rom_sha256": "a" * 64,
        "scenario_sha256": "b" * 64,
        "core_sha256": "c" * 64,
        "patch_sha256": "d" * 64,
        "abi_sha256": "e" * 64,
        "runner_sha256": "f" * 64,
    },
    "options": {"model": "md1va3", "region": "U"},
}
event = {
    "struct_size": 304,
    "kind": 1,
    "cpu": 1,
    "flags": 0,
    "sequence": 1,
    "master_clock": 500,
    "epoch": 2,
    "pc": 0x200,
    "next_pc": 0x204,
    "payload": {"fetched_bytes": [0x4E, 0x71], "register": 0x12345678},
}


def expect_error(call, label):
    try:
        call()
    except (OSError, ValueError, trace.NMDHError):
        return
    raise AssertionError(label + " was accepted")


with tempfile.TemporaryDirectory() as temporary:
    directory = pathlib.Path(temporary)
    capture = directory / "roundtrip.nmdh"
    writer = trace.NMDHWriter(capture, metadata, max_bytes=1024 * 1024)
    writer.write_event(event)
    writer.finish(0)
    records = list(trace.read_nmdh(capture))
    assert records == [event]
    assert trace.validate_identity(metadata, metadata) == metadata
    private_path_metadata = {**metadata, "rom_path": "/example/private-title.bin"}
    expect_error(lambda: trace.NMDHWriter(directory / "private-path.nmdh",
                                          private_path_metadata, max_bytes=1024 * 1024),
                 "ROM path in metadata")

    z80_metadata = {**metadata, "capabilities": (1 << 2) | (1 << 3)}
    z80_event = {**event, "cpu": 2, "payload": {"fetched_bytes": [0x3E, 0x17]}}
    z80_capture = directory / "z80-roundtrip.nmdh"
    z80_writer = trace.NMDHWriter(z80_capture, z80_metadata, max_bytes=1024 * 1024)
    z80_writer.write_event(z80_event)
    z80_writer.finish(0)
    assert list(trace.read_nmdh(z80_capture)) == [z80_event]
    expected = dict(metadata)
    expected["capabilities"] = 2
    expect_error(lambda: trace.validate_identity(metadata, expected), "wrong capability")
    expected = {"identities": {"rom_sha256": "0" * 64}}
    expect_error(lambda: trace.validate_identity(metadata, expected), "stale ROM identity")

    raw = capture.read_bytes()
    magic, version, flags, metadata_size = HEADER.unpack_from(raw, 0)
    record_at = HEADER.size + metadata_size
    record_size = RECORD_LENGTH.unpack_from(raw, record_at)[0]
    footer_at = record_at + RECORD_LENGTH.size + record_size
    assert magic == b"NMDH" and version == 1 and flags == 0

    bad = bytearray(raw)
    bad[0] = ord("X")
    bad_path = directory / "bad-magic.nmdh"
    bad_path.write_bytes(bad)
    expect_error(lambda: list(trace.read_nmdh(bad_path)), "bad magic")

    bad = bytearray(raw)
    struct.pack_into("<H", bad, 4, 2)
    bad_path = directory / "bad-version.nmdh"
    bad_path.write_bytes(bad)
    expect_error(lambda: list(trace.read_nmdh(bad_path)), "unsupported version")

    bad = bytearray(raw)
    struct.pack_into("<I", bad, record_at, 4097)
    bad_path = directory / "oversized-record.nmdh"
    bad_path.write_bytes(bad)
    expect_error(lambda: list(trace.read_nmdh(bad_path)), "oversized record")

    bad = bytearray(raw)
    bad[footer_at + FOOTER.size - 1] ^= 1
    bad_path = directory / "bad-digest.nmdh"
    bad_path.write_bytes(bad)
    expect_error(lambda: list(trace.read_nmdh(bad_path)), "bad digest")

    bad = bytearray(raw)
    struct.pack_into("<Q", bad, footer_at + 4, 2)
    bad_path = directory / "bad-count.nmdh"
    bad_path.write_bytes(bad)
    expect_error(lambda: list(trace.read_nmdh(bad_path)), "bad count")

    for name, content in (("truncated", raw[:-9]), ("trailing", raw + b"x")):
        bad_path = directory / (name + ".nmdh")
        bad_path.write_bytes(content)
        expect_error(lambda p=bad_path: list(trace.read_nmdh(p)), name + " stream")

    limited_path = directory / "limited.nmdh"
    limited = trace.NMDHWriter(limited_path, metadata, max_bytes=700)
    expect_error(lambda: limited.write_event(event), "storage limit")
    assert limited.partial_path.exists()
    expect_error(lambda: list(trace.read_nmdh(limited.partial_path)), "incomplete capture")

    aborted_path = directory / "timeout.nmdh"
    aborted = trace.NMDHWriter(aborted_path, metadata, max_bytes=1024 * 1024)
    aborted.write_event(event)
    aborted.abort("timeout")
    expect_error(lambda: list(trace.read_nmdh(aborted.partial_path)), "timeout capture")

    # a writer that stores nothing still hashes the records a stored one writes
    stored = trace.NMDHWriter(directory / "stored.nmdh", metadata, max_bytes=1024 * 1024)
    unstored = trace.NMDHWriter(directory / "unstored.nmdh", metadata, max_bytes=64, store=False)
    for sequence in (1, 2, 3):
        for target in (stored, unstored):
            target.write_event({**event, "sequence": sequence})
    stored_path = stored.finish(0)
    assert unstored.finish(0) is None and unstored.count == 3
    assert not list(directory.glob("unstored.nmdh*"))
    body = stored_path.read_bytes()
    header_size = trace.HEADER.size + len(trace.canonical_json(metadata))
    import hashlib
    assert stored.records_sha256 == unstored.records_sha256 == hashlib.sha256(
        body[header_size:-trace.FOOTER.size]).hexdigest()
    expect_error(lambda: unstored.write_event({**event, "sequence": 9}), "write after finish")
print("decomp format checks: ok")
