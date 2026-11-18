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
from fixtures import build_bus_fixture, build_handoff_fixture, build_m68k_fixture, build_z80_fixture

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
        if (type(manifest.get("peak_worker_rss_bytes")) is not int or
                manifest["peak_worker_rss_bytes"] <= 0):
            raise AssertionError("capture manifest omitted positive peak worker RSS")
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


def _handoff_scenario(stop):
    return {
        "version": 1,
        "region": "U",
        "model": "md1va3",
        "pad1": "3",
        "pad2": "3",
        "input": {},
        "required_capabilities": [
            "m68k_instruction", "m68k_state", "z80_instruction", "z80_state",
            "flow_edge", "reset_exception_interrupt", "fetch_provenance",
            "master_clock", "controlled_stop", "replace_handoff",
        ],
        "stop": stop,
    }


def _capture_handoff_case(directory, rom_path, stop, label):
    scenario_path = directory / (label + ".json")
    scenario_path.write_text(json.dumps(_handoff_scenario(stop), sort_keys=True,
                                        separators=(",", ":")))
    estimate_dir = directory / (label + "-estimate")
    estimate = subprocess.run([
        sys.executable, str(RUNNER), "--mode", "estimate", "--core", str(CORE),
        "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "1",
        "--out", str(estimate_dir),
    ], capture_output=True, text=True, timeout=450)
    if estimate.returncode:
        raise AssertionError("%s estimate failed: %s" % (label, estimate.stderr.strip()))

    capture_dir = directory / (label + "-capture")
    capture = subprocess.run([
        sys.executable, str(RUNNER), "--mode", "capture", "--core", str(CORE),
        "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "1",
        "--estimate-file", str(estimate_dir / "estimate.json"), "--out", str(capture_dir),
    ], capture_output=True, text=True, timeout=450)
    if capture.returncode:
        raise AssertionError("%s capture failed: %s" % (label, capture.stderr.strip()))
    events = list(nmdh_trace.read_nmdh(capture_dir / "capture.nmdh"))
    manifest = json.loads((capture_dir / "manifest.json").read_text())
    return events, manifest


def _assert_handoff(events, manifest, action, cpu, criterion, target=None):
    handoffs = [event for event in events if event["kind"] in (7, 8)]
    if len(handoffs) != 1:
        raise AssertionError("expected exactly one synchronized stop/replacement event")
    event = handoffs[0]
    expected_kind = 8 if action == "replace" else 7
    if event["kind"] != expected_kind or event["cpu"] != (1 if cpu == "m68k" else 2):
        raise AssertionError("handoff event does not match requested CPU/action")
    payload = event["payload"]
    if (payload["criterion"] != criterion or payload["handoff"]["master_clock"] != event["master_clock"] or
            payload["handoff"]["stop_clock"] != event["master_clock"]):
        raise AssertionError("handoff reason or master-clock snapshot is wrong")
    if payload["handoff"]["m68k"]["cycles"] > event["master_clock"]:
        raise AssertionError("68000 snapshot is ahead of the synchronized clock")
    if payload["handoff"]["z80"]["cycles"] > event["master_clock"]:
        raise AssertionError("Z80 snapshot is ahead of the synchronized clock")
    if target is not None and manifest["trigger_event"]["next_pc"] != target:
        raise AssertionError("stop trigger did not match the requested post-instruction boundary")
    trigger = manifest["trigger_event"]
    cpu_id = 1 if cpu == "m68k" else 2
    later = [item for item in events if item["sequence"] > trigger["sequence"] and
             item["kind"] == 1 and item["cpu"] == cpu_id]
    if later:
        raise AssertionError("the stopped CPU executed another guest instruction")
    if not manifest["instruction_counts"]["m68k"] or not manifest["instruction_counts"]["z80"]:
        raise AssertionError("combined-CPU handoff did not advance both CPUs: " +
                             repr(manifest["instruction_counts"]))
    snapshots = payload["handoff"]
    clocks = (snapshots["master_clock"], snapshots["vdp_clock"], snapshots["ym_clock"],
              snapshots["psg_clock"], snapshots["io_clock"])
    if any(clock != snapshots["master_clock"] for clock in clocks[1:]):
        raise AssertionError("device and scheduler clocks are not synchronized: " + repr(clocks))
    return event, trigger


def run_handoff_case():
    if not CORE.is_file() or not ASSEMBLER.is_file():
        raise AssertionError("pinned test core and vasm must be built before the handoff case")
    z80_assembler = root / "build/toolchains/vasm/vasmz80_oldstyle"
    if not z80_assembler.is_file():
        raise AssertionError("pinned Z80 vasm must be built before the handoff case")
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        rom_path, expected = build_handoff_fixture(directory, ASSEMBLER, z80_assembler)

        pc_stop = {"cpu": "m68k", "criterion": "pc", "action": "stop",
                   "value": expected["m68k_stop_pc"]}
        stop_events, stop_manifest = _capture_handoff_case(
            directory, rom_path, pc_stop, "handoff-stop")
        stop_event, stop_trigger = _assert_handoff(
            stop_events, stop_manifest, "stop", "m68k", "pc", expected["m68k_stop_pc"])

        replace_stop = dict(pc_stop, action="replace")
        replace_events, replace_manifest = _capture_handoff_case(
            directory, rom_path, replace_stop, "handoff-replace")
        replace_event, replace_trigger = _assert_handoff(
            replace_events, replace_manifest, "replace", "m68k", "pc", expected["m68k_stop_pc"])
        if stop_trigger != replace_trigger:
            raise AssertionError("STOP and REPLACE did not preserve the same immutable trigger event")
        if stop_event["payload"]["handoff"] != replace_event["payload"]["handoff"]:
            raise AssertionError("STOP and REPLACE mutated the same decision-boundary snapshot differently")

        return_stop = {"cpu": "z80", "criterion": "return_sentinel", "action": "stop",
                       "value": expected["z80_return_pc"]}
        return_events, return_manifest = _capture_handoff_case(
            directory, rom_path, return_stop, "handoff-return")
        _assert_handoff(return_events, return_manifest, "stop", "z80", "return_sentinel",
                        expected["z80_return_pc"])

        budget_stop = {"cpu": "z80", "criterion": "instruction_budget", "action": "stop",
                       "value": 20}
        budget_events, budget_manifest = _capture_handoff_case(
            directory, rom_path, budget_stop, "handoff-budget")
        _assert_handoff(budget_events, budget_manifest, "stop", "z80", "instruction_budget")

        irq_stop = {"cpu": "m68k", "criterion": "interrupt", "action": "stop"}
        irq_events, irq_manifest = _capture_handoff_case(
            directory, rom_path, irq_stop, "handoff-interrupt")
        _assert_handoff(irq_events, irq_manifest, "stop", "m68k", "interrupt")

        frame_stop = {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"}
        frame_events, frame_manifest = _capture_handoff_case(
            directory, rom_path, frame_stop, "handoff-frame")
        if frame_manifest["frames_run"] != 1 or frame_manifest["stop_criterion"] != "frame_limit":
            raise AssertionError("frame-limit capture did not stop at its requested frame boundary")
        for cpu, kind in ((1, 4), (2, 4), (1, 5), (1, 6), (2, 6)):
            if not any(event["kind"] == kind and event["cpu"] == cpu for event in frame_events):
                raise AssertionError("missing reset, exception or CPU interrupt event %d/%d" % (kind, cpu))
        for cpu in (1, 2):
            phases = {event["payload"]["phase"] for event in frame_events
                      if event["kind"] == 6 and event["cpu"] == cpu}
            if not {1, 2}.issubset(phases):
                raise AssertionError("CPU %d IRQ/NMI entry and return events are incomplete" % cpu)

        identities = stop_manifest["metadata"]["identities"]
        print("handoff synthetic: m68k_code_bytes=%d z80_code_bytes=%d stop_clock=%d events=%d rom_sha256=%s scenario_sha256=%s core_sha256=%s patch_sha256=%s abi_sha256=%s runner_sha256=%s" %
              (expected["m68k_code_bytes"], expected["z80_code_bytes"],
               stop_event["master_clock"], len(stop_events),
               identities["rom_sha256"], identities["scenario_sha256"],
               identities["core_sha256"], identities["patch_sha256"],
               identities["abi_sha256"], identities["runner_sha256"]))


def assert_bus_trace(events, expected):
    accesses = [event for event in events if event["kind"] == 3]
    if not accesses:
        raise AssertionError("no NMDH bus interval events")

    previous_epoch = 0
    previous_sequence = 0
    last_clock_by_cpu = {1: 0, 2: 0}
    for event in events:
        if event["sequence"] != previous_sequence + 1:
            raise AssertionError("NMDH event ordering is not contiguous")
        previous_sequence = event["sequence"]
        if event["master_clock"] < last_clock_by_cpu[event["cpu"]]:
            raise AssertionError("NMDH per-CPU master clock regressed: " +
                                 repr((event["cpu"], last_clock_by_cpu[event["cpu"]],
                                       event["master_clock"], event["epoch"])))
        if event["epoch"] < previous_epoch:
            raise AssertionError("NMDH master-clock epoch regressed")
        last_clock_by_cpu[event["cpu"]] = event["master_clock"]
        previous_epoch = event["epoch"]
        if event["kind"] == 3:
            bus = event["payload"]
            if bus["start_clock"] > bus["completion_clock"]:
                raise AssertionError("bus interval starts after completion")
            if event["master_clock"] != bus["completion_clock"]:
                raise AssertionError("BUS_ACCESS event clock differs from completion clock")

    def has(cpu, address, width, operation, provenance=None):
        return any(
            event["cpu"] == cpu and event["payload"]["address"] == address and
            event["payload"]["width"] == width and
            event["payload"]["operation"] == operation and
            (provenance is None or event["payload"]["provenance"] == provenance)
            for event in accesses)

    ram = expected["m68k_ram"]
    for address, width in ((ram + 1, 1), (ram + 2, 2), (ram + 4, 2), (ram + 6, 2)):
        if not has(1, address, width, 2):
            raise AssertionError("missing 68000 byte/word/long write interval at %08x" % address)
    for address, width in ((ram + 1, 1), (ram + 2, 2), (ram + 4, 2), (ram + 6, 2)):
        if not has(1, address, width, 1):
            raise AssertionError("missing 68000 byte/word/long read interval at %08x" % address)
    if not has(1, expected["main_pc"], 2, 1, 1):
        raise AssertionError("68000 opcode fetch provenance is missing")
    if not any(event["cpu"] == 1 and event["payload"]["provenance"] in (2, 3)
               for event in accesses):
        raise AssertionError("68000 operand/data provenance is missing")
    if not has(2, expected["z80_ram"], 1, 2):
        raise AssertionError("Z80 RAM write interval is missing")
    if not has(2, expected["z80_ram"], 1, 1):
        raise AssertionError("Z80 RAM read interval is missing")
    if not has(2, expected["z80_io"], 1, 2) or not has(2, expected["z80_io"], 1, 1):
        raise AssertionError("Z80 I/O read/write interval is missing")

    waits = [event for event in accesses if event["cpu"] == 1 and
             event["payload"]["address"] == expected["vdp_data"] and
             event["payload"]["operation"] == 2 and
             event["payload"]["completion_clock"] - event["payload"]["start_clock"] > 28]
    if not waits:
        raise AssertionError("fixture did not observe a 68000 VDP wait-state interval")
    if not any(event["epoch"] > 0 for event in events):
        raise AssertionError("forced counter rebase did not advance the NMDH clock epoch")


def expect_bad_bus_trace(events, expected, mutate, label):
    altered = copy.deepcopy(events)
    mutate(altered)
    try:
        assert_bus_trace(altered, expected)
    except AssertionError:
        return
    raise AssertionError("planted bus " + label + " mismatch was accepted")


def run_bus_case():
    if not CORE.is_file() or not ASSEMBLER.is_file():
        raise AssertionError("pinned test core and vasm must be built before the bus case")
    z80_assembler = root / "build/toolchains/vasm/vasmz80_oldstyle"
    if not z80_assembler.is_file():
        raise AssertionError("pinned Z80 vasm must be built before the bus case")
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        rom_path, scenario_path, expected = build_bus_fixture(
            directory, ASSEMBLER, z80_assembler)
        estimate_dir = directory / "bus-estimate"
        estimate = subprocess.run([
            sys.executable, str(RUNNER), "--mode", "estimate", "--core", str(CORE),
            "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "2",
            "--out", str(estimate_dir),
        ], capture_output=True, text=True, timeout=450)
        if estimate.returncode:
            raise AssertionError("bus preflight failed: " + estimate.stderr.strip())
        capture_dir = directory / "bus-capture"
        capture = subprocess.run([
            sys.executable, str(RUNNER), "--mode", "capture", "--core", str(CORE),
            "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "2",
            "--estimate-file", str(estimate_dir / "estimate.json"), "--out", str(capture_dir),
        ], capture_output=True, text=True, timeout=450)
        if capture.returncode:
            raise AssertionError("bus candidate capture failed: " + capture.stderr.strip())
        events = list(nmdh_trace.read_nmdh(capture_dir / "capture.nmdh"))
        manifest = json.loads((capture_dir / "manifest.json").read_text())
        assert_bus_trace(events, expected)

        accesses = [index for index, event in enumerate(events) if event["kind"] == 3]
        first_bus = accesses[0]
        expect_bad_bus_trace(events, expected,
                             lambda altered: altered[first_bus]["payload"].__setitem__(
                                 "start_clock", altered[first_bus]["payload"]["completion_clock"] + 1),
                             "interval")
        completion_index = next(index for index, event in enumerate(events)
                                if event["kind"] == 3 and
                                event["payload"]["start_clock"] > 0)
        expect_bad_bus_trace(events, expected,
                             lambda altered: altered[completion_index]["payload"].__setitem__(
                                 "completion_clock",
                                 altered[completion_index]["payload"]["start_clock"] - 1),
                             "completion")
        epoch_index = next(index for index, event in enumerate(events)
                           if event["epoch"] > 0) + 1
        expect_bad_bus_trace(events, expected,
                             lambda altered: altered[epoch_index].__setitem__("epoch", 0),
                             "epoch")
        swapped = copy.deepcopy(events)
        swapped[first_bus], swapped[first_bus + 1] = swapped[first_bus + 1], swapped[first_bus]
        try:
            assert_bus_trace(swapped, expected)
        except AssertionError:
            pass
        else:
            raise AssertionError("planted bus event-order mismatch was accepted")
        identities = manifest["metadata"]["identities"]
        print("bus synthetic: frames=%d events=%d intervals=%d epochs=%s rom_sha256=%s scenario_sha256=%s core_sha256=%s patch_sha256=%s abi_sha256=%s runner_sha256=%s" %
              (manifest["frames_run"], len(events), len(accesses),
               sorted({event["epoch"] for event in events}),
               identities["rom_sha256"], identities["scenario_sha256"],
               identities["core_sha256"], identities["patch_sha256"],
               identities["abi_sha256"], identities["runner_sha256"]))


def _checkpoint_scenario(stop, boundary):
    required = [
        "m68k_instruction", "m68k_state", "z80_instruction", "z80_state",
        "flow_edge", "reset_exception_interrupt", "fetch_provenance",
        "master_clock", "sync_checkpoint",
    ]
    if boundary == "stop":
        required.append("controlled_stop")
    checkpoint_test = {"boundary": boundary, "continuation_frames": 1}
    if boundary == "post_frame":
        checkpoint_test["frame"] = 1
    return {
        "version": 1, "region": "U", "model": "md1va3", "pad1": "3", "pad2": "3",
        "input": {}, "required_capabilities": required, "stop": stop,
        "checkpoint_test": checkpoint_test,
    }


def _run_checkpoint_replay(directory, rom_path, stop, boundary, label):
    scenario = _checkpoint_scenario(stop, boundary)
    scenario_path = directory / (label + ".json")
    scenario_path.write_text(json.dumps(scenario, sort_keys=True, separators=(",", ":")))
    estimate_dir = directory / (label + "-estimate")
    estimate = subprocess.run([
        sys.executable, str(RUNNER), "--mode", "estimate", "--core", str(CORE),
        "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "2",
        "--out", str(estimate_dir),
    ], capture_output=True, text=True, timeout=450)
    if estimate.returncode:
        raise AssertionError("%s estimate failed: %s" % (label, estimate.stderr.strip()))
    capture_dir = directory / (label + "-capture")
    capture = subprocess.run([
        sys.executable, str(RUNNER), "--mode", "capture", "--core", str(CORE),
        "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "2",
        "--estimate-file", str(estimate_dir / "estimate.json"), "--out", str(capture_dir),
    ], capture_output=True, text=True, timeout=450)
    if capture.returncode:
        raise AssertionError("%s checkpoint replay failed: %s" % (label, capture.stderr.strip()))
    return json.loads((capture_dir / "manifest.json").read_text())


def run_checkpoint_case():
    if not CORE.is_file() or not ASSEMBLER.is_file():
        raise AssertionError("pinned test core and vasm must be built before the checkpoint case")
    z80_assembler = root / "build/toolchains/vasm/vasmz80_oldstyle"
    if not z80_assembler.is_file():
        raise AssertionError("pinned Z80 vasm must be built before the checkpoint case")
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        rom_path, _, expected = build_z80_fixture(directory, ASSEMBLER, z80_assembler)
        post_frame = _run_checkpoint_replay(
            directory, rom_path,
            {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
            "post_frame", "checkpoint-frame")
        stop = _run_checkpoint_replay(
            directory, rom_path,
            {"cpu": "z80", "criterion": "pc", "action": "stop",
             "value": expected["wz_instruction_pc"] + 3},
            "stop", "checkpoint-handoff")
        for label, manifest in (("post-frame", post_frame), ("handoff", stop)):
            replay = manifest.get("checkpoint_replay")
            if not isinstance(replay, dict) or replay.get("status") != "ok":
                raise AssertionError("%s checkpoint replay report is missing or incomplete" % label)
            for field in ("nmdh_equal", "nmdt_equal", "cpu_snapshots_equal",
                          "video_equal", "audio_equal", "final_state_equal"):
                if replay.get(field) is not True:
                    raise AssertionError("%s checkpoint replay mismatch: %s" % (label, field))
        if stop["checkpoint_replay"]["checkpoint_snapshot"]["z80"]["wz"] != 0x7F11:
            raise AssertionError("synchronized checkpoint omitted the Z80 WZ value")
        for label, manifest in (("post-frame", post_frame), ("handoff", stop)):
            identities = manifest["metadata"]["identities"]
            print("checkpoint %s: rom_sha256=%s scenario_sha256=%s core_sha256=%s patch_sha256=%s abi_sha256=%s runner_sha256=%s state_sha256=%s" %
                  (label, identities["rom_sha256"], identities["scenario_sha256"],
                   identities["core_sha256"], identities["patch_sha256"],
                   identities["abi_sha256"], identities["runner_sha256"],
                   manifest["checkpoint_replay"]["checkpoint_state_sha256"]))
        print("checkpoint synthetic: post-frame and synchronized-stop replay equal")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=("m68k", "z80", "handoff", "bus", "checkpoint"), required=True)
    args = parser.parse_args()
    if args.case == "m68k":
        run_m68k_case()
    elif args.case == "z80":
        run_z80_case()
    elif args.case == "handoff":
        run_handoff_case()
    elif args.case == "bus":
        run_bus_case()
    elif args.case == "checkpoint":
        run_checkpoint_case()
    print("decomp core %s checks: ok" % args.case)


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, RuntimeError, ValueError) as exc:
        print("core_check: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
