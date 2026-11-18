#!/usr/bin/env python3
"""Build a game's C routines into the per-call checker's hybrid library.

The library holds the C as written for a native cart, linked with
`check/replay.c` in place of the nmd runtime: the bus calls and CPU state are
served from one recorded call of the original (see `check_calls.py`). The C
exports its routine tables under fixed names:

    const nmd_routine      nmd_game_routines[];      const size_t nmd_game_routine_count;
    const nmd_z80_routine  nmd_game_z80_routines[];  const size_t nmd_game_z80_routine_count;

Each table address must be a routine entry of the intake's catalog for that
CPU when a catalog is given. Nothing of the library is part of a game.
"""
import argparse
import ctypes
import hashlib
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
TOOLS = pathlib.Path(__file__).resolve().parent
INCLUDES = (ROOT / "consoles/md/include", TOOLS / "include", TOOLS / "check")
RUNTIMES = {"replay": TOOLS / "check" / "replay.c",     # one recorded call (check_calls.py)
            "live": TOOLS / "check" / "live.c"}         # the reference core's machine (lockstep.py)
REPLAY = RUNTIMES["replay"]
SUFFIX = ".dylib" if sys.platform == "darwin" else ".so"
LIBRARY = "hybrid" + SUFFIX
LIVE_LIBRARY = "hybrid-live" + SUFFIX
MANIFEST = "hybrid.json"


class HybridError(RuntimeError):
    """The hybrid library cannot be built or does not match the catalog."""


class Routine(ctypes.Structure):
    _fields_ = [("addr", ctypes.c_uint32), ("abi", ctypes.c_void_p), ("name", ctypes.c_char_p)]


class Z80Routine(ctypes.Structure):
    _fields_ = [("addr", ctypes.c_uint16), ("abi", ctypes.c_void_p), ("name", ctypes.c_char_p)]


def _sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def _compile(arguments):
    result = subprocess.run(arguments, stdin=subprocess.DEVNULL, capture_output=True, text=True)
    if result.returncode:
        raise HybridError("hybrid: %s failed:\n%s" % (pathlib.Path(arguments[-1]).name,
                                                    result.stderr.strip()))


def tables(library):
    """The loaded library's routine tables: [(cpu, address, function pointer, name)]."""
    found = []
    for cpu, symbol, kind in (("m68k", "nmd_game_routines", Routine),
                              ("z80", "nmd_game_z80_routines", Z80Routine)):
        try:
            count = ctypes.c_size_t.in_dll(library, symbol[:-1] + "_count").value
            table = (kind * count).in_dll(library, symbol)
        except ValueError as exc:
            raise HybridError("hybrid: the C does not export %s" % symbol) from exc
        for row in table:
            found.append((cpu, row.addr, row.abi, (row.name or b"").decode("ascii", "replace")))
    addresses = [(cpu, address) for cpu, address, _, _ in found]
    if len(set(addresses)) != len(addresses):
        raise HybridError("hybrid: a routine address is in a table twice")
    return found


def build(sources, out, catalog=None, defines=(), runtime="replay"):
    """Compile `sources` with the replay or the live runtime; return the library path."""
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get("CC", "cc")
    flags = ["-std=c11", "-O1", "-g", "-fPIC", "-Wall", "-Wextra", "-Werror",
             "-Wno-deprecated-declarations", "-DNMD_HYBRID"]
    flags += ["-I" + str(path) for path in INCLUDES] + ["-D" + name for name in defines]
    objects = []
    for index, source in enumerate(sources):
        target = out / ("game%d.o" % index)
        # every routine entry is reported to the checker: -finstrument-functions
        live = ["-DNMD_LIVE_AT"] if runtime == "live" else []
        _compile([compiler, *flags, *live, "-finstrument-functions", "-c", "-o", str(target),
                  str(source)])
        objects.append(target)
    replay = out / (runtime + ".o")
    _compile([compiler, *flags, "-c", "-o", str(replay), str(RUNTIMES[runtime])])
    library = out / (LIBRARY if runtime == "replay" else LIVE_LIBRARY)
    _compile([compiler, "-shared", "-o", str(library), *map(str, objects), str(replay)])
    found = tables(ctypes.CDLL(str(library)))
    if catalog is not None:
        entries = {(routine["cpu"], routine["at"]) for routine in catalog["routines"]}
        for cpu, address, _, name in found:
            if (cpu, address) not in entries:
                raise HybridError("hybrid: %s $%06X (%s) is not a catalog entry"
                                  % (cpu, address, name))
    manifest = {
        "schema": "nmd-hybrid", "version": 1, "library_sha256": _sha256(library),
        "sources": [{"name": pathlib.Path(source).name, "sha256": _sha256(source)}
                    for source in sources],
        "runtime": runtime, "replay_sha256": _sha256(RUNTIMES[runtime]),
        "defines": sorted(defines),
        "catalog_checked": catalog is not None,
        "routines": [{"cpu": cpu, "address": address, "name": name}
                     for cpu, address, _, name in found],
    }
    (out / (MANIFEST if runtime == "replay" else "hybrid-live.json")).write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")
    return library


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", required=True)
    parser.add_argument("--catalog", help="the intake's catalog.json")
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--runtime", choices=sorted(RUNTIMES), default="replay")
    parser.add_argument("source", nargs="+")
    args = parser.parse_args()
    catalog = json.loads(pathlib.Path(args.catalog).read_text()) if args.catalog else None
    try:
        library = build(args.source, args.out, catalog, args.define, args.runtime)
    except HybridError as exc:
        sys.exit(str(exc))
    print("hybrid: %s, %d routines" % (library, len(tables(ctypes.CDLL(str(library))))))


if __name__ == "__main__":
    main()
