#!/usr/bin/env python3
"""Run Z80 coroutine cases and verify invalid Z80 contracts fail clearly."""
import pathlib
import subprocess
import sys


root = pathlib.Path(__file__).resolve().parents[3]
binary = root / "build/null/tests/z80_check"
result = subprocess.run([str(binary)], capture_output=True, text=True)
if result.returncode:
    sys.exit("z80_check: reset/BUSREQ routine cases failed: " + result.stderr)

fatal_cases = {
    "missing-entry": ("Z80", "routine"),
    "unmapped-access": ("unsupported Z80 memory read", "$2000"),
}
for mode, fragments in fatal_cases.items():
    result = subprocess.run([str(binary), mode], capture_output=True, text=True)
    if result.returncode != 70 or not result.stderr.startswith("nativemd: ") or any(
            fragment not in result.stderr for fragment in fragments):
        sys.exit(f"z80_check: {mode} fatal mismatch: {result.returncode}, {result.stderr!r}")

print("z80_check: ok")
