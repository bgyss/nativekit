#!/usr/bin/env python3
"""Check named and unnamed invalid-access diagnostics."""
import subprocess
import sys
import pathlib
import tempfile

for args, expected in [([], "an access of kind 2, not one of the console's 2"),
                       (["--names"], "an access of kind 2, not one of the console's 2 (the console's kinds: memory, io)")]:
    result = subprocess.run([sys.argv[1], *args], capture_output=True, text=True)
    if result.returncode != 70 or result.stderr.strip() != expected:
        sys.exit("kinds_check: unexpected result %s: %r" % (result.returncode, result.stderr))

with tempfile.TemporaryDirectory() as temp:
    counts = pathlib.Path(temp) / "counts.txt"
    result = subprocess.run([sys.argv[1], "--groups", "--headless", "--frames", "1",
                             "--counts", str(counts)], capture_output=True, text=True)
    if result.returncode or not counts.is_file() or counts.read_text().strip() != "F 1 1 0 1 1":
        sys.exit("kinds_check: independent poll groups were merged: %r %r" %
                 (result.returncode, counts.read_text() if counts.is_file() else result.stderr))

    counts = pathlib.Path(temp) / "counts-across-frames.txt"
    result = subprocess.run([sys.argv[1], "--groups-across-frames", "--headless", "--frames", "2",
                             "--counts", str(counts)], capture_output=True, text=True)
    expected = "F 1 0 0 0 0\nF 2 1 0 1 0\n"
    if result.returncode or not counts.is_file() or counts.read_text() != expected:
        sys.exit("kinds_check: pending reads were attributed to the wrong frame: %r %r" %
                 (result.returncode, counts.read_text() if counts.is_file() else result.stderr))
print("kinds_check: ok")
