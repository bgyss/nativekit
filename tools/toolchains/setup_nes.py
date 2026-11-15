#!/usr/bin/env python3
"""Build an isolated, pinned Mesen core host; never build or launch its UI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CAPTURE = ROOT / "consoles/nes/tools/capture"
REFERENCE = json.loads((ROOT / "tools/toolchains/mesen.json").read_text())
PIN = REFERENCE["source_commit"]
URL = REFERENCE["source_url"]
PATCHES = [ROOT / name for name in REFERENCE["patches"]]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def check_sources(tree, recorded):
    for name, expected in recorded.items():
        path = tree / name
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f"refusing to build edited source: {path}")
    for path in tree.rglob("*"):
        if (path.is_file() and path.suffix.lower() in (".c", ".cpp", ".h", ".hpp", ".mm")
                and str(path.relative_to(tree)) not in recorded):
            raise ValueError(f"refusing to build added source: {path}")


def object_hashes(tree):
    return {str(p.relative_to(tree)): digest(p) for p in tree.rglob("*.o")}


def prepare_objects(tree):
    """Reuse objects only when a previous successful build attests their bytes."""
    record = tree / ".nativekit-objects.json"
    current = object_hashes(tree)
    previous = json.loads(record.read_text()) if record.exists() else {}
    if current != previous:
        for name in current:
            (tree / name).unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    local_source = ROOT.parent / "MesenCE"
    parser.add_argument("--source", default=os.getenv("NNES_MESEN_SOURCE",
                        str(local_source) if (local_source / ".git").exists() else URL),
                        help="MesenCE Git URL or local checkout (read-only source)")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    args = parser.parse_args()
    if platform.system() not in ("Darwin", "Linux"):
        parser.error("native capture toolchain currently supports macOS and Linux")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    tc = ROOT / "build/toolchains"
    checkout = tc / "src/mesen-source"
    inputs = {name: digest(ROOT / name) for name in
              REFERENCE["patches"] + REFERENCE["build_inputs"]}
    stamp = hashlib.sha256(json.dumps([PIN, inputs], sort_keys=True).encode()).hexdigest()
    source_stamp = hashlib.sha256(json.dumps([PIN, {str(p.relative_to(ROOT)): digest(p) for p in PATCHES}],
                                           sort_keys=True).encode()).hexdigest()
    tree = tc / "src" / ("mesen-capture-" + source_stamp[:20])
    checkout.parent.mkdir(parents=True, exist_ok=True)
    if not (checkout / ".git").exists():
        checked("git", "init", "-q", checkout)
    dirty = subprocess.check_output(["git", "-C", str(checkout), "status",
                                     "--porcelain"], text=True)
    if dirty:
        raise ValueError(f"refusing to use edited source checkout: {checkout}")
    exists = subprocess.run(["git", "-C", str(checkout), "cat-file", "-e", PIN],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if exists.returncode:
        checked("git", "-C", checkout, "fetch", "--depth=1", args.source, PIN)
    checked("git", "-C", checkout, "checkout", "-q", "--detach", PIN)
    if not tree.exists():
        tree.mkdir()
        try:
            with tempfile.TemporaryFile() as archive:
                checked("git", "-C", checkout, "archive", PIN, stdout=archive)
                archive.seek(0)
                checked("tar", "-xf", "-", "-C", tree, stdin=archive)
            for patch in PATCHES:
                with patch.open("rb") as source:
                    checked("patch", "-s", "-p1", cwd=tree, stdin=source)
            source_hashes = {str(p.relative_to(tree)): digest(p) for p in tree.rglob("*")
                             if p.is_file()}
            (tree / ".nativekit-sources.json").write_text(json.dumps(source_hashes, sort_keys=True))
        except BaseException:
            shutil.rmtree(tree)
            raise
    recorded = json.loads((tree / ".nativekit-sources.json").read_text())
    check_sources(tree, recorded)
    prepare_objects(tree)
    log = tc / "mesen-build.log"
    with log.open("w") as output:
        try:
            checked("make", "-f", "makefile", "-f", CAPTURE / "mesen.mk",
                    "nnes-headless", "LTO=false", "STATICLINK=false", "LINKOPTIONS=",
                    f"NNES_HOST={CAPTURE / 'mesen_host.cpp'}",
                    "NNES_LINK=" + ("-ldl" if platform.system() == "Linux" else ""),
                    "-j", args.jobs, cwd=tree, stdout=output, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError:
            print("\n".join(log.read_text().splitlines()[-30:]), file=sys.stderr)
            raise ValueError(f"Mesen build failed; see {log}")
    (tree / ".nativekit-objects.json").write_text(json.dumps(object_hashes(tree), sort_keys=True))
    install = tc / "mesen"
    install.mkdir(exist_ok=True)
    shutil.copy2(tree / "nnes-mesen", install / "nnes-mesen")
    identity = {"format": "nnes-mesen-toolchain", "version": 1,
                "source_url": URL, "source_commit": PIN, "inputs": inputs,
                "build_stamp": stamp, "executable_sha256": digest(install / "nnes-mesen"),
                "system": platform.system(), "machine": platform.machine(),
                "host_backends": [], "operation_types": True}
    (install / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")
    print(f"NES capture reference: {install / 'nnes-mesen'}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"tools:nes: {error}")
