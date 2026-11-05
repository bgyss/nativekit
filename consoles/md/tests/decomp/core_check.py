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
from fixtures import build_m68k_fixture, build_z80_fixture

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


def assert_z80_trace(events, expected):
    instructions = [event for event in events if event["kind"] == 1 and event["cpu"] == 2]
    edges = [event["payload"] for event in events if event["kind"] == 2]
    if not instructions:
        raise AssertionError("no Z80 instruction events")

    first = next((event for event in instructions if event["pc"] == expected["z80_entry_pc"]), None)
    if (first is None or first["payload"]["fetched_bytes"] != [0x31] or
            first["payload"]["fetched_length"] != 1 or first["next_pc"] != 3 or
            first["payload"]["state"]["sp"] != 0x1FF0):
        raise AssertionError("Z80 stack setup opcode/state at reset entry is wrong")

    immediate = next((event for event in instructions if event["pc"] == expected["z80_immediate_pc"]), None)
    if (immediate is None or immediate["payload"]["fetched_bytes"] != [0x3E] or
            immediate["next_pc"] != 5 or immediate["payload"]["state"]["main"][7] != 0x7F):
        raise AssertionError("Z80 immediate load opcode or A-register snapshot is wrong")

    wz_event = next((event for event in instructions
                     if event["pc"] == expected["wz_instruction_pc"]), None)
    if (wz_event is None or wz_event["payload"]["fetched_bytes"] != [0x32] or
            wz_event["next_pc"] != 8 or wz_event["payload"]["state"]["wz"] != 0x7F11):
        detail = None if wz_event is None else (
            wz_event["pc"], wz_event["next_pc"], wz_event["payload"]["fetched_bytes"],
            wz_event["payload"]["state"]["wz"])
        raise AssertionError("Z80 immediate store opcode, PC or WZ snapshot is wrong: " + repr(detail))

    alternate = next((event for event in instructions
                      if event["pc"] == expected["alternate_pc"]), None)
    if (alternate is None or alternate["payload"]["state"]["main"][0] != 0x02 or
            alternate["payload"]["state"]["alt"][0] != 0x5A):
        raise AssertionError("Z80 alternate-register state is wrong")

    prefix = next((event for event in instructions if event["pc"] == expected["prefix_pc"]), None)
    if (prefix is None or prefix["payload"]["fetched_length"] != 2 or
            prefix["payload"]["fetched_bytes"] != [0xDD, 0x21] or
            prefix["next_pc"] != expected["call_pc"] or
            prefix["payload"]["state"]["ix"] != 0x1234):
        raise AssertionError("Z80 prefix/opcode fetch sequence or IX state is wrong")

    xor_event = next((event for event in instructions if event["pc"] == expected["xor_pc"]), None)
    if (xor_event is None or xor_event["payload"]["state"]["main"][7] != 0 or
            xor_event["payload"]["state"]["zflag"] != 1):
        detail = None if xor_event is None else (
            xor_event["pc"], xor_event["next_pc"], xor_event["payload"]["state"])
        raise AssertionError("Z80 flag snapshot after XOR is wrong: " + repr(detail))

    ret_event = next((event for event in instructions if event["pc"] == expected["ret_pc"]), None)
    if ret_event is None or ret_event["next_pc"] != expected["return_pc"]:
        detail = None if ret_event is None else (ret_event["pc"], ret_event["next_pc"])
        raise AssertionError("Z80 RET did not return to the CALL continuation: " + repr(detail))
    if not all(any(edge["current_pc"] == target for edge in edges) for target in
               (expected["subroutine_pc"], expected["return_pc"],
                expected["branch_target_pc"], expected["loop_pc"])):
        raise AssertionError("Z80 direct, return or computed flow edge is missing: " +
                             repr([edge["current_pc"] for edge in edges]))
    if not any(event["kind"] == 1 and event["cpu"] == 1 for event in events):
        raise AssertionError("68000 did not remain active with the Z80 fixture")


def expect_bad_z80_trace(events, expected, mutate, label):
    altered = copy.deepcopy(events)
    mutate(altered)
    try:
        assert_z80_trace(altered, expected)
    except AssertionError:
        return
    raise AssertionError("planted Z80 " + label + " mismatch was accepted")


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


def run_z80_case():
    z80_assembler = root / "build/toolchains/vasm/vasmz80_oldstyle"
    if not CORE.is_file() or not ASSEMBLER.is_file() or not z80_assembler.is_file():
        raise AssertionError("pinned test core and both vasm assemblers must be built before the Z80 case")
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        rom_path, scenario_path, expected = build_z80_fixture(
            directory, ASSEMBLER, z80_assembler)
        estimate_dir = directory / "estimate-z80"
        command = [sys.executable, str(RUNNER), "--mode", "estimate", "--core", str(CORE),
                   "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "1",
                   "--out", str(estimate_dir)]
        estimate = subprocess.run(command, capture_output=True, text=True, timeout=450)
        if estimate.returncode:
            if "z80_instruction" in estimate.stderr or "required capability" in estimate.stderr:
                raise AssertionError("baseline core lacks Z80 instruction/state capability: " +
                                     estimate.stderr.strip())
            raise AssertionError("Z80 preflight failed unexpectedly: " + estimate.stderr.strip())

        capture_dir = directory / "capture-z80"
        command = [sys.executable, str(RUNNER), "--mode", "capture", "--core", str(CORE),
                   "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "1",
                   "--estimate-file", str(estimate_dir / "estimate.json"), "--out", str(capture_dir)]
        capture = subprocess.run(command, capture_output=True, text=True, timeout=450)
        if capture.returncode:
            raise AssertionError("Z80 candidate capture failed: " + capture.stderr.strip())
        events = list(nmdh_trace.read_nmdh(capture_dir / "capture.nmdh"))
        assert_z80_trace(events, expected)
        manifest = json.loads((capture_dir / "manifest.json").read_text())
        serialized_manifest = json.dumps(manifest)
        if str(root) in serialized_manifest or str(directory) in serialized_manifest:
            raise AssertionError("path leaked into the Z80 synthetic capture manifest")

        first_index = next(index for index, event in enumerate(events)
                           if event["kind"] == 1 and event["cpu"] == 2 and
                           event["pc"] == expected["z80_entry_pc"])
        wz_index = next(index for index, event in enumerate(events)
                        if event["kind"] == 1 and event["cpu"] == 2 and
                        event["pc"] == expected["wz_instruction_pc"])
        xor_index = next(index for index, event in enumerate(events)
                         if event["kind"] == 1 and event["cpu"] == 2 and
                         event["pc"] == expected["xor_pc"])
        expect_bad_z80_trace(events, expected,
                             lambda altered: altered[first_index].__setitem__("next_pc", 0x04),
                             "PC")
        expect_bad_z80_trace(events, expected,
                             lambda altered: altered[wz_index]["payload"]["state"].__setitem__(
                                 "wz", 0x7F12), "WZ")
        expect_bad_z80_trace(events, expected,
                             lambda altered: altered[xor_index]["payload"]["state"].__setitem__(
                                 "zflag", 0), "flag")
        identities = manifest["metadata"]["identities"]
        print("z80 synthetic: m68k_code_bytes=%d z80_code_bytes=%d frames=%d m68k_instructions=%d z80_instructions=%d events=%d nmdt_records=%d rom_sha256=%s scenario_sha256=%s core_sha256=%s patch_sha256=%s abi_sha256=%s runner_sha256=%s" %
              (expected["m68k_code_bytes"], expected["z80_code_bytes"],
               manifest["frames_run"], manifest["instruction_counts"]["m68k"],
               manifest["instruction_counts"]["z80"], manifest["events"],
               manifest["nmdt_records"], identities["rom_sha256"],
               identities["scenario_sha256"], identities["core_sha256"],
               identities["patch_sha256"], identities["abi_sha256"],
               identities["runner_sha256"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=("m68k", "z80"), required=True)
    args = parser.parse_args()
    if args.case == "m68k":
        run_m68k_case()
    elif args.case == "z80":
        run_z80_case()
    print("decomp core %s checks: ok" % args.case)


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, RuntimeError, ValueError) as exc:
        print("core_check: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
