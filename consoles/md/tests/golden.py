#!/usr/bin/env python3
"""Record and compare deterministic outputs from synthetic nativemd cases.

  golden.py record OUT   run the full reference matrix and create its baseline
  golden.py compare OUT  rerun every synthetic case and compare its outputs

The manifest contains case names, result status, relative output names, and
SHA-256 values. ROMs and local paths stay in the ignored build workspace.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
WORK = ROOT / "build/golden-work/md"
FORMAT = "nmd-synthetic-golden-1"
sys.path.insert(0, str(HERE / "rom"))
import cases  # noqa: E402
import run as romrun  # noqa: E402


def _digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def snapshot_tree(directory):
    directory = pathlib.Path(directory)
    files = {}
    for root, dirs, names in os.walk(directory):
        dirs.sort()
        for name in sorted(names):
            path = pathlib.Path(root) / name
            if path.is_file() and not path.is_symlink():
                data = path.read_bytes()
                if name in ("candidate.stdout", "candidate.stderr", "native.stdout", "native.stderr"):
                    data = data.replace(str(directory).encode(), b"<case-work>")
                files[path.relative_to(directory).as_posix()] = _digest_bytes(data)
    return files


def compare_manifests(want, got):
    differences = []
    if want.get("format") != FORMAT or got.get("format") != FORMAT:
        return ["manifest format"]
    expected_cases = want.get("cases")
    actual_cases = got.get("cases")
    if not isinstance(expected_cases, dict) or not isinstance(actual_cases, dict):
        return ["case table"]
    for name in sorted(set(expected_cases) | set(actual_cases)):
        expected = expected_cases.get(name)
        actual = actual_cases.get(name)
        if expected is None:
            differences.append("%s: new case" % name)
            continue
        if actual is None:
            differences.append("%s: case missing" % name)
            continue
        if expected.get("status") != actual.get("status"):
            differences.append("%s: status" % name)
        expected_files = expected.get("outputs", {})
        actual_files = actual.get("outputs", {})
        for path in sorted(set(expected_files) | set(actual_files)):
            if expected_files.get(path) != actual_files.get(path):
                differences.append("%s: %s" % (name, path))
    return differences


def _tool_paths():
    extension = "dylib" if sys.platform == "darwin" else "so"
    return (
        pathlib.Path(os.environ.get("NMD_ASSEMBLER", ROOT / "build/toolchains/vasm/vasmm68k_mot")),
        pathlib.Path(os.environ.get("NMD_Z80_ASSEMBLER", ROOT / "build/toolchains/vasm/vasmz80_oldstyle")),
        pathlib.Path(os.environ.get("NMD_ORACLE_CORE", ROOT / "build/toolchains/blastem" /
                                    ("libblastem." + extension))),
        pathlib.Path(os.environ.get("NMD_TESTCART", ROOT / "build/null/tests/md_testcart")),
    )


def run_matrix():
    assembler, z80_assembler, core, testcart = _tool_paths()
    for tool in (assembler, z80_assembler, core, testcart):
        if not tool.is_file():
            raise RuntimeError("missing required tool or executable: " + tool.name)
    if WORK.exists():
        shutil.rmtree(WORK)
    WORK.mkdir(parents=True)

    previous = {name: os.environ.get(name) for name in
                ("NMD_ROM_WORKDIR", "NMD_ROM_RECORD_STREAMS")}
    os.environ["NMD_ROM_WORKDIR"] = str(WORK)
    os.environ["NMD_ROM_RECORD_STREAMS"] = "1"
    known = romrun._known_cases()
    results = {}
    try:
        for case in cases.all_cases():
            difference = romrun.run_one(case, assembler, z80_assembler, core, testcart)
            if case.name in known:
                if not difference:
                    raise RuntimeError("known case now matches; review rom/KNOWN: " + case.name)
                status = "known"
            else:
                if difference:
                    raise RuntimeError("unlisted case differs (%s): %s" % (case.name, difference))
                status = "match"
            results[case.name] = {
                "status": status,
                "outputs": snapshot_tree(WORK / case.name),
            }
            print("%-24s %s" % (case.name, status))
    finally:
        for name, value in previous.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
    return {"format": FORMAT, "cases": results}


def record(output):
    output = pathlib.Path(output)
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("golden output is not empty; compare the existing baseline")
    manifest = run_matrix()
    output.mkdir(parents=True, exist_ok=True)
    path = output / "manifest.json"
    temporary = output / "manifest.json.tmp"
    temporary.write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")
    temporary.replace(path)
    print("golden: recorded %d synthetic cases" % len(manifest["cases"]))


def compare(output):
    path = pathlib.Path(output) / "manifest.json"
    if not path.is_file():
        raise RuntimeError("golden manifest is missing: " + path.name)
    want = json.loads(path.read_text())
    got = run_matrix()
    differences = compare_manifests(want, got)
    for difference in differences:
        print("golden: differs: " + difference)
    count = len(set(want.get("cases", {})) | set(got["cases"]))
    print("golden: %d of %d synthetic cases differ" % (len(differences), count))
    return 1 if differences else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    record_parser = sub.add_parser("record", help="record the synthetic case baseline once")
    record_parser.add_argument("out")
    compare_parser = sub.add_parser("compare", help="compare all synthetic cases with the baseline")
    compare_parser.add_argument("out")
    args = parser.parse_args()
    if args.command == "record":
        record(args.out)
        return 0
    return compare(args.out)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        sys.exit("golden.py: " + str(exc))
