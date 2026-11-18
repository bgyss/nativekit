"""Run a private title gate using its existing command-line arguments.

md: pass lockstep.py arguments, including --out. Requires certified lockstep.
nes: pass verify-dw1-start-input.py arguments. Requires strict Start bus equality.
Both verdicts apply only to the selected scenario and horizon.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--title-repo", type=Path, help="Dragon Warrior checkout")
    parser.add_argument("console", choices=["md", "nes"])
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.console == "nes":
        if not args.title_repo:
            parser.error("NES requires --title-repo")
        program = args.title_repo / "tools/verify-dw1-start-input.py"
        return subprocess.call([sys.executable, str(program), "--strict-bus", *args.arguments])
    options = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    options.add_argument("--out", required=True, type=Path)
    run, _ = options.parse_known_args(args.arguments)
    program = ROOT / "consoles/md/tools/decomp/lockstep.py"
    result = subprocess.run([sys.executable, str(program), *args.arguments])
    if result.returncode:
        return result.returncode
    report = json.loads((run.out / "lockstep.json").read_text())
    return 0 if report["certified"] else 1


if __name__ == "__main__":
    sys.exit(main())
