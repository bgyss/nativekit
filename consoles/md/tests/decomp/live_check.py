#!/usr/bin/env python3
"""The lockstep's live runtime on a fake machine (D05).

Builds check/live.c with live/harness.c and runs it: a routine polling a
port with the same registers on every pass pauses where the original was
interrupted, after the polls the original made before, and not at an
earlier pass whose registers match.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
HERE = pathlib.Path(__file__).resolve().parent


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-live-") as temp:
        program = pathlib.Path(temp) / "live_check"
        build = subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
             "-Wno-deprecated-declarations", "-DNMD_LIVE_AT",
             "-I" + str(ROOT / "consoles/md/include"),
             "-I" + str(ROOT / "consoles/md/tools/decomp/check"),
             "-o", str(program), str(HERE / "live" / "harness.c"),
             str(ROOT / "consoles/md/tools/decomp/check/live.c")],
            capture_output=True, text=True)
        if build.returncode:
            sys.exit("live_check: the harness does not build: " + build.stderr)
        result = subprocess.run([str(program)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.returncode:
            sys.exit(result.stderr.strip() or "live_check: failed")


if __name__ == "__main__":
    main()
