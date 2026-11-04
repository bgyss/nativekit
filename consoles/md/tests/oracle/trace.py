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
M68K_KIND_NAMES = ("rom", "work ram", "z80 ram", "io", "vdp", "sound", "open bus")
Z80_KIND_NAMES = ("z80 ram", "z80 ym2612", "z80 bank register", "z80 psg",
                  "z80 bank window", "z80 open bus")
M68K_KIND_COUNT = len(M68K_KIND_NAMES)
Z80_KIND_COUNT = len(Z80_KIND_NAMES)
CPU_STATS_HEADER = "nmd-cpu-stats 1"
CPU_STATS_MAX_GAP = 1024
CPU_STATS_MAX_POLL_READS = 64


@dataclass(frozen=True)
class Event:
    clock: int
    device: int
    operation: int
    width: int
    address: int
    value: int


@dataclass(frozen=True)
class CPUStatsRow:
    frame: int
    cpu: str
    counts: list
    work_clocks: int
    bus_clocks: int


def _m68k_kind(address):
    address &= 0xFFFFFF
    if address <= 0x3FFFFF:
        return 0
    if 0xA00000 <= address <= 0xA01FFF:
        return 2
    if address >= 0xE00000:
        return 1
    if (0xA04000 <= address <= 0xA04003 or
            (0xC00000 <= address <= 0xDFFFFF and (address & 0x1F) == 0x11)):
        return 5
    if 0xC00000 <= address <= 0xDFFFFF:
        return 4
    if (0xA06000 <= address <= 0xA06001 or 0xA10000 <= address <= 0xA1001F or
            0xA11000 <= address <= 0xA11201 or 0xA14000 <= address <= 0xA14101):
        return 3
    return 6


def _z80_kind(address, operation):
    address &= 0xFFFF
    # BlastEm traces OUT ($7F),A at the I/O port address; nmd normalizes that
    # operation to its PSG memory-map kind at $7F11.
    if address == 0x7F and operation == WRITE:
        return 3
    if address <= 0x1FFF:
        return 0
    if 0x4000 <= address <= 0x4003:
        return 1
    if address == 0x6000:
        return 2
    if 0x7F10 <= address <= 0x7F17:
        return 3
    if address >= 0x8000:
        return 4
    return 5


def _bus_clocks(event):
    if event.device == M68K:
        return 28 * ((event.width + 1) // 2)
    address = event.address & 0xFFFF
    if event.width == 1 and address == 0x7F and event.operation == WRITE:
        return 60                         # Z80 I/O cycle: four T-states.
    clocks = 0
    for offset in range(event.width):
        byte_address = (address + offset) & 0xFFFF
        clocks += 45                     # Z80 memory cycle: three T-states.
        if byte_address >= 0x8000:
            clocks += 45                 # serial 68000 bank window: three-T wait.
    return clocks


def _access_kinds(event, cpu):
    address = event.address
    if cpu == M68K:
        accesses = (address, (address + 2) & 0xFFFFFF) if event.width == 4 else (address,)
        return [_m68k_kind(access) for access in accesses]
    return [_z80_kind((address + offset) & 0xFFFF, event.operation)
            for offset in range(event.width)]


class CPUStats:
    """Accumulate CPU data-access gaps and bus clocks into VBlank rows.

    Trace timestamps are each CPU's cumulative master-clock counter after an
    access handler returns. A data event's nominal bus time is removed from
    the interval; the remaining interval is candidate-derived CPU work.
    Fetch and idle intervals accrue until the next data access. Poll reads
    and gaps over 1,024 clocks are omitted, like the shared SNES fitter.
    """

    def __init__(self, max_gap=CPU_STATS_MAX_GAP,
                 max_poll_reads=CPU_STATS_MAX_POLL_READS):
        if max_gap < 0 or max_poll_reads < 0:
            raise ValueError("invalid CPU statistics limits")
        self.max_gap = max_gap
        self.max_poll_reads = max_poll_reads
        self.current_frame = None
        self.last_clock = {M68K: 0, Z80: 0}
        self.gap = {M68K: 0, Z80: 0}
        self.pending = {M68K: [], Z80: []}
        self.pending_count = {M68K: 0, Z80: 0}
        self.data = {}

    def begin_frame(self, frame):
        if self.current_frame is not None or not isinstance(frame, int) or frame < 1:
            raise ValueError("invalid CPU statistics frame transition")
        self.current_frame = frame
        for cpu, name, count in ((M68K, "m68k", M68K_KIND_COUNT),
                                 (Z80, "z80", Z80_KIND_COUNT)):
            self.data.setdefault((frame, cpu), {
                "frame": frame, "cpu": name, "counts": [0] * (2 * count),
                "work_clocks": 0, "bus_clocks": 0,
            })

    def _commit(self, cpu, sample):
        frame, kinds, work, bus, is_write = sample
        row = self.data[(frame, cpu)]
        kind_count = M68K_KIND_COUNT if cpu == M68K else Z80_KIND_COUNT
        for kind in kinds:
            row["counts"][kind + (kind_count if is_write else 0)] += 1
        row["work_clocks"] += work
        row["bus_clocks"] += bus

    def _access(self, event, cpu):
        kinds = _access_kinds(event, cpu)
        bus = _bus_clocks(event)
        work = self.gap[cpu] - bus
        self.gap[cpu] = 0
        if work < 0 or work > self.max_gap:
            return (self.current_frame, kinds, 0, 0, event.operation == WRITE)
        return (self.current_frame, kinds, work, bus, event.operation == WRITE)

    def add(self, event):
        if self.current_frame is None:
            raise ValueError("CPU statistics event outside a frame")
        if event.device not in (M68K, Z80):
            return
        cpu = event.device
        if event.clock < self.last_clock[cpu]:
            raise ValueError("CPU statistics clock moved backward")
        self.gap[cpu] += event.clock - self.last_clock[cpu]
        self.last_clock[cpu] = event.clock
        if event.operation not in (READ, WRITE):
            return
        sample = self._access(event, cpu)
        if event.operation == READ:
            if self.pending_count[cpu] < self.max_poll_reads + 1:
                self.pending[cpu].append(sample)
            self.pending_count[cpu] += len(sample[1])
            return
        if self.pending_count[cpu] <= self.max_poll_reads:
            for pending in self.pending[cpu]:
                if pending[2] or pending[3]:
                    self._commit(cpu, pending)
        self.pending[cpu].clear()
        self.pending_count[cpu] = 0
        if sample[2] or sample[3]:
            self._commit(cpu, sample)

    def end_frame(self):
        if self.current_frame is None:
            raise ValueError("CPU statistics frame is not open")
        self.current_frame = None

    def rows(self):
        return [CPUStatsRow(**row) for _, row in sorted(
            self.data.items(), key=lambda item: (item[0][0], item[0][1]))]


def write_cpu_stats(stream, rows):
    stream.write(CPU_STATS_HEADER + "\n")
    for row in rows:
        counts = " ".join(str(value) for value in row.counts)
        stream.write("F %d %s %d %d %s\n" %
                     (row.frame, row.cpu, row.work_clocks, row.bus_clocks, counts))


def read_cpu_stats(stream):
    if stream.readline().rstrip("\r\n") != CPU_STATS_HEADER:
        raise ValueError("invalid or unsupported CPU statistics header")
    rows = []
    last = (0, "")
    for line in stream:
        fields = line.split()
        if not fields:
            continue
        if len(fields) < 6 or fields[0] != "F" or fields[2] not in ("m68k", "z80"):
            raise ValueError("invalid CPU statistics row")
        try:
            frame, work, bus = int(fields[1]), int(fields[3]), int(fields[4])
            counts = [int(value) for value in fields[5:]]
        except ValueError as exc:
            raise ValueError("invalid CPU statistics number") from exc
        expected = 2 * (M68K_KIND_COUNT if fields[2] == "m68k" else Z80_KIND_COUNT)
        if (frame < 1 or work < 0 or bus < 0 or len(counts) != expected or
                any(value < 0 for value in counts)):
            raise ValueError("invalid CPU statistics values")
        key = (frame, fields[2])
        if key <= last:
            raise ValueError("CPU statistics rows are not ordered")
        last = key
        rows.append(CPUStatsRow(frame, fields[2], counts, work, bus))
    if not rows or len(rows) % 2 or any(
            rows[i].cpu != "m68k" or rows[i + 1].cpu != "z80" or
            rows[i].frame != rows[i + 1].frame or rows[i].frame != i // 2 + 1
            for i in range(0, len(rows), 2)):
        raise ValueError("CPU statistics must contain an m68k and z80 row for each frame")
    return rows


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
