#!/usr/bin/env python3
"""Run every NMDH ABI, format, runner and actual-core gate."""
import pathlib
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[3]

CHECKS = (
    ("benchmark resource calculations", "consoles/md/tests/decomp/benchmark_check.py"),
    ("NMDH ABI", "consoles/md/tests/decomp/abi_check.py"),
    ("NMDH format and framing mutations", "consoles/md/tests/decomp/format_check.py"),
    ("runner identity, capability, timeout and storage controls",
     "consoles/md/tests/decomp/runner_check.py"),
    ("NMDT v2 checkpoint state", "consoles/md/tests/trace_abi_check.py"),
    ("actual 68000 core and planted state errors",
     "consoles/md/tests/decomp/core_check.py", "--case", "m68k"),
    ("actual Z80 core and planted state errors",
     "consoles/md/tests/decomp/core_check.py", "--case", "z80"),
    ("actual synchronized handoff and stop boundaries",
     "consoles/md/tests/decomp/core_check.py", "--case", "handoff"),
    ("actual bus intervals, epochs and event ordering",
     "consoles/md/tests/decomp/core_check.py", "--case", "bus"),
    ("actual post-frame and STOP checkpoint replay",
     "consoles/md/tests/decomp/core_check.py", "--case", "checkpoint"),
)


def validate_mise_entrypoint():
    config = tomllib.loads((ROOT / "mise.toml").read_text())
    task = config.get("tasks", {}).get("test:md-decomp")
    if not isinstance(task, dict):
        raise AssertionError("missing non-empty mise task test:md-decomp")
    if "tools:md" not in task.get("depends", []):
        raise AssertionError("test:md-decomp must depend on tools:md")
    if task.get("run") != "python3 consoles/md/tests/decomp_check.py":
        raise AssertionError("test:md-decomp must run this integration gate")


def main():
    validate_mise_entrypoint()
    for check in CHECKS:
        label, *arguments = check
        command = [sys.executable, *arguments]
        print("decomp integration: " + label, flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
    print("decomp integration checks: ok")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, subprocess.CalledProcessError) as exc:
        print("decomp_check: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
