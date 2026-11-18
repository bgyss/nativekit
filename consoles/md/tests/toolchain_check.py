#!/usr/bin/env python3
"""Check the locally built assemblers against small synthetic instructions."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
tools = root / "build" / "toolchains" / "vasm"
cases = (
    ("vasmm68k_mot", "    moveq #1,d0\n    bra.s *\n", bytes.fromhex("700160fe")),
    ("vasmz80_oldstyle", "    ld a,42\n    halt\n", bytes.fromhex("3e2a76")),
)
with tempfile.TemporaryDirectory() as directory:
    for executable, source, expected in cases:
        binary = tools / executable
        if not binary.is_file():
            sys.exit(f"toolchain_check: missing {executable}")
        asm = pathlib.Path(directory) / "fixture.s"
        out = pathlib.Path(directory) / "fixture.bin"
        out.unlink(missing_ok=True)
        asm.write_text(source)
        result = subprocess.run([str(binary), "-Fbin", "-o", str(out), str(asm)],
                                capture_output=True, text=True)
        if result.returncode or not out.exists() or out.read_bytes() != expected:
            sys.exit(f"toolchain_check: {executable} failed: {result.stderr}")
print("toolchain_check: ok")
