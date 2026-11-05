"""Streaming NMDH v1 capture writer and fail-closed reader."""
import hashlib
import json
import os
from pathlib import Path
import struct

MAGIC = b"NMDH"
VERSION = 1
FOOTER_MAGIC = b"NHEN"
HEADER = struct.Struct("<4sHHI")
RECORD_LENGTH = struct.Struct("<I")
FOOTER = struct.Struct("<4sQB3s32s")
MAX_METADATA = 16_384
MAX_RECORD = 4_096
EVENT_STRUCT_SIZE = 304
KNOWN_CAPABILITIES = (1 << 12) - 1
CAPABILITY_FOR_EVENT = {
    1: 0,
    2: 1 << 4,
    3: 1 << 7,
    4: 1 << 5,
    5: 1 << 5,
    6: 1 << 5,
    7: 1 << 10,
    8: 1 << 11,
}
STATUS_CODES = {
    "complete": 0,
    "timeout": 1,
    "storage_limit": 2,
    "core_error": 3,
    "cancelled": 4,
}
IDENTITY_FIELDS = (
    "rom_sha256", "scenario_sha256", "core_sha256", "patch_sha256",
    "abi_sha256", "runner_sha256",
)


class NMDHError(ValueError):
    """Invalid NMDH v1 input or capture operation."""


class IncompleteCaptureError(NMDHError):
    """The NMDH file is validly framed but is not a complete capture."""


class CaptureLimitError(NMDHError):
    """The capture cannot fit within the caller's declared byte limit."""


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("utf-8")


def _uint(value, bits, label):
    if type(value) is not int or value < 0 or value >= (1 << bits):
        raise NMDHError("invalid " + label)


def _validate_metadata(metadata):
    if (not isinstance(metadata, dict) or type(metadata.get("abi_version")) is not int or
            metadata.get("abi_version") != VERSION):
        raise NMDHError("invalid NMDH ABI metadata")
    capabilities = metadata.get("capabilities")
    _uint(capabilities, 64, "capability mask")
    if capabilities == 0 or capabilities & ~KNOWN_CAPABILITIES:
        raise NMDHError("invalid NMDH capability mask")
    identities = metadata.get("identities")
    if not isinstance(identities, dict):
        raise NMDHError("missing NMDH identities")
    for field in IDENTITY_FIELDS:
        digest = identities.get(field)
        if (not isinstance(digest, str) or len(digest) != 64 or
                any(char not in "0123456789abcdef" for char in digest)):
            raise NMDHError("invalid identity digest: " + field)
    options = metadata.get("options")
    if not isinstance(options, dict):
        raise NMDHError("missing NMDH machine options")
    for key, value in _walk(metadata):
        lowered = key.casefold()
        if "path" in lowered or "filename" in lowered:
            raise NMDHError("local paths are forbidden in NMDH metadata")
        if isinstance(value, str) and (value.startswith(("/", "~/")) or "\\Users\\" in value or
                                       (len(value) >= 3 and value[1:3] == ":\\")):
            raise NMDHError("local paths are forbidden in NMDH metadata")


def _walk(value, key=""):
    if isinstance(value, dict):
        for item_key, item_value in value.items():
            yield from _walk(item_value, str(item_key))
    elif isinstance(value, list):
        for item in value:
            yield from _walk(item, key)
    else:
        yield key, value


def _validate_event(event, capabilities, previous_sequence):
    if not isinstance(event, dict):
        raise NMDHError("event must be an object")
    required = ("struct_size", "kind", "cpu", "flags", "sequence",
                "master_clock", "epoch", "pc", "next_pc", "payload")
    if any(field not in event for field in required):
        raise NMDHError("event is missing a required field")
    _uint(event["struct_size"], 32, "event structure size")
    if event["struct_size"] != EVENT_STRUCT_SIZE:
        raise NMDHError("unsupported event structure size")
    kind = event["kind"]
    if type(kind) is not int or kind not in CAPABILITY_FOR_EVENT:
        raise NMDHError("invalid event kind")
    cpu = event["cpu"]
    if type(cpu) is not int or cpu not in (1, 2):
        raise NMDHError("invalid CPU ID")
    for field, bits in (("flags", 32), ("sequence", 64), ("master_clock", 64),
                        ("epoch", 64), ("pc", 32), ("next_pc", 32)):
        _uint(event[field], bits, field)
    if event["sequence"] != previous_sequence + 1:
        raise NMDHError("event sequence is not contiguous")
    required_capability = (((1 << 0) | (1 << 1)) if cpu == 1 else
                           ((1 << 2) | (1 << 3))) if kind == 1 else CAPABILITY_FOR_EVENT[kind]
    if (capabilities & required_capability) != required_capability:
        raise NMDHError("event exceeds advertised capabilities")
    if not isinstance(event["payload"], dict):
        raise NMDHError("event payload must be an object")
    return kind


def _status_code(status):
    if isinstance(status, str):
        if status not in STATUS_CODES:
            raise NMDHError("unknown capture status")
        return STATUS_CODES[status]
    if type(status) is not int or status not in STATUS_CODES.values():
        raise NMDHError("unknown capture status")
    return status


class NMDHWriter:
    """Write bounded, canonical NMDH records without buffering a capture."""

    def __init__(self, path, metadata, max_bytes):
        self.path = Path(path)
        self.partial_path = Path(str(self.path) + ".partial")
        if type(max_bytes) is not int or max_bytes < FOOTER.size:
            raise CaptureLimitError("max_bytes is too small")
        if self.path.exists() or self.partial_path.exists():
            raise FileExistsError("NMDH output already exists")
        _validate_metadata(metadata)
        metadata_bytes = canonical_json(metadata)
        if len(metadata_bytes) > MAX_METADATA:
            raise NMDHError("NMDH metadata exceeds its limit")
        header = HEADER.pack(MAGIC, VERSION, 0, len(metadata_bytes)) + metadata_bytes
        if len(header) + FOOTER.size > max_bytes:
            raise CaptureLimitError("max_bytes cannot hold the NMDH header and footer")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._stream = self.partial_path.open("xb")
        self._digest = hashlib.sha256()
        self._digest.update(header)
        self._stream.write(header)
        self._bytes_written = len(header)
        self._max_bytes = max_bytes
        self._metadata = metadata
        self._count = 0
        self._closed = False

    @property
    def count(self):
        return self._count

    def write_event(self, event):
        if self._closed:
            raise NMDHError("NMDH writer is closed")
        _validate_event(event, self._metadata["capabilities"], self._count)
        payload = canonical_json(event)
        if not payload or len(payload) > MAX_RECORD:
            self.abort("storage_limit")
            raise CaptureLimitError("NMDH event exceeds the record limit")
        record = RECORD_LENGTH.pack(len(payload)) + payload
        if self._bytes_written + len(record) + FOOTER.size > self._max_bytes:
            self.abort("storage_limit")
            raise CaptureLimitError("NMDH capture exceeded max_bytes")
        self._stream.write(record)
        self._digest.update(record)
        self._bytes_written += len(record)
        self._count += 1

    def _write_footer(self, status):
        code = _status_code(status)
        footer = FOOTER.pack(FOOTER_MAGIC, self._count, code, b"\0" * 3,
                             self._digest.digest())
        self._stream.write(footer)
        self._stream.flush()
        os.fsync(self._stream.fileno())
        self._stream.close()
        self._closed = True

    def finish(self, status=0):
        code = _status_code(status)
        if code != STATUS_CODES["complete"]:
            self.abort(code)
            return self.partial_path
        if self._count == 0:
            self.abort("core_error")
            raise NMDHError("empty NMDH capture is incomplete")
        if self._bytes_written + FOOTER.size > self._max_bytes:
            self.abort("storage_limit")
            raise CaptureLimitError("NMDH footer exceeded max_bytes")
        self._write_footer(code)
        os.replace(self.partial_path, self.path)
        return self.path

    def abort(self, reason):
        if not self._closed:
            if self._bytes_written + FOOTER.size <= self._max_bytes:
                self._write_footer(reason)
            else:
                self._stream.close()
                self._closed = True
        return self.partial_path


def _read_exact(stream, size, label):
    data = stream.read(size)
    if len(data) != size:
        raise NMDHError("truncated " + label)
    return data


def _scan(path):
    digest = hashlib.sha256()
    count = 0
    capabilities = None
    with Path(path).open("rb") as stream:
        fixed = _read_exact(stream, HEADER.size, "NMDH header")
        magic, version, flags, metadata_size = HEADER.unpack(fixed)
        if magic != MAGIC or version != VERSION or flags != 0:
            raise NMDHError("invalid or unsupported NMDH header")
        if metadata_size > MAX_METADATA:
            raise NMDHError("NMDH metadata exceeds its limit")
        metadata_bytes = _read_exact(stream, metadata_size, "NMDH metadata")
        try:
            metadata = json.loads(metadata_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise NMDHError("invalid NMDH metadata JSON") from exc
        if canonical_json(metadata) != metadata_bytes:
            raise NMDHError("NMDH metadata is not canonical JSON")
        _validate_metadata(metadata)
        capabilities = metadata["capabilities"]
        digest.update(fixed + metadata_bytes)
        previous_sequence = 0
        while True:
            marker = stream.read(4)
            if len(marker) != 4:
                raise NMDHError("missing NMDH footer")
            if marker == FOOTER_MAGIC:
                footer = marker + _read_exact(stream, FOOTER.size - 4, "NMDH footer")
                magic, record_count, status, reserved, saved_digest = FOOTER.unpack(footer)
                if magic != FOOTER_MAGIC or reserved != b"\0" * 3:
                    raise NMDHError("invalid NMDH footer")
                if status not in STATUS_CODES.values():
                    raise NMDHError("invalid NMDH completion status")
                if record_count != count or saved_digest != digest.digest():
                    raise NMDHError("NMDH footer count or digest mismatch")
                if stream.read(1):
                    raise NMDHError("trailing bytes after NMDH footer")
                if count == 0:
                    raise NMDHError("empty NMDH capture")
                if status != STATUS_CODES["complete"]:
                    raise IncompleteCaptureError("NMDH capture is incomplete: " +
                                                 str(status))
                return metadata, count
            length = RECORD_LENGTH.unpack(marker)[0]
            if length == 0 or length > MAX_RECORD:
                raise NMDHError("invalid NMDH record length")
            record_bytes = _read_exact(stream, length, "NMDH record")
            try:
                event = json.loads(record_bytes.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise NMDHError("invalid NMDH event JSON") from exc
            if canonical_json(event) != record_bytes:
                raise NMDHError("NMDH event is not canonical JSON")
            _validate_event(event, capabilities, previous_sequence)
            previous_sequence = event["sequence"]
            count += 1
            digest.update(marker + record_bytes)


def read_nmdh(path):
    """Validate the full capture first, then yield records in a second pass."""
    _scan(path)
    with Path(path).open("rb") as stream:
        _, _, _, metadata_size = HEADER.unpack(_read_exact(stream, HEADER.size, "header"))
        stream.seek(metadata_size, os.SEEK_CUR)
        while True:
            marker = _read_exact(stream, 4, "record or footer")
            if marker == FOOTER_MAGIC:
                return
            length = RECORD_LENGTH.unpack(marker)[0]
            yield json.loads(_read_exact(stream, length, "NMDH record").decode("utf-8"))


def validate_identity(metadata, expected):
    """Require a nested expected-metadata subset to match exactly."""
    _validate_metadata(metadata)

    def compare(actual, wanted, prefix):
        if isinstance(wanted, dict):
            if not isinstance(actual, dict):
                raise NMDHError("identity mismatch at " + prefix)
            for key, value in wanted.items():
                if key not in actual:
                    raise NMDHError("identity field missing: " + prefix + "." + key)
                compare(actual[key], value, prefix + "." + key)
        elif actual != wanted:
            raise NMDHError("identity mismatch at " + prefix)

    compare(metadata, expected, "metadata")
    return metadata
