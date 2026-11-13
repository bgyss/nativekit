"""NMDE v1: the evidence digest a capture's events reduce to, and its
fail-closed reader.

A digest holds what `codemap.Ingest` keeps of a trace: for each CPU the RAM
program generations in the order they closed, then the three ROM sets. It is
framed like NMDH v1 (the same header, metadata rules and footer), with each
record a zlib stream of canonical JSON:

    generation  {"cpu", "delta", "executed", "edges"}
    sets        {"traced_rom", "rom_edges", "reads"}     the last record, once

A generation's RAM image is stored as `delta`: the runs of bytes, as
[offset, hex], that differ from the previous generation record of the same
CPU, or from all zeros for the first. `executed` is [offset, length] pairs
and `edges` [from, to] pairs; every list is sorted. This module knows the
framing and the record shapes, not what they mean.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import zlib

import trace as nmdh_trace

MAGIC = b"NMDE"
VERSION = 1
FOOTER_MAGIC = b"NEEN"
MAX_RECORD = 16 << 20        # compressed bytes; the sets record of a large image is the longest
RAM_SIZES = {"m68k": 0x10000, "z80": 0x2000}
_RUN = re.compile(rb"[^\x00]+")
_HEX = re.compile(r"(?:[0-9a-f]{2})+\Z")


class DigestError(ValueError):
    """Invalid NMDE v1 input."""


def _delta(previous, snapshot):
    """The runs of `snapshot` that differ from `previous`, as [offset, hex]."""
    if previous == snapshot:
        return []
    size = len(snapshot)
    changed = (int.from_bytes(previous, "big") ^ int.from_bytes(snapshot, "big")).to_bytes(size, "big")
    return [[run.start(), snapshot[run.start():run.end()].hex()] for run in _RUN.finditer(changed)]


def _pairs(value, low=0):
    """Whether `value` is a strictly ascending list of [int, int] pairs."""
    return (isinstance(value, list) and
            all(isinstance(pair, list) and len(pair) == 2 and
                all(type(item) is int and item >= 0 for item in pair) for pair in value) and
            all(first < second for first, second in zip(value, value[1:])) and
            all(pair[1] >= low for pair in value))


class DigestWriter:
    """Write a bounded digest; the file appears at `path` only when finished."""

    def __init__(self, path, metadata, max_bytes):
        self.path = Path(path)
        self.partial_path = Path(str(self.path) + ".partial")
        if type(max_bytes) is not int or max_bytes < nmdh_trace.FOOTER.size:
            raise nmdh_trace.CaptureLimitError("max_bytes is too small")
        if self.path.exists() or self.partial_path.exists():
            raise FileExistsError("NMDE output already exists")
        nmdh_trace._validate_metadata(metadata)
        metadata_bytes = nmdh_trace.canonical_json(metadata)
        if len(metadata_bytes) > nmdh_trace.MAX_METADATA:
            raise DigestError("NMDE metadata exceeds its limit")
        header = nmdh_trace.HEADER.pack(MAGIC, VERSION, 0, len(metadata_bytes)) + metadata_bytes
        if len(header) + nmdh_trace.FOOTER.size > max_bytes:
            raise nmdh_trace.CaptureLimitError("max_bytes cannot hold the NMDE header and footer")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._stream = self.partial_path.open("xb")
        self._digest = hashlib.sha256(header)
        self._stream.write(header)
        self._bytes_written = len(header)
        self._max_bytes = max_bytes
        self._count = 0
        self._closed = False
        self._previous = {cpu: bytes(size) for cpu, size in RAM_SIZES.items()}

    @property
    def count(self):
        return self._count

    @property
    def bytes_written(self):
        return self._bytes_written

    def _record(self, value):
        if self._closed:
            raise DigestError("NMDE writer is closed")
        payload = zlib.compress(nmdh_trace.canonical_json(value), 6)
        record = nmdh_trace.RECORD_LENGTH.pack(len(payload)) + payload
        if len(payload) > MAX_RECORD or \
                self._bytes_written + len(record) + nmdh_trace.FOOTER.size > self._max_bytes:
            self.abort("storage_limit")
            raise nmdh_trace.CaptureLimitError("NMDE digest exceeded max_bytes")
        self._stream.write(record)
        self._digest.update(record)
        self._bytes_written += len(record)
        self._count += 1

    def generation(self, cpu, snapshot, executed, edges):
        if cpu not in RAM_SIZES or len(snapshot) != RAM_SIZES[cpu] or not executed:
            raise DigestError("invalid NMDE generation")
        self._record({"cpu": cpu, "delta": _delta(self._previous[cpu], snapshot),
                      "executed": sorted([offset, length] for offset, length in executed.items()),
                      "edges": sorted([source, target] for source, target in edges)})
        self._previous[cpu] = snapshot

    def _footer(self, status):
        self._stream.write(nmdh_trace.FOOTER.pack(
            FOOTER_MAGIC, self._count, nmdh_trace._status_code(status), b"\0" * 3,
            self._digest.digest()))
        self._stream.flush()
        os.fsync(self._stream.fileno())
        self._stream.close()
        self._closed = True

    def finish(self, traced_rom, rom_edges, reads):
        """Write the sets record and the footer; the digest is then at `path`."""
        self._record({"traced_rom": sorted(traced_rom),
                      "rom_edges": sorted([source, target] for source, target in rom_edges),
                      "reads": sorted([address, width] for address, width in reads)})
        self._footer("complete")
        self._bytes_written += nmdh_trace.FOOTER.size
        os.replace(self.partial_path, self.path)
        return self.path

    def abort(self, reason):
        """Close the partial file with that status; nothing appears at `path`."""
        if not self._closed:
            if self._bytes_written + nmdh_trace.FOOTER.size <= self._max_bytes:
                self._footer(reason)
            else:
                self._stream.close()
                self._closed = True
        return self.partial_path


def _exact(stream, size, label):
    data = stream.read(size)
    if len(data) != size:
        raise DigestError("truncated NMDE " + label)
    return data


def _records(path):
    """Yield each record's JSON value, in file order, without validating the footer."""
    with Path(path).open("rb") as stream:
        _, _, _, metadata_size = nmdh_trace.HEADER.unpack(_exact(stream, nmdh_trace.HEADER.size, "header"))
        stream.seek(metadata_size, os.SEEK_CUR)
        while True:
            marker = _exact(stream, 4, "record or footer")
            if marker == FOOTER_MAGIC:
                return
            payload = _exact(stream, nmdh_trace.RECORD_LENGTH.unpack(marker)[0], "record")
            try:
                text = zlib.decompress(payload)
                value = json.loads(text.decode("utf-8"))
            except (zlib.error, UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise DigestError("invalid NMDE record") from exc
            if nmdh_trace.canonical_json(value) != text:
                raise DigestError("NMDE record is not canonical JSON")
            yield value


def _frame(path):
    """Check the header, the framing and the footer; return the metadata."""
    digest = hashlib.sha256()
    count = 0
    with Path(path).open("rb") as stream:
        fixed = _exact(stream, nmdh_trace.HEADER.size, "header")
        magic, version, flags, metadata_size = nmdh_trace.HEADER.unpack(fixed)
        if magic != MAGIC or version != VERSION or flags != 0:
            raise DigestError("invalid or unsupported NMDE header")
        if metadata_size > nmdh_trace.MAX_METADATA:
            raise DigestError("NMDE metadata exceeds its limit")
        metadata_bytes = _exact(stream, metadata_size, "metadata")
        try:
            metadata = json.loads(metadata_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise DigestError("invalid NMDE metadata JSON") from exc
        if nmdh_trace.canonical_json(metadata) != metadata_bytes:
            raise DigestError("NMDE metadata is not canonical JSON")
        nmdh_trace._validate_metadata(metadata)
        digest.update(fixed + metadata_bytes)
        while True:
            marker = stream.read(4)
            if len(marker) != 4:
                raise DigestError("missing NMDE footer")
            if marker == FOOTER_MAGIC:
                footer = marker + _exact(stream, nmdh_trace.FOOTER.size - 4, "footer")
                _, record_count, status, reserved, saved = nmdh_trace.FOOTER.unpack(footer)
                if reserved != b"\0" * 3 or status not in nmdh_trace.STATUS_CODES.values():
                    raise DigestError("invalid NMDE footer")
                if record_count != count or saved != digest.digest():
                    raise DigestError("NMDE footer count or digest mismatch")
                if stream.read(1):
                    raise DigestError("trailing bytes after NMDE footer")
                if status != nmdh_trace.STATUS_CODES["complete"]:
                    raise DigestError("NMDE digest is incomplete: %d" % status)
                return metadata
            length = nmdh_trace.RECORD_LENGTH.unpack(marker)[0]
            if length == 0 or length > MAX_RECORD:
                raise DigestError("invalid NMDE record length")
            digest.update(marker + _exact(stream, length, "record"))
            count += 1


def _check_generation(value):
    cpu = value.get("cpu")
    if set(value) != {"cpu", "delta", "executed", "edges"} or cpu not in RAM_SIZES:
        raise DigestError("invalid NMDE generation")
    size = RAM_SIZES[cpu]
    delta, executed = value["delta"], value["executed"]
    if not isinstance(delta, list) or not all(
            isinstance(run, list) and len(run) == 2 and type(run[0]) is int and run[0] >= 0 and
            isinstance(run[1], str) and _HEX.match(run[1]) for run in delta):
        raise DigestError("invalid NMDE generation")
    end = 0
    for offset, data in delta:
        if offset < end:
            raise DigestError("invalid NMDE generation")
        end = offset + len(data) // 2
        if end > size:
            raise DigestError("NMDE delta run exceeds the RAM")
    if not executed or not _pairs(executed, 1) or executed[-1][0] >= size or \
            not _pairs(value["edges"]):
        raise DigestError("invalid NMDE generation")


def _check_sets(value):
    traced = value.get("traced_rom")
    if set(value) != {"traced_rom", "rom_edges", "reads"} or not isinstance(traced, list) or \
            not all(type(item) is int and item >= 0 for item in traced) or \
            not all(first < second for first, second in zip(traced, traced[1:])) or \
            not _pairs(value["rom_edges"]) or not _pairs(value["reads"], 1):
        raise DigestError("invalid NMDE sets")


class Digest:
    """A validated digest: its metadata, its three sets, and its generations."""

    def __init__(self, path, metadata, sets):
        self.path = Path(path)
        self.metadata = metadata
        self.traced_rom = set(sets["traced_rom"])
        self.rom_edges = {tuple(pair) for pair in sets["rom_edges"]}
        self.reads = {tuple(pair) for pair in sets["reads"]}

    def generations(self):
        """Yield (cpu, snapshot, executed, edges) in the order they closed."""
        images = {cpu: bytearray(size) for cpu, size in RAM_SIZES.items()}
        for value in _records(self.path):
            if "cpu" not in value:
                return
            image = images[value["cpu"]]
            for offset, data in value["delta"]:
                image[offset:offset + len(data) // 2] = bytes.fromhex(data)
            yield (value["cpu"], bytes(image), {offset: length for offset, length in value["executed"]},
                   {tuple(pair) for pair in value["edges"]})


def read_digest(path):
    """Validate the whole file, then return its Digest."""
    try:
        metadata = _frame(path)
    except nmdh_trace.NMDHError as exc:
        raise DigestError(str(exc)) from exc
    sets = None
    for value in _records(path):
        if sets is not None:
            raise DigestError("NMDE record after the sets record")
        if not isinstance(value, dict):
            raise DigestError("invalid NMDE record")
        if "cpu" in value:
            _check_generation(value)
        else:
            _check_sets(value)
            sets = value
    if sets is None:
        raise DigestError("NMDE digest has no sets record")
    return Digest(path, metadata, sets)
