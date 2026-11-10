#!/usr/bin/env python3
"""Run the native execution cases and check each missing target fails clearly."""
import pathlib
import subprocess
import sys


root = pathlib.Path(__file__).resolve().parents[3]
binary = root / "build/null/tests/execution_check"
result = subprocess.run([str(binary)], capture_output=True, text=True)
if result.returncode or "execution_check: ok" not in result.stdout:
    sys.exit("execution_check: native execution cases failed: " + result.stderr)

fatal_cases = {
    "missing_reset": "reset vector $000280 has no C routine",
    "returns": "the 68000 routine at $000200 returned to no caller",
    "run_frame_inside": "nmd_run_frame from the game's 68000 routine",
    "power_inside": "nmd_power from the game's 68000 routine",
    "short_rom": "native execution needs a ROM with reset vectors",
    "bad_return": "interrupt handler returned to $000555 with no C routine",
    "restart_missing": "nmd_restart to $000555 with no C routine",
    "restart_host": "nmd_restart outside the game's 68000 routine",
    "missing_vint": "interrupt vector $78 points to $000340 with no C routine",
    "z80_missing_vint": "Z80 VINT vector $0038 has no C routine",
}
for mode, message in fatal_cases.items():
    result = subprocess.run([str(binary), mode], capture_output=True, text=True)
    if result.returncode != 70 or result.stderr.strip() != "nativemd: " + message:
        sys.exit(f"execution_check: {mode} fatal mismatch: {result.returncode}, {result.stderr!r}")

# Planted errors in the run: each named case passes alone and must fail with
# the error planted (1 skipped reset entry, 2 state lost across a yield,
# 3 an interrupt return that ignores the guest stack).
planted = {
    "1": ["boots", "resumes", "irq_runs"],
    "2": ["resumes", "repower", "irq_frame"],
    "3": ["irq_keeps_registers", "irq_sr_edit", "return_elsewhere"],
}
for plant, names in planted.items():
    for name in names:
        clean = subprocess.run([str(binary), "case", name], capture_output=True, text=True)
        if clean.returncode:
            sys.exit(f"execution_check: {name} failed alone: {clean.stderr!r}")
        broken = subprocess.run([str(binary), "case", name, plant], capture_output=True, text=True)
        if broken.returncode == 0 or "Assertion failed" not in broken.stderr and \
                not broken.stderr.startswith("nativemd: "):
            sys.exit(f"execution_check: planted error {plant} was not caught by {name}: "
                     f"{broken.returncode}, {broken.stderr!r}")

print(f"execution_check: ok ({len(fatal_cases)} fatal cases, {sum(map(len, planted.values()))} planted errors caught)")
