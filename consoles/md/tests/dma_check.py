#!/usr/bin/env python3
"""Run VDP DMA unit cases, including fatal source-bound checks."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
BINARY = ROOT / "build/null/tests/dma_check"

result = subprocess.run([str(BINARY)], capture_output=True, text=True)
if result.returncode:
    sys.exit("dma_check: valid transfer cases failed: " + result.stderr)

for mode, message in (("source-bounds", "outside cartridge ROM"),
                      ("zero-count", "outside cartridge ROM")):
    result = subprocess.run([str(BINARY), mode], capture_output=True, text=True)
    if (result.returncode != 70 or not result.stderr.startswith("nativemd: ") or
            message not in result.stderr):
        sys.exit(f"dma_check: {mode} fatal mismatch: {result.returncode}, {result.stderr!r}")

print("dma_check: ok")
