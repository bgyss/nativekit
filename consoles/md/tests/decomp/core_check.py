#!/usr/bin/env python3
"""Run authored CPU fixtures through the actual pinned candidate core."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fixtures import build_m68k_fixture

sys.path.insert(0, str(root / "consoles/md/tools/decomp"))
import trace as nmdh_trace

RUNNER = root / "consoles/md/tools/decomp/run.py"
CORE = root / "build/toolchains/blastem/libblastem.dylib"
ASSEMBLER = root / "build/toolchains/vasm/vasmm68k_mot"


def assert_m68k_trace(events, expected):
    instructions = [event for event in events if event["kind"] == 1 and event["cpu"] == 1]
    edges = [event["payload"] for event in events if event["kind"] == 2]
    if not instructions:
        raise AssertionError("no 68000 instruction events")

    first = next((event for event in instructions if event["pc"] == expected["main_pc"]), None)
    if first is None:
        raise AssertionError("reset execution did not reach the fixture entry PC")
    payload = first["payload"]
    if (payload["fetched_length"] != 2 or payload["fetched_bytes"] != expected["first_bytes"] or
            first["next_pc"] != expected["main_pc"] + 6):
        raise AssertionError("first 68000 opcode word or observed PC transition is wrong")
    if payload["state"]["d"][0] != 0x11223344:
        raise AssertionError("68000 D0 snapshot after the immediate move is wrong")
    if any(payload["state"][flag] for flag in ("xflag", "nflag", "zflag", "vflag", "cflag")):
        raise AssertionError("68000 condition-code snapshot after the immediate move is wrong")

    for target in (expected["subroutine_pc"], expected["nested_pc"], expected["ram_pc"]):
        if not any(edge["current_pc"] == target for edge in edges):
            raise AssertionError("missing direct/computed flow edge to %06x" % target)

    subroutine = next((event for event in instructions if event["pc"] == expected["subroutine_pc"]), None)
    if subroutine is None or subroutine["payload"]["state"]["d"][0] != 0x11223345:
        raise AssertionError("nested call did not preserve the expected D0 state")
    nested = next((event for event in instructions if event["pc"] == expected["nested_pc"]), None)
    if nested is None or nested["payload"]["state"]["d"][0] != 0x11223347:
        raise AssertionError("nested routine state is wrong")

    ram_first = next((event for event in instructions if event["pc"] == expected["ram_pc"]), None)
    ram_next = next((event for event in instructions if event["pc"] == expected["ram_pc"] + 2), None)
    if ram_first is None or ram_next is None:
        raise AssertionError("copied work-RAM code did not execute")
    if ram_first["payload"]["fetched_bytes"] != expected["ram_bytes"][:2]:
        raise AssertionError("work-RAM opcode bytes do not match the copied image")
    if (ram_next["payload"]["fetched_bytes"] != expected["ram_bytes"][2:4] or
            ram_next["next_pc"] != expected["ram_pc"] + 8):
        raise AssertionError("work-RAM opcode word or observed PC transition is wrong")
    if ram_next["payload"]["state"]["d"][0] != 0x1122334A:
        raise AssertionError("work-RAM routine state is wrong")


def expect_bad_trace(events, expected, mutate, label):
    altered = copy.deepcopy(events)
    mutate(altered)
    try:
        assert_m68k_trace(altered, expected)
    except AssertionError:
        return
    raise AssertionError("planted " + label + " mismatch was accepted")


def run_m68k_case():
    if not CORE.is_file() or not ASSEMBLER.is_file():
        raise AssertionError("pinned test core and vasm must be built before the m68k case")
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        rom_path, scenario_path, expected = build_m68k_fixture(directory, ASSEMBLER)
        estimate_dir = directory / "estimate"
        command = [sys.executable, str(RUNNER), "--mode", "estimate", "--core", str(CORE),
                   "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "1",
                   "--out", str(estimate_dir)]
        estimate = subprocess.run(command, capture_output=True, text=True, timeout=450)
        if estimate.returncode:
            if "NMDH ABI" in estimate.stderr or "required capability" in estimate.stderr:
                raise AssertionError("baseline core lacks NMDH 68000 evidence: " + estimate.stderr.strip())
            raise AssertionError("preflight failed unexpectedly: " + estimate.stderr.strip())

        capture_dir = directory / "capture"
        command = [sys.executable, str(RUNNER), "--mode", "capture", "--core", str(CORE),
                   "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "1",
                   "--estimate-file", str(estimate_dir / "estimate.json"), "--out", str(capture_dir)]
        capture = subprocess.run(command, capture_output=True, text=True, timeout=450)
        if capture.returncode:
            raise AssertionError("candidate capture failed: " + capture.stderr.strip())
        events = list(nmdh_trace.read_nmdh(capture_dir / "capture.nmdh"))
        assert_m68k_trace(events, expected)
        manifest = json.loads((capture_dir / "manifest.json").read_text())
        serialized_manifest = json.dumps(manifest)
        if str(root) in serialized_manifest or str(directory) in serialized_manifest:
            raise AssertionError("path leaked into the synthetic capture manifest")

        first_index = next(index for index, event in enumerate(events)
                           if event["kind"] == 1 and event["cpu"] == 1 and
                           event["pc"] == expected["main_pc"])
        expect_bad_trace(events, expected,
                         lambda altered: altered[first_index].__setitem__("next_pc", 0x208),
                         "PC")
        expect_bad_trace(events, expected,
                         lambda altered: altered[first_index]["payload"]["state"]["d"].__setitem__(
                             0, 0x11223345), "register")
        expect_bad_trace(events, expected,
                         lambda altered: altered[first_index]["payload"]["state"].__setitem__(
                             "nflag", 1), "condition-code")
        expect_bad_trace(events, expected,
                         lambda altered: altered[first_index]["payload"].__setitem__(
                             "fetched_length", 4), "opcode extent")
        identities = manifest["metadata"]["identities"]
        print("m68k synthetic: code_bytes=%d frames=%d instructions=%d events=%d nmdt_records=%d rom_sha256=%s scenario_sha256=%s core_sha256=%s patch_sha256=%s abi_sha256=%s runner_sha256=%s" %
              (expected["m68k_code_bytes"], manifest["frames_run"],
               manifest["instruction_counts"]["m68k"], manifest["events"],
               manifest["nmdt_records"], identities["rom_sha256"],
               identities["scenario_sha256"], identities["core_sha256"],
               identities["patch_sha256"], identities["abi_sha256"],
               identities["runner_sha256"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=("m68k",), required=True)
    args = parser.parse_args()
    if args.case == "m68k":
        run_m68k_case()
    print("decomp core %s checks: ok" % args.case)


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, RuntimeError, ValueError) as exc:
        print("core_check: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
