#!/usr/bin/env python3
"""Run scheduler tests and check missing-vector fatal behavior."""
import os
import pathlib
import subprocess
import sys
import tempfile


root = pathlib.Path(__file__).resolve().parents[3]
binary = root / "build/null/tests/interrupt_check"
oracle_runner = root / "consoles/md/tests/oracle/run.py"
extension = "dylib" if sys.platform == "darwin" else "so"
assembler = pathlib.Path(os.environ.get("NMD_ASSEMBLER", root / "build/toolchains/vasm/vasmm68k_mot"))
core = pathlib.Path(os.environ.get(
    "NMD_ORACLE_CORE", root / "build/toolchains/blastem" / ("libblastem." + extension)))


def check_hint_trace():
    if not assembler.is_file() or not core.is_file():
        sys.exit("interrupt_check: missing vasm or candidate core")
    with tempfile.TemporaryDirectory(prefix="nmd-hint-") as temp:
        directory = pathlib.Path(temp)
        source = directory / "hint.s"
        program = directory / "hint-program.bin"
        rom_path = directory / "hint.bin"
        output = directory / "oracle"
        source.write_text("        move.w #$8114,$c00004\n"
                          "        move.w #$8010,$c00004\n"
                          "        move.w #$8a01,$c00004\n"
                          "loop:   bra.s loop\n")
        assembled = subprocess.run([str(assembler), "-Fbin", "-o", str(program), str(source)],
                                   capture_output=True, text=True)
        if assembled.returncode:
            sys.exit("interrupt_check: HINT fixture assembly failed: " + assembled.stderr)
        rom = bytearray(32768)
        rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
        rom[4:8] = (0x00000200).to_bytes(4, "big")
        rom[0x100:0x110] = b"SEGA GENESIS    "
        rom[0x120:0x150] = b"NATIVEKIT HINT TEST".ljust(48, b" ")
        rom[0x150:0x180] = b"NATIVEKIT HINT TEST".ljust(48, b" ")
        rom[0x1F0:0x200] = b"JUE             "
        code = program.read_bytes()
        rom[0x200:0x200 + len(code)] = code
        rom_path.write_bytes(rom)
        result = subprocess.run([sys.executable, str(oracle_runner), "--core", str(core),
                                 "--rom", str(rom_path), "--frames", "1", "--out", str(output),
                                 "--trace"], capture_output=True, text=True, timeout=90)
        if result.returncode:
            sys.exit("interrupt_check: candidate HINT fixture failed: " + result.stderr[-500:])

        sys.path.insert(0, str(root / "consoles/md/tests/oracle"))
        from trace import IRQ, VDP, read as read_trace
        with (output / "bus.trace").open("rb") as stream:
            candidate = [(event.address, event.clock) for event in read_trace(stream)
                         if event.device == VDP and event.operation == IRQ and event.address < 224]

        native = subprocess.run([str(binary), "hint-trace"], capture_output=True, text=True)
        if native.returncode:
            sys.exit("interrupt_check: native HINT fixture failed: " + native.stderr[-500:])
        native_events = []
        for line in native.stdout.splitlines():
            line_number, clock = line.split("\t")
            native_events.append((int(line_number), int(clock)))

        # The candidate trace starts in its local core epoch; the native C
        # harness starts at clock zero. Compare line-relative timing deltas.
        def relative(events):
            if not events:
                return []
            first_line, first_clock = events[0]
            return [(line - first_line, clock - first_clock) for line, clock in events]

        native_relative = relative(native_events)
        candidate_relative = relative(candidate)
        if native_relative != candidate_relative:
            limit = min(len(native_relative), len(candidate_relative))
            first = next((i for i in range(limit)
                          if native_relative[i] != candidate_relative[i]), limit)
            native_event = native_relative[first] if first < len(native_relative) else None
            candidate_event = candidate_relative[first] if first < len(candidate_relative) else None
            sys.exit("interrupt_check: relative HINT trace mismatch at event " + str(first) +
                     f": native {native_event!r}, candidate {candidate_event!r}; " +
                     f"counts {len(native_relative)}/{len(candidate_relative)}")
        print(f"interrupt_check: HINT trace {len(native_events)} events match candidate")


result = subprocess.run([str(binary)], capture_output=True, text=True)
if result.returncode:
    sys.exit("interrupt_check: scheduler cases failed: " + result.stderr)

missing = subprocess.run([str(binary), "missing"], capture_output=True, text=True)
if (missing.returncode != 70 or not missing.stderr.startswith("nativemd: ") or
        "interrupt vector" not in missing.stderr):
    sys.exit(f"interrupt_check: missing-vector fatal mismatch: {missing.returncode}, {missing.stderr!r}")

check_hint_trace()
print("interrupt_check: ok")
