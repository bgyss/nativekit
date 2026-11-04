#!/usr/bin/env python3
"""Compare shared synthetic bus cases on BlastEm and the native C model."""
import argparse
import hashlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
from collections import Counter

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "oracle"))

import cases  # noqa: E402
import mdtest  # noqa: E402
from trace import M68K, Z80, VDP, DMA, YM2612, PSG, READ, WRITE, read as read_trace  # noqa: E402

AUDIO_PHASE_LIMIT = 8
AUDIO_MEAN_ERROR_LIMIT = 32
AUDIO_PEAK_ERROR_LIMIT = 600
MD_SOUND_OUTPUT_RATE = 44100.0
NMD_BUTTON_BITS = {
    "UP": 1 << 0, "DOWN": 1 << 1, "LEFT": 1 << 2, "RIGHT": 1 << 3,
    "B": 1 << 4, "C": 1 << 5, "A": 1 << 6, "START": 1 << 7,
    "X": 1 << 8, "Y": 1 << 9, "Z": 1 << 10, "MODE": 1 << 11,
}


def _case_input_script(case):
    script = {}
    for frame_number, (port1, port2) in enumerate(case.input_frames, start=1):
        ports = {}
        if port1:
            ports["1"] = list(port1)
        if port2:
            ports["2"] = list(port2)
        if ports:
            script[str(frame_number)] = ports
    return script


def _native_button_mask(names):
    return sum(NMD_BUTTON_BITS[name] for name in names)


def _target_addresses(case):
    addresses = set()
    for op in case.operations:
        address = op.addr & 0xFFFFFF
        addresses.add(address)
        if op.width == 4:
            addresses.add((address + 2) & 0xFFFFFF)
    if case.z80_fixture_id:
        addresses.update(range(0xA00000, 0xA02000))
    return addresses


def _native_events(result):
    bus_events, vdp_events, dma_events, z80_events = [], [], [], []
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) == 6 and fields[0] in ("VDP", "Z80"):
            device, clock, operation, width, address, value = fields
            event = (int(operation), int(width), int(address, 16), int(value, 16), int(clock))
            if device == "Z80":
                z80_events.append(event)
            else:
                (dma_events if event[0] == DMA else vdp_events).append(event)
            continue
        if len(fields) != 5:
            raise ValueError("testcart emitted an invalid bus trace line")
        clock, operation, width, addr, value = fields
        bus_events.append((int(operation), int(width), int(addr, 16), int(value, 16), int(clock)))
    return bus_events, vdp_events, dma_events, z80_events


def _candidate_events(trace_path, addresses):
    with trace_path.open("rb") as stream:
        trace = list(read_trace(stream))
    bus = [(event.operation, event.width, event.address & 0xFFFFFF, event.value, event.clock)
           for event in trace
           if event.device == M68K and event.operation in (READ, WRITE) and
           (event.address & 0xFFFFFF) in addresses]
    vdp = [(event.operation, event.width, event.address, event.value, event.clock)
           for event in trace
           if event.device == VDP and event.operation in (READ, WRITE) and event.address in (0, 4, 8)]
    dma = [(event.operation, event.width, event.address, event.value, event.clock)
           for event in trace if event.device == VDP and event.operation == DMA]
    z80 = [(event.operation, event.width, event.address, event.value, event.clock)
           for event in trace if event.device == Z80 and event.operation in (READ, WRITE)]
    return bus, vdp, dma, z80


def _candidate_psg_writes(trace_path):
    with trace_path.open("rb") as stream:
        trace = list(read_trace(stream))
    return [(event.clock, event.value & 0xFF) for event in trace
            if event.device == PSG and event.operation == WRITE and event.width == 1]


def _candidate_ym_writes(trace_path):
    with trace_path.open("rb") as stream:
        trace = list(read_trace(stream))
    return [(event.clock, (event.address >> 8) & 1, event.address & 0xFF,
             event.value & 0xFF)
            for event in trace
            if event.device == YM2612 and event.operation == WRITE and event.width == 1 and
            not (event.address & 0xC0000000)]


def _event_value_masks(case):
    masks = {}
    for op in case.operations:
        address = op.addr & 0xFFFFFF
        widths = (2, 2) if op.width == 4 else (op.width,)
        for width in widths:
            if op.code in (cases.READ, cases.POLL) and op.compare_mask:
                masks[(READ, width, address)] = op.compare_mask
            if op.width == 4:
                address = (address + 2) & 0xFFFFFF
    return masks


def _compare(case, native, candidate):
    masks = _event_value_masks(case)
    expected_reads = {}
    expected_read_counts = Counter()
    for op in case.operations:
        if op.code == cases.READ and op.compare_mask:
            key = (READ, op.width, op.addr & 0xFFFFFF)
            expected_reads.setdefault(key, []).append((op.value, op.compare_mask))
    for index, (native_event, candidate_event) in enumerate(zip(native, candidate)):
        if native_event[:3] != candidate_event[:3]:
            return (f"event {index}: native {native_event[:4]!r}, "
                    f"candidate {candidate_event[:4]!r}")
        mask = masks.get(native_event[:3], (1 << (native_event[1] * 8)) - 1)
        expected_sequence = expected_reads.get(native_event[:3], ())
        expected_index = expected_read_counts[native_event[:3]]
        expected = (expected_sequence[expected_index]
                    if expected_index < len(expected_sequence) else None)
        if expected:
            expected_read_counts[native_event[:3]] += 1
            expected_value, expected_mask = expected
            if (native_event[3] & expected_mask) != (expected_value & expected_mask):
                return (f"native read {index}: {native_event[:4]!r}, expected "
                        f"0x{expected_value:X} with mask 0x{expected_mask:X}")
            if (candidate_event[3] & expected_mask) != (expected_value & expected_mask):
                return (f"candidate read {index}: {candidate_event[:4]!r}, expected "
                        f"0x{expected_value:X} with mask 0x{expected_mask:X}")
        if (native_event[3] & mask) != (candidate_event[3] & mask):
            return (f"event {index}: native {native_event[:4]!r}, "
                    f"candidate {candidate_event[:4]!r}, mask 0x{mask:X}")
    if len(native) != len(candidate):
        first_extra = min(len(native), len(candidate))
        native_event = native[first_extra] if first_extra < len(native) else None
        candidate_event = candidate[first_extra] if first_extra < len(candidate) else None
        return (f"event count: native {len(native)}, candidate {len(candidate)}; "
                f"first unmatched native {native_event!r}, candidate {candidate_event!r}")
    return None


def _vdp_port_accesses(bus_events):
    accesses = []
    for event in bus_events:
        addr = event[2]
        if 0xC00000 <= addr <= 0xDFFFFF:
            port = (addr & 0x1F) & 0x1D
            if port in (0, 4, 8):
                accesses.append((event[0], event[1], port, event[3], event[4]))
    return accesses


def _compare_vdp_offsets(case, native_bus, native_vdp, candidate_bus, candidate_vdp, check_clock):
    native_cpu = _vdp_port_accesses(native_bus)
    candidate_cpu = _vdp_port_accesses(candidate_bus)
    if len(native_cpu) != len(candidate_cpu) or len(native_vdp) != len(candidate_vdp):
        return (f"VDP event counts: native CPU/device {len(native_cpu)}/{len(native_vdp)}, "
                f"candidate CPU/device {len(candidate_cpu)}/{len(candidate_vdp)}")
    if len(native_cpu) != len(native_vdp):
        return f"VDP CPU/device accesses do not pair: {len(native_cpu)}/{len(native_vdp)}"
    vdp_masks = {}
    for step in case.operations:
        address = step.addr & 0xFFFFFF
        if step.code == cases.READ and step.compare_mask and 0xC00000 <= address <= 0xDFFFFF:
            port = (address & 0x1F) & 0x1D
            vdp_masks[(READ, step.width, port)] = step.compare_mask
    for index, (native_cpu_event, native_vdp_event, candidate_cpu_event,
                candidate_vdp_event) in enumerate(zip(native_cpu, native_vdp,
                                                       candidate_cpu, candidate_vdp)):
        expected = native_cpu_event[:3] == candidate_cpu_event[:3]
        if not expected or native_vdp_event[:3] != candidate_vdp_event[:3]:
            return f"VDP port event {index} differs: native {native_vdp_event[:4]!r}, candidate {candidate_vdp_event[:4]!r}"
        mask = vdp_masks.get(native_vdp_event[:3], 0xFFFFFFFF)
        if (native_vdp_event[3] & mask) != (candidate_vdp_event[3] & mask):
            return f"VDP port event {index} value differs: native 0x{native_vdp_event[3]:X}, candidate 0x{candidate_vdp_event[3]:X}"
        if check_clock:
            native_delta = native_vdp_event[4] - native_cpu_event[4]
            candidate_delta = candidate_vdp_event[4] - candidate_cpu_event[4]
            if native_delta != candidate_delta:
                native_deltas = [vdp[4] - cpu[4] for cpu, vdp in zip(native_cpu, native_vdp)]
                candidate_deltas = [vdp[4] - cpu[4] for cpu, vdp in zip(candidate_cpu, candidate_vdp)]
                return (f"VDP port event {index} clock delta {native_delta}, candidate {candidate_delta}; "
                        f"all deltas native {native_deltas}, candidate {candidate_deltas}")
    return None


def _compare_dma(case, native_vdp, native_dma, candidate_vdp, candidate_dma):
    if not case.name.startswith("dma_"):
        return None
    if len(native_dma) != len(candidate_dma):
        return f"DMA event counts: native {len(native_dma)}, candidate {len(candidate_dma)}"
    for index, (native_event, candidate_event) in enumerate(zip(native_dma, candidate_dma)):
        if native_event[:4] != candidate_event[:4]:
            return (f"DMA event {index}: native {native_event[:4]!r}, "
                    f"candidate {candidate_event[:4]!r}")

    if case.name == "dma_fill":
        native_launch = next(event for event in native_vdp
                             if event[:4] == (WRITE, 2, 0, 0xA500))
        candidate_launch = next(event for event in candidate_vdp
                                if event[:4] == (WRITE, 2, 0, 0xA500))
    else:
        launch_value = 0x0090 if case.name == "dma_memory_vsram" else 0x0080
        native_launch = next(event for event in native_vdp
                             if event[:4] == (WRITE, 2, 4, launch_value))
        candidate_launch = next(event for event in candidate_vdp
                                if event[:4] == (WRITE, 2, 4, launch_value))
    if case.name == "dma_copy":
        native_phases = [event[4] % 40 for event in native_dma]
        candidate_phases = [event[4] % 40 for event in candidate_dma]
        native_intervals = [b[4] - a[4] for a, b in zip(native_dma, native_dma[1:])]
        candidate_intervals = [b[4] - a[4] for a, b in zip(candidate_dma, candidate_dma[1:])]
        if native_phases != candidate_phases or native_intervals != candidate_intervals:
            return (f"DMA copy slot clocks: native phases/intervals {native_phases}/{native_intervals}, "
                    f"candidate {candidate_phases}/{candidate_intervals}")
    else:
        native_deltas = [event[4] - native_launch[4] for event in native_dma]
        candidate_deltas = [event[4] - candidate_launch[4] for event in candidate_dma]
        if native_deltas != candidate_deltas:
            return f"DMA slot clocks: native {native_deltas}, candidate {candidate_deltas}"

    return None


def _compare_z80(case, native_z80, candidate_z80):
    if not case.z80_accesses:
        return None

    expected = []
    for op in case.z80_accesses:
        operation = READ if op.code == cases.READ else WRITE if op.code == cases.WRITE else None
        if operation is None:
            return f"unsupported expected Z80 operation {op.code}"
        expected.append((operation, op.width, op.addr & 0xFFFF, op.value))

    selected_keys = {event[:3] for event in expected}
    native = [event[:4] for event in native_z80 if event[:3] in selected_keys]
    candidate = [event[:4] for event in candidate_z80 if event[:3] in selected_keys]
    if len(native) != len(expected) or len(candidate) != len(expected):
        return (f"Z80 access counts: expected {len(expected)}, native {len(native)}, "
                f"candidate {len(candidate)}")
    for index, (want, native_event, candidate_event) in enumerate(
            zip(expected, native, candidate)):
        if native_event != want or candidate_event != want:
            return (f"Z80 access {index}: expected {want!r}, native {native_event!r}, "
                    f"candidate {candidate_event!r}")
    return None


def _ignore_render_sync_reads(case, bus_events, vdp_events):
    sync_ports = {
        (op.addr & 0x1F) & 0x1D
        for op in case.operations
        if op.code == cases.POLL and 0xC00000 <= (op.addr & 0xFFFFFF) <= 0xDFFFFF
    }
    bus_poll_addresses = {
        op.addr & 0xFFFFFF for op in case.operations
        if op.code == cases.POLL and not 0xC00000 <= (op.addr & 0xFFFFFF) <= 0xDFFFFF
    }
    if not sync_ports and not bus_poll_addresses:
        return bus_events, vdp_events

    def strip_bus_polls(events):
        if not bus_poll_addresses:
            return events
        keep_reads = Counter(op.addr & 0xFFFFFF for op in case.operations
                            if op.code == cases.READ and
                            (op.addr & 0xFFFFFF) in bus_poll_addresses)
        counts = Counter(event[2] for event in events
                         if event[0] == READ and event[2] in bus_poll_addresses)
        discard = Counter({addr: max(0, count - keep_reads[addr])
                           for addr, count in counts.items()})
        filtered = []
        for event in events:
            addr = event[2] if event[0] == READ else None
            if addr in bus_poll_addresses and discard[addr]:
                discard[addr] -= 1
            else:
                filtered.append(event)
        return filtered

    def bus_port(event):
        address = event[2]
        if 0xC00000 <= address <= 0xDFFFFF:
            return (address & 0x1F) & 0x1D
        return None

    keep_reads = Counter(
        (op.addr & 0x1F) & 0x1D for op in case.operations
        if op.code == cases.READ and
        0xC00000 <= (op.addr & 0xFFFFFF) <= 0xDFFFFF)

    def strip_sync_reads(events, port_for):
        counts = Counter(event_port for event in events
                         if event[0] == READ and
                         (event_port := port_for(event)) in sync_ports)
        discard = Counter({port: max(0, count - keep_reads[port])
                           for port, count in counts.items()})
        filtered = []
        for event in events:
            port = port_for(event) if event[0] == READ else None
            if port in sync_ports and discard[port]:
                discard[port] -= 1
            else:
                filtered.append(event)
        return filtered

    bus = strip_sync_reads(strip_bus_polls(bus_events), bus_port)
    vdp = strip_sync_reads(vdp_events, lambda event: event[2])
    return bus, vdp


def _read_ppm(path):
    data = path.read_bytes()
    header, separator, pixels = data.partition(b"\n255\n")
    if not separator:
        raise ValueError("PPM has no pixel separator")
    fields = header.split()
    if len(fields) != 3 or fields[0] != b"P6":
        raise ValueError("unsupported PPM header")
    width, height = int(fields[1]), int(fields[2])
    if len(pixels) != width * height * 3:
        raise ValueError("PPM pixel payload size does not match its dimensions")
    return width, height, pixels


def _compare_picture(native_path, candidate_path):
    native_w, native_h, native = _read_ppm(native_path)
    candidate_w, candidate_h, candidate = _read_ppm(candidate_path)
    if (native_w, native_h) == (candidate_w, candidate_h):
        crop_x = crop_y = 0
    elif candidate_w - native_w == 27 and (candidate_h, native_h) in ((243, 224), (294, 240)):
        # The core's frame callback includes the overscan margins around the active raster.
        crop_x = 13
        crop_y = 11 if native_h == 224 else 30
    else:
        return f"picture dimensions: native {native_w}x{native_h}, candidate {candidate_w}x{candidate_h}"
    cropped = bytearray(native_w * native_h * 3)
    for y in range(native_h):
        source = ((y + crop_y) * candidate_w + crop_x) * 3
        target = y * native_w * 3
        cropped[target:target + native_w * 3] = candidate[source:source + native_w * 3]
    candidate = bytes(cropped)
    for pixel in range(native_w * native_h):
        offset = pixel * 3
        if native[offset:offset + 3] != candidate[offset:offset + 3]:
            x, y = pixel % native_w, pixel // native_w
            got = native[offset:offset + 3].hex()
            want = candidate[offset:offset + 3].hex()
            hashes = (hashlib.sha256(native).hexdigest(), hashlib.sha256(candidate).hexdigest())
            return (f"picture pixel ({x},{y}): native #{got}, candidate #{want}; "
                    f"frame SHA-256 native/candidate {hashes[0]}/{hashes[1]}")
    return None


def _compare_audio(native_path, candidate_path, sample_rate, chip="PSG",
                   ignore_start_frames=0, allow_tolerance=False):
    native = native_path.read_bytes()
    candidate = candidate_path.read_bytes()
    if len(native) != len(candidate):
        return f"{chip} stream lengths: native {len(native)} bytes, candidate {len(candidate)} bytes"
    if len(native) % 4:
        return f"{chip} streams are not complete stereo s16 frames"
    native_samples = struct.unpack("=" + "h" * (len(native) // 2), native)
    candidate_samples = struct.unpack("=" + "h" * (len(candidate) // 2), candidate)
    frame_count = len(native_samples) // 2
    if not allow_tolerance:
        for frame in range(max(0, ignore_start_frames), frame_count):
            for channel in range(2):
                native_sample = native_samples[frame * 2 + channel]
                candidate_sample = candidate_samples[frame * 2 + channel]
                if native_sample != candidate_sample:
                    return (f"{chip} sample {frame} channel {channel} at {sample_rate:g} Hz: "
                            f"native {native_sample}, candidate {candidate_sample}; exact PCM")
        return None

    # The source resamplers have different startup latency. Register writes
    # are paired separately; allow only a short output-phase shift and small
    # filter-edge differences in the PCM stream.
    phase_trials = []
    for phase in range(-AUDIO_PHASE_LIMIT, AUDIO_PHASE_LIMIT + 1):
        first = max(ignore_start_frames, -phase)
        stop = min(frame_count, frame_count - phase)
        errors = [abs(native_samples[frame * 2 + channel] -
                      candidate_samples[(frame + phase) * 2 + channel])
                  for frame in range(first, stop) for channel in range(2)]
        if errors:
            phase_trials.append((sum(errors) / len(errors), max(errors), phase, first, stop))
    mean_error, peak_error, phase, first, stop = min(phase_trials)
    if mean_error > AUDIO_MEAN_ERROR_LIMIT or peak_error > AUDIO_PEAK_ERROR_LIMIT:
        for frame in range(first, stop):
            for channel in range(2):
                native_sample = native_samples[frame * 2 + channel]
                candidate_sample = candidate_samples[(frame + phase) * 2 + channel]
                if abs(native_sample - candidate_sample) > AUDIO_PEAK_ERROR_LIMIT:
                    return (f"{chip} sample {frame} channel {channel} at {sample_rate:g} Hz: "
                            f"native {native_sample}, candidate {candidate_sample}; "
                            f"phase {phase}, mean error {mean_error:.1f}")
        return (f"{chip} stream at {sample_rate:g} Hz: phase {phase}, mean error "
                f"{mean_error:.1f}, peak error {peak_error}")
    return None


def _active_picture(path, width, height):
    frame_width, frame_height, pixels = _read_ppm(path)
    if (frame_width, frame_height) == (width, height):
        return pixels
    if frame_width - width == 27 and frame_height - height in (19, 54):
        crop_x = 13
        crop_y = 11 if height == 224 else 30
        active = bytearray(width * height * 3)
        for y in range(height):
            source = ((y + crop_y) * frame_width + crop_x) * 3
            target = y * width * 3
            active[target:target + width * 3] = pixels[source:source + width * 3]
        return bytes(active)
    raise ValueError(f"cannot crop {frame_width}x{frame_height} to {width}x{height}")


def _weave_fields(first_path, second_path, width, field_height, output_path):
    first = _active_picture(first_path, width, field_height)
    second = _active_picture(second_path, width, field_height)
    frame = bytearray(width * field_height * 2 * 3)
    for y in range(field_height):
        source = y * width * 3
        even = (2 * y) * width * 3
        odd = (2 * y + 1) * width * 3
        frame[even:even + width * 3] = first[source:source + width * 3]
        frame[odd:odd + width * 3] = second[source:source + width * 3]
    output_path.write_bytes(f"P6\n{width} {field_height * 2}\n255\n".encode() + frame)


def run_one(case, assembler, z80_assembler, core, testcart):
    with tempfile.TemporaryDirectory(prefix="nmd-rom-") as temp:
        base = pathlib.Path(temp)
        capture_psg = case.capture_audio or case.capture_mixed_audio
        capture_ym = case.capture_ym_audio or case.capture_mixed_audio
        try:
            rom, steps = mdtest.build_case(case, base, assembler, z80_assembler)
        except RuntimeError as error:
            return str(error)
        oracle_out = base / "oracle"
        frame_count = case.capture_frames if case.render or capture_psg or capture_ym else 1
        frame_count = max(frame_count, len(case.input_frames))
        frames = str(frame_count)
        input_script = _case_input_script(case)
        input_path = None
        if input_script:
            input_path = base / "input.json"
            input_path.write_text(json.dumps(input_script, sort_keys=True))
        candidate_command = [
            sys.executable, str(ROOT / "consoles/md/tests/oracle/run.py"),
            "--core", str(core), "--rom", str(rom), "--frames", frames,
            "--region", case.region, "--pad1", case.pad1, "--pad2", case.pad2,
            "--out", str(oracle_out), "--trace",
        ]
        if input_path:
            candidate_command.extend(("--input", str(input_path)))
        candidate = subprocess.run(candidate_command, capture_output=True, text=True, timeout=90)
        if candidate.returncode:
            return "candidate run failed: " + candidate.stderr.strip()[-500:]

        manifest = json.loads((oracle_out / "manifest.json").read_text())
        ym_source_streams = manifest.get("audio_source_streams", {})
        ym_stream = ym_source_streams.get("ym2612") if capture_ym else None
        psg_stream = ym_source_streams.get("psg") if capture_psg else None
        psg_rate = psg_stream.get("sample_rate") if psg_stream else None
        ym_rate = ym_stream.get("sample_rate") if ym_stream else None
        if capture_psg and (not psg_stream or not psg_rate):
            return "candidate did not export raw PSG source samples"
        if capture_ym and (not ym_stream or not ym_rate):
            return "candidate did not export raw YM2612 source samples"
        master_clock_hz = 53203395 if case.region == "E" else 53693175
        mixed_render_clock = None
        if case.capture_mixed_audio:
            ym_end_clock = round(ym_stream["frames"] * master_clock_hz / ym_rate)
            psg_end_clock = round(psg_stream["frames"] * master_clock_hz / psg_rate)
            mixed_render_clock = max(ym_end_clock, psg_end_clock) + 64 * 1008
        psg_events_path = None
        if capture_psg:
            psg_writes = _candidate_psg_writes(oracle_out / "bus.trace")
            if not psg_writes:
                return "candidate did not trace PSG register writes"
            end_clock = round(psg_stream["frames"] * master_clock_hz / psg_rate)
            render_clock = mixed_render_clock or end_clock + 64 * 240
            psg_events_path = base / "psg.events"
            payload = bytearray(struct.pack(">4sHIQ", b"NPSG", 1, len(psg_writes), render_clock))
            for clock, value in psg_writes:
                payload.extend(struct.pack(">QB", clock, value))
            psg_events_path.write_bytes(payload)

        ym_events_path = None
        if capture_ym:
            ym_writes = _candidate_ym_writes(oracle_out / "bus.trace")
            if not ym_writes:
                return "candidate did not trace YM2612 register writes"
            end_clock = round(ym_stream["frames"] * master_clock_hz / ym_rate)
            render_clock = mixed_render_clock or end_clock + 64 * 1008
            ym_events_path = base / "ym.events"
            payload = bytearray(struct.pack(">4sHIQ", b"NYM1", 1, len(ym_writes), render_clock))
            for clock, bank, address, value in ym_writes:
                payload.extend(struct.pack(">QBBB", clock, bank, address, value))
            ym_events_path.write_bytes(payload)

        native_picture = base / "native.ppm"
        native_command = [str(testcart), "--rom", str(rom), "--steps", str(steps)]
        native_command.extend(("--region", case.region, "--pad1", case.pad1, "--pad2", case.pad2))
        first_input = case.input_frames[0] if case.input_frames else ((), ())
        native_command.extend(("--buttons1", str(_native_button_mask(first_input[0])),
                               "--buttons2", str(_native_button_mask(first_input[1]))))
        native_psg = base / "native-psg.raw"
        if case.capture_audio:
            native_command.extend(("--psg-source-out", str(native_psg),
                                   "--psg-source-frames", str(psg_stream["frames"]),
                                   "--psg-events", str(psg_events_path)))
        native_ym = base / "native-ym.raw"
        if case.capture_ym_audio:
            native_command.extend(("--ym-source-out", str(native_ym),
                                   "--ym-source-frames", str(ym_stream["frames"]),
                                   "--ym-events", str(ym_events_path)))
        native_mixed = base / "native-mixed.raw"
        candidate_mixed = base / "candidate-mixed.raw"
        if case.capture_mixed_audio:
            native_command.extend(("--mix-out", str(native_mixed),
                                   "--mix-reference-out", str(candidate_mixed),
                                   "--mix-ym-source", str(oracle_out / ym_stream["file"]),
                                   "--mix-psg-source", str(oracle_out / psg_stream["file"]),
                                   "--mix-ym-rate", str(ym_rate),
                                   "--mix-psg-rate", str(psg_rate),
                                   "--ym-events", str(ym_events_path),
                                   "--psg-events", str(psg_events_path)))
        if case.render or (capture_psg and not psg_events_path) or (
                capture_ym and not ym_events_path):
            native_command.extend(("--frames", frames))
        if case.render:
            native_command.extend(("--picture-out", str(native_picture)))
        native = subprocess.run(native_command,
                                capture_output=True, text=True, timeout=30)
        if native.returncode:
            return "native testcart failed: " + native.stderr.strip()[-500:]

        try:
            native_bus, native_vdp, native_dma, native_z80 = _native_events(native)
            candidate_bus, candidate_vdp, candidate_dma, candidate_z80 = _candidate_events(
                oracle_out / "bus.trace", _target_addresses(case))
        except (OSError, ValueError) as error:
            return "trace read failed: " + str(error)
        native_bus, native_vdp = _ignore_render_sync_reads(case, native_bus, native_vdp)
        candidate_bus, candidate_vdp = _ignore_render_sync_reads(case, candidate_bus, candidate_vdp)
        difference = _compare(case, native_bus, candidate_bus)
        if difference:
            return difference
        difference = _compare_vdp_offsets(case, native_bus, native_vdp, candidate_bus, candidate_vdp,
                                          case.direct)
        if difference:
            return difference
        difference = _compare_dma(case, native_vdp, native_dma,
                                  candidate_vdp, candidate_dma)
        if difference:
            return difference
        difference = _compare_z80(case, native_z80, candidate_z80)
        if difference:
            return difference
        if case.capture_audio:
            difference = _compare_audio(native_psg,
                                        oracle_out / psg_stream["file"], psg_rate,
                                        chip="PSG",
                                        allow_tolerance=(case.name in {
                                            "sound_psg_tone", "sound_psg_noise"}))
            if difference:
                return difference
        if case.capture_ym_audio:
            difference = _compare_audio(native_ym, oracle_out / ym_source_streams["ym2612"]["file"],
                                        ym_rate, chip="YM2612",
                                        ignore_start_frames=16 if case.name == "sound_ym_dac" else 0,
                                        allow_tolerance=(case.name == "sound_ym_dac"))
            if difference:
                return difference
        if case.capture_mixed_audio:
            difference = _compare_audio(native_mixed, candidate_mixed, MD_SOUND_OUTPUT_RATE,
                                        chip="Mixed PSG/YM2612")
            if difference:
                return difference
        if case.render:
            frame_path = oracle_out / f"frame_{case.capture_frames:06}.ppm"
            if case.weave_fields:
                candidate_width, field_height = case.weave_dimensions
                woven = base / "candidate-woven.ppm"
                _weave_fields(oracle_out / "frame_000001.ppm", frame_path,
                              candidate_width, field_height, woven)
                frame_path = woven
            difference = _compare_picture(native_picture, frame_path)
            if difference:
                return difference
        return None


def _known_cases():
    known = {}
    path = HERE / "KNOWN"
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, _, reason = line.partition(" ")
        known[name] = reason.strip()
    return known


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", metavar="NAME,...",
                        help="run only these comma-separated case names")
    args = parser.parse_args(argv)

    tests = cases.all_cases()
    if args.only is not None:
        requested = {name.strip() for name in args.only.split(",") if name.strip()}
        if not requested:
            parser.error("--only requires at least one case name")
        available = {case.name for case in tests}
        unknown = sorted(requested - available)
        if unknown:
            parser.error("unknown case name(s): " + ", ".join(unknown))
        tests = [case for case in tests if case.name in requested]

    extension = "dylib" if sys.platform == "darwin" else "so"
    assembler = pathlib.Path(os.environ.get(
        "NMD_ASSEMBLER", ROOT / "build/toolchains/vasm/vasmm68k_mot"))
    z80_assembler = pathlib.Path(os.environ.get(
        "NMD_Z80_ASSEMBLER", ROOT / "build/toolchains/vasm/vasmz80_oldstyle"))
    core = pathlib.Path(os.environ.get(
        "NMD_ORACLE_CORE", ROOT / "build/toolchains/blastem" / ("libblastem." + extension)))
    testcart = pathlib.Path(os.environ.get(
        "NMD_TESTCART", ROOT / "build/null/tests/md_testcart"))
    for path in (assembler, z80_assembler, core, testcart):
        if not path.is_file():
            sys.exit("run.py: missing required tool or executable: " + path.name)

    known = _known_cases()
    failures = fixed = known_differences = 0
    for case in tests:
        difference = run_one(case, assembler, z80_assembler, core, testcart)
        if case.name in known:
            if difference:
                known_differences += 1
                print(f"{case.name:20s} KNOWN {difference} ({known[case.name]})")
            else:
                fixed += 1
                print(f"{case.name:20s} now matches; remove it from KNOWN")
        elif difference:
            failures += 1
            print(f"{case.name:20s} FAIL {difference}")
        else:
            print(f"{case.name:20s} ok")
    print(f"{failures} of {len(tests)} unlisted cases differ; "
          f"{known_differences} documented candidate differences; {fixed} known cases now pass")
    return 1 if failures or fixed else 0


if __name__ == "__main__":
    sys.exit(main())
