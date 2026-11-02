"""Versioned candidate binary trace envelope.

All integers are little endian. The 16-byte header is ``NMDT``, version (u16),
record size (u16), and record count (u64). Version 2 records are 20 bytes: absolute master clock
(u64), device, operation, width, reserved (u8 each), address and value (u32
each). Clocks are nondecreasing within each device; interleaved devices may
be observed out of clock order. The reserved byte must be zero. A 12-byte
footer (``NEND`` and the record count as u64) is mandatory; extra bytes fail.
"""
from dataclasses import dataclass
import struct
import tempfile

MAGIC = b"NMDT"
VERSION = 2
HEADER = struct.Struct("<4sHHQ")
RECORD = struct.Struct("<QBBBBII")
FOOTER = struct.Struct("<4sQ")
M68K, Z80, VDP, YM2612, PSG = range(1, 6)
FETCH, READ, WRITE, DMA, IRQ = range(1, 6)
DEVICES = {M68K, Z80, VDP, YM2612, PSG}
OPERATIONS = {FETCH, READ, WRITE, DMA, IRQ}
MAX_U64 = (1 << 64) - 1


@dataclass(frozen=True)
class Event:
    clock: int
    device: int
    operation: int
    width: int
    address: int
    value: int


class Epoch:
    """Convert a renormalized local counter into an absolute master clock."""

    def __init__(self):
        self.offset = 0

    def adjust(self, deduction):
        if not isinstance(deduction, int) or deduction < 0 or self.offset + deduction > MAX_U64:
            raise ValueError("invalid cycle adjustment")
        self.offset += deduction

    def clock(self, local):
        if not isinstance(local, int) or local < 0 or self.offset + local > MAX_U64:
            raise ValueError("invalid local clock")
        return self.offset + local


def _check(event, last):
    if (not isinstance(event, Event) or not isinstance(event.clock, int) or
            not 0 <= event.clock <= MAX_U64 or event.device not in DEVICES or
            event.operation not in OPERATIONS or event.width not in (1, 2, 4) or
            not isinstance(event.address, int) or not 0 <= event.address <= 0xFFFFFFFF or
            not isinstance(event.value, int) or not 0 <= event.value <= 0xFFFFFFFF):
        raise ValueError("invalid trace event")
    if event.clock < last.get(event.device, 0):
        raise ValueError("device clock moved backward")
    last[event.device] = event.clock


class Writer:
    """Append validated records, publishing the header count and footer on close."""

    def __init__(self, stream):
        self.stream = stream
        self.start = stream.tell()
        self.last = {}
        self.count = 0
        self.closed = False
        stream.write(HEADER.pack(MAGIC, VERSION, RECORD.size, 0))

    def emit(self, event):
        if self.closed:
            raise ValueError("trace writer is closed")
        _check(event, self.last)
        self.stream.write(RECORD.pack(event.clock, event.device, event.operation, event.width, 0,
                                      event.address, event.value))
        self.count += 1

    def close(self):
        if self.closed:
            raise ValueError("trace writer is closed")
        self.stream.write(FOOTER.pack(b"NEND", self.count))
        end = self.stream.tell()
        self.stream.seek(self.start)
        self.stream.write(HEADER.pack(MAGIC, VERSION, RECORD.size, self.count))
        self.stream.seek(end)
        self.closed = True


def write(stream, events):
    writer = Writer(stream)
    for event in events:
        writer.emit(event)
    writer.close()


def _read_up_to(stream, size):
    data = bytearray()
    while len(data) < size:
        chunk = stream.read(size - len(data))
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


def read(stream):
    header = _read_up_to(stream, HEADER.size)
    if len(header) != HEADER.size:
        raise ValueError("invalid or unsupported trace header")
    magic, version, size, count = HEADER.unpack(header)
    if (magic, version, size) != (MAGIC, VERSION, RECORD.size):
        raise ValueError("invalid or unsupported trace header")
    last = {}
    # Validate the complete stream before exposing any events. Spooling also
    # preserves the validated bytes if the original file changes afterward.
    with tempfile.SpooledTemporaryFile(max_size=8 * 1024 * 1024) as checked:
        for _ in range(count):
            data = _read_up_to(stream, RECORD.size)
            if len(data) != RECORD.size:
                raise ValueError("truncated trace record")
            clock, device, operation, width, reserved, address, value = RECORD.unpack(data)
            if reserved:
                raise ValueError("unsupported trace flags")
            _check(Event(clock, device, operation, width, address, value), last)
            checked.write(data)
        footer = _read_up_to(stream, FOOTER.size)
        if len(footer) != FOOTER.size or FOOTER.unpack(footer) != (b"NEND", count):
            raise ValueError("missing or invalid trace footer")
        if _read_up_to(stream, 1):
            raise ValueError("trailing trace data")
        checked.seek(0)
        for _ in range(count):
            clock, device, operation, width, _, address, value = RECORD.unpack(checked.read(RECORD.size))
            yield Event(clock, device, operation, width, address, value)
