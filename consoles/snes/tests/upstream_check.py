#!/usr/bin/env python3
"""Run compatible upstream contract checks from a selected CMake build tree."""
import os
from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: upstream_check.py BUILD_DIR")
    build = Path(sys.argv[1]).resolve()
    tests = build / "tests"
    env = os.environ.copy()
    env["NSHVC_TESTBUILD"] = str(tests)
    cases = [(name, [], 0) for name in [
        "input", "resample_check", "vram_direct", "waits", "ipl", "chip",
        "chip_irq", "window_edges", "window_map", "window_columns",
        "canvas32_check", "backdrop_check", "scaling_check",
    ]]
    cases += [("chip", ["--direct"], 0), ("waits", ["--allowance"], 0),
              ("ipl", ["--profile", "--sweep"], 0),
              ("pause_check", ["--headless", "--frames", "60"], 0),
              ("watchdog_check", [], 0), ("watchdog_check", ["--stall"], 70)]
    cases += [("vram_direct", [flag], 70) for flag in ["--copy0", "--direct0"]]
    cases += [("waits", [flag], 70) for flag in ["--offpoll", "--held", "--heldpoll"]]
    failures = 0
    for name, args, expected in cases:
        run_env = env.copy()
        if expected == 70 or name == "watchdog_check":
            run_env["NSHVC_WATCHDOG"] = "1"
        try:
            r = subprocess.run([str(tests / name), *args], env=run_env,
                               capture_output=True, text=True, timeout=120)
            ok = r.returncode == expected
            print(f"{'PASS' if ok else 'FAIL'} {name} {' '.join(args)} (exit {r.returncode}, expected {expected})", flush=True)
            if not ok:
                print((r.stdout + r.stderr)[-4000:])
        except subprocess.TimeoutExpired:
            ok = False
            print(f"FAIL {name} {' '.join(args)} timed out", flush=True)
        failures += not ok
    r = subprocess.run([sys.executable, str(Path(__file__).parent / "unit" / "hdma_bulk.py")],
                       env=env, timeout=120)
    failures += r.returncode != 0
    print(f"{failures} upstream contract check(s) failed")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
