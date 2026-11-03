#!/usr/bin/env python3
"""Run the MD bus check and verify fatal address/bus conditions."""
import pathlib
import subprocess
import sys


root = pathlib.Path(__file__).resolve().parents[3]
binary = root / "build/null/tests/bus_check"
result = subprocess.run([str(binary)], capture_output=True, text=True)
if result.returncode:
    sys.exit("bus_check: valid bus cases failed: " + result.stderr)

fatal_cases = {
    "odd-word-read": "address error: odd-address word read",
    "odd-word-write": "address error: odd-address word write",
    "odd-long-read": "address error: odd-address longword read",
    "odd-long-write": "address error: odd-address longword write",
    "rom-write": "write to cartridge ROM",
    "reserved-read": "unsupported 68000 byte read",
    "z80-without-busreq": "without BUSREQ",
    "z80-word": "word access to 8-bit Z80 RAM",
    "ym-without-busreq": "without BUSREQ acknowledgement",
    "bank-without-busreq": "bank-register write at $A06000 without BUSREQ acknowledgement",
    "vdp-word-read-0a": "prohibited VDP word read",
    "vdp-word-read-d-0a": "prohibited VDP word read",
    "vdp-byte-read-0a": "VDP read state is not implemented",
    "vdp-byte-read-d-0a": "VDP read state is not implemented",
}
for mode, message in fatal_cases.items():
    result = subprocess.run([str(binary), mode], capture_output=True, text=True)
    if result.returncode != 70 or not result.stderr.startswith("nativemd: ") or message not in result.stderr:
        sys.exit(f"bus_check: {mode} fatal mismatch: {result.returncode}, {result.stderr!r}")

print("bus_check: ok")
