#!/usr/bin/env python3
"""Compare shared synthetic bus cases on BlastEm and the native C model."""
import os
import pathlib
import hashlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "oracle"))

import cases  # noqa: E402
import mdtest  # noqa: E402
from trace import M68K, VDP, DMA, READ, WRITE, read as read_trace  # noqa: E402


def _target_addresses(case):
    addresses = set()
    for op in case.operations:
        address = op.addr & 0xFFFFFF
        addresses.add(address)
        if op.width == 4:
            addresses.add((address + 2) & 0xFFFFFF)
    return addresses


def _native_events(result):
    bus_events, vdp_events, dma_events = [], [], []
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) == 6 and fields[0] == "VDP":
            _, clock, operation, width, port, value = fields
            event = (int(operation), int(width), int(port, 16), int(value, 16), int(clock))
            (dma_events if event[0] == DMA else vdp_events).append(event)
            continue
        if len(fields) != 5:
            raise ValueError("testcart emitted an invalid bus trace line")
        clock, operation, width, addr, value = fields
        bus_events.append((int(operation), int(width), int(addr, 16), int(value, 16), int(clock)))
    return bus_events, vdp_events, dma_events


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
    return bus, vdp, dma


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
    for index, (native_event, candidate_event) in enumerate(zip(native, candidate)):
        if native_event[:3] != candidate_event[:3]:
            return (f"event {index}: native {native_event[:4]!r}, "
                    f"candidate {candidate_event[:4]!r}")
        mask = masks.get(native_event[:3], (1 << (native_event[1] * 8)) - 1)
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


def _ignore_render_sync_reads(case, bus_events, vdp_events):
    if not case.render:
        return bus_events, vdp_events
    bus = [event for event in bus_events
           if not (event[0] == READ and 0xC00000 <= event[2] <= 0xDFFFFF and
                   ((event[2] & 0x1F) & 0x1D) in (4, 8))]
    vdp = [event for event in vdp_events
           if not (event[0] == READ and event[2] in (4, 8))]
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


def run_one(case, assembler, core, testcart):
    with tempfile.TemporaryDirectory(prefix="nmd-rom-") as temp:
        base = pathlib.Path(temp)
        try:
            rom, steps = mdtest.build_case(case, base, assembler)
        except RuntimeError as error:
            return str(error)
        oracle_out = base / "oracle"
        frames = str(case.capture_frames) if case.render else "1"
        candidate = subprocess.run([
            sys.executable, str(ROOT / "consoles/md/tests/oracle/run.py"),
            "--core", str(core), "--rom", str(rom), "--frames", frames,
            "--region", case.region,
            "--out", str(oracle_out), "--trace",
        ], capture_output=True, text=True, timeout=90)
        if candidate.returncode:
            return "candidate run failed: " + candidate.stderr.strip()[-500:]

        native_picture = base / "native.ppm"
        native_command = [str(testcart), "--rom", str(rom), "--steps", str(steps)]
        native_command.extend(("--region", case.region))
        if case.render:
            native_command.extend(("--frames", frames))
            native_command.extend(("--picture-out", str(native_picture)))
        native = subprocess.run(native_command,
                                capture_output=True, text=True, timeout=30)
        if native.returncode:
            return "native testcart failed: " + native.stderr.strip()[-500:]

        try:
            native_bus, native_vdp, native_dma = _native_events(native)
            candidate_bus, candidate_vdp, candidate_dma = _candidate_events(
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
        if case.render:
            frame_path = oracle_out / f"frame_{case.capture_frames:06}.ppm"
            if case.weave_fields:
                candidate_width = 320 if case.name.endswith("h40") else 256
                field_height = 240 if case.region == "E" else 224
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


def main():
    extension = "dylib" if sys.platform == "darwin" else "so"
    assembler = pathlib.Path(os.environ.get(
        "NMD_ASSEMBLER", ROOT / "build/toolchains/vasm/vasmm68k_mot"))
    core = pathlib.Path(os.environ.get(
        "NMD_ORACLE_CORE", ROOT / "build/toolchains/blastem" / ("libblastem." + extension)))
    testcart = pathlib.Path(os.environ.get(
        "NMD_TESTCART", ROOT / "build/null/tests/md_testcart"))
    for path in (assembler, core, testcart):
        if not path.is_file():
            sys.exit("run.py: missing required tool or executable: " + path.name)

    known = _known_cases()
    failures = fixed = known_differences = 0
    tests = cases.all_cases()
    for case in tests:
        difference = run_one(case, assembler, core, testcart)
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
