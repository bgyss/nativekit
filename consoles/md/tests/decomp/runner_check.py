#!/usr/bin/env python3
"""Check runner input rejection, capability gating and child timeouts."""
import argparse
import importlib.util
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[4]
runner_path = root / "consoles/md/tools/decomp/run.py"
if not runner_path.is_file():
    raise SystemExit("runner_check: NMDH runner is not implemented")
spec = importlib.util.spec_from_file_location("nmdh_run", runner_path)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
compiler = shlex.split(os.environ.get("CC", "/usr/bin/clang" if sys.platform == "darwin" else "cc"))
link_flag = "-dynamiclib" if sys.platform == "darwin" else "-shared"
extension = "dylib" if sys.platform == "darwin" else "so"


def build_core(directory, source, name):
    c_path = directory / (name + ".c")
    library = directory / (name + "." + extension)
    c_path.write_text(source)
    subprocess.run([*compiler, "-fPIC", link_flag, str(c_path), "-o", str(library)],
                   check=True, capture_output=True, text=True)
    return library


with tempfile.TemporaryDirectory() as temporary:
    base = pathlib.Path(temporary)
    missing_abi = build_core(base, """\
        #include <stdint.h>
        uint32_t retro_api_version(void) { return 1; }
    """, "missing-abi")
    no_caps = build_core(base, """\
        #include <stdint.h>
        uint32_t nmd_decomp_abi_version(void) { return 1; }
        uint64_t nmd_decomp_capabilities(void) { return 0; }
        int nmd_decomp_register(uint32_t version, void *callback, void *user) {
            (void)version; (void)callback; (void)user; return 1;
        }
        void nmd_decomp_reset(void) { }
        int nmd_decomp_error(void) { return 0; }
    """, "no-caps")
    capable_no_libretro = build_core(base, """\
        #include <stdint.h>
        uint32_t nmd_decomp_abi_version(void) { return 1; }
        uint64_t nmd_decomp_capabilities(void) { return 3; }
        int nmd_decomp_register(uint32_t version, void *callback, void *user) {
            (void)version; (void)callback; (void)user; return 1;
        }
        void nmd_decomp_reset(void) { }
        int nmd_decomp_error(void) { return 0; }
        uint32_t nmd_trace_version(void) { return 2; }
        uint64_t nmd_trace_capabilities(void) { return 1; }
        int nmd_trace_register(uint32_t version, void *callback, void *user) {
            (void)version; (void)callback; (void)user; return 1;
        }
        void nmd_trace_reset(void) { }
        int nmd_trace_error(void) { return 0; }
    """, "capable-no-libretro")
    instruction_only = build_core(
        base, (base / "capable-no-libretro.c").read_text().replace("return 3;", "return 1;"),
        "instruction-only")
    rom = base / "fixture.bin"
    rom.write_bytes(b"synthetic only")
    scenario = base / "scenario.json"
    scenario.write_text(json.dumps({
        "version": 1,
        "region": "U",
        "model": "md1va3",
        "pad1": "3",
        "pad2": "3",
        "input": {},
        "required_capabilities": ["m68k_instruction", "m68k_state"],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
    }))

    for core, marker in ((missing_abi, "NMDH ABI"), (no_caps, "required capability")):
        output = base / (core.stem + "-out")
        result = subprocess.run([
            sys.executable, str(runner_path), "--mode", "estimate", "--core", str(core),
            "--rom", str(rom), "--scenario", str(scenario), "--frames", "3", "--out", str(output),
        ], capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and marker.lower() in result.stderr.lower(), result.stderr
        assert not output.exists()

    checkpoint_scenario = {
        "stop": {"criterion": "frame_limit"},
        "checkpoint_test": {},
        "required_capabilities": ["sync_checkpoint"],
    }
    try:
        runner._preflight_core(no_caps, checkpoint_scenario)
    except runner.RunnerError as exc:
        assert "sync_checkpoint" in str(exc), str(exc)
    else:
        raise AssertionError("checkpoint capability gap was hidden by absent extension symbols")

    state_output = base / "missing-state-capability"
    result = subprocess.run([
        sys.executable, str(runner_path), "--mode", "estimate", "--core", str(instruction_only),
        "--rom", str(rom), "--scenario", str(scenario), "--frames", "3", "--out", str(state_output),
    ], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "required capability" in result.stderr.lower()
    assert not state_output.exists()

    occupied = base / "occupied"
    occupied.mkdir()
    sentinel = occupied / "keep.txt"
    sentinel.write_text("keep")
    result = subprocess.run([
        sys.executable, str(runner_path), "--mode", "estimate", "--core", str(missing_abi),
        "--rom", str(rom), "--scenario", str(scenario), "--frames", "3", "--out", str(occupied),
    ], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "output" in result.stderr.lower()
    assert sentinel.read_text() == "keep"

    long_scenario = base / "long-input-scenario.json"
    long_scenario.write_text(json.dumps({
        "version": 1,
        "region": "U",
        "model": "md1va3",
        "pad1": "3",
        "pad2": "3",
        "input": {"10": {"1": ["START"]}},
        "required_capabilities": ["m68k_instruction", "m68k_state"],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
    }))
    long_out = base / "long-input-estimate"
    result = subprocess.run([
        sys.executable, str(runner_path), "--mode", "estimate", "--core", str(capable_no_libretro),
        "--rom", str(rom), "--scenario", str(long_scenario), "--frames", "10", "--out", str(long_out),
    ], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "libretro" in result.stderr.lower()
    assert "scenario input frame" not in result.stderr.lower()

    matching_scenario = json.loads(scenario.read_text())
    matching_metadata = runner._make_identity(capable_no_libretro, rom, matching_scenario, 3)
    checkpoint = runner.checkpoint_envelope(b"synthetic-state", matching_metadata, "post_frame")
    assert runner.validate_checkpoint_envelope(checkpoint, matching_metadata) == b"synthetic-state"
    bad_digest = {**checkpoint, "state": b"changed"}
    try:
        runner.validate_checkpoint_envelope(bad_digest, matching_metadata)
    except runner.RunnerError as exc:
        assert "digest" in str(exc)
    else:
        raise AssertionError("corrupted checkpoint state bytes were accepted")
    wrong_identity = json.loads(json.dumps(matching_metadata))
    wrong_identity["identities"]["patch_sha256"] = "0" * 64
    try:
        runner.validate_checkpoint_envelope(
            {**checkpoint, "metadata": wrong_identity}, matching_metadata)
    except runner.nmdh_trace.NMDHError:
        pass
    else:
        raise AssertionError("checkpoint with a mismatched patch identity was accepted")
    estimate_file = base / "matching-estimate.json"
    estimate_file.write_text(json.dumps({
        "status": "complete", "metadata": matching_metadata, "frames": 3,
        "estimated_seconds": 0.0, "estimated_bytes": 0,
    }))
    capture_out = base / "capture-with-estimate"
    result = subprocess.run([
        sys.executable, str(runner_path), "--mode", "capture", "--core", str(capable_no_libretro),
        "--rom", str(rom), "--scenario", str(scenario), "--frames", "3",
        "--estimate-file", str(estimate_file), "--out", str(capture_out),
    ], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "libretro" in result.stderr.lower(), result.stderr
    assert not (capture_out / "capture.nmdh").exists()

    # the total fits --max-bytes but the NMDH stream alone exceeds the half it is given
    split_estimate_file = base / "split-estimate.json"
    split_estimate_file.write_text(json.dumps({
        "status": "complete", "metadata": matching_metadata, "frames": 3,
        "estimated_seconds": 0.0, "estimated_bytes": 900_000,
        "estimated_stream_bytes": {"nmdh": 600_000, "nmdt": 300_000},
    }))
    split_out = base / "capture-over-stream-cap"
    result = subprocess.run([
        sys.executable, str(runner_path), "--mode", "capture", "--core", str(capable_no_libretro),
        "--rom", str(rom), "--scenario", str(scenario), "--frames", "3",
        "--estimate-file", str(split_estimate_file), "--out", str(split_out),
        "--max-bytes", "1000000",
    ], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "NMDH stream needs 600000 bytes" in result.stderr, result.stderr
    assert not split_out.exists()
    # the same estimate passes the preflight when each half holds its stream
    result = subprocess.run([
        sys.executable, str(runner_path), "--mode", "capture", "--core", str(capable_no_libretro),
        "--rom", str(rom), "--scenario", str(scenario), "--frames", "3",
        "--estimate-file", str(split_estimate_file), "--out", str(base / "capture-within-stream-cap"),
        "--max-bytes", "1200000",
    ], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and "libretro" in result.stderr.lower(), result.stderr

    # light evidence: refused before anything runs when the scenario cannot give a whole trace
    def light(name, scenario_file, estimate, *extra, started=False):
        out = base / name
        done = subprocess.run([
            sys.executable, str(runner_path), "--mode", "capture", "--core", str(capable_no_libretro),
            "--rom", str(rom), "--scenario", str(scenario_file), "--frames", "3",
            "--estimate-file", str(estimate), "--out", str(out), *extra,
        ], capture_output=True, text=True, timeout=10)
        assert done.returncode != 0 and out.exists() == started, done.stderr
        return done.stderr

    pc_scenario = base / "pc-stop-scenario.json"
    pc_scenario.write_text(json.dumps({
        **matching_scenario,
        "required_capabilities": matching_scenario["required_capabilities"] + [
            "controlled_stop", "master_clock"],
        "stop": {"cpu": "m68k", "criterion": "pc", "value": 0x200, "action": "stop"}}))
    assert "light evidence needs a frame_limit stop" in light(
        "light-pc-stop", pc_scenario, estimate_file, "--evidence", "light")
    light_checkpoint = base / "checkpoint-scenario.json"
    light_checkpoint.write_text(json.dumps({
        **matching_scenario,
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
        "required_capabilities": matching_scenario["required_capabilities"] + ["sync_checkpoint"],
        "checkpoint_test": {"boundary": "post_frame", "frame": 1, "continuation_frames": 1}}))
    assert "light evidence cannot run a checkpoint test" in light(
        "light-checkpoint", light_checkpoint, estimate_file, "--evidence", "light")
    # an estimate made in one evidence mode does not license a capture in the other
    frame_scenario = base / "frame-limit-scenario.json"
    frame_scenario.write_text(json.dumps({
        **matching_scenario, "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"}}))
    frame_metadata = runner._make_identity(
        capable_no_libretro, rom, json.loads(frame_scenario.read_text()), 3)
    full_estimate = base / "full-estimate.json"
    full_estimate.write_text(json.dumps({
        "status": "complete", "metadata": frame_metadata, "frames": 3,
        "estimated_seconds": 0.0, "estimated_bytes": 0}))
    light_estimate = base / "light-estimate.json"
    light_estimate.write_text(json.dumps({
        "status": "complete", "metadata": frame_metadata, "frames": 3, "evidence": "light",
        "estimated_seconds": 0.0, "estimated_bytes": 0, "estimated_stream_bytes": {"nmde": 0}}))
    assert "preflight estimate evidence mode mismatch" in light(
        "light-with-full-estimate", frame_scenario, full_estimate, "--evidence", "light")
    assert "preflight estimate evidence mode mismatch" in light(
        "full-with-light-estimate", frame_scenario, light_estimate)
    # with the modes matching, the light capture passes the preflight and reaches the core
    assert "libretro" in light(
        "light-with-light-estimate", frame_scenario, light_estimate, "--evidence", "light",
        started=True).lower()

    instruction_event = {"kind": 1, "cpu": 1, "pc": 0x200, "next_pc": 0x206}
    pc_stop = {"cpu": "m68k", "criterion": "pc", "action": "stop", "value": 0x206}
    sentinel_stop = {"cpu": "m68k", "criterion": "return_sentinel",
                     "action": "replace", "value": 0x206}
    assert runner.instruction_stop_matches(pc_stop, instruction_event, 1, False)
    assert runner.instruction_stop_matches(sentinel_stop, instruction_event, 1, False)
    assert not runner.instruction_stop_matches({**pc_stop, "value": 0x200},
                                               instruction_event, 1, False)
    budget_stop = {"cpu": "m68k", "criterion": "instruction_budget",
                   "action": "stop", "value": 4}
    assert runner.instruction_stop_matches(budget_stop, instruction_event, 4, False)
    assert not runner.instruction_stop_matches(budget_stop, instruction_event, 3, False)
    interrupt_stop = {"cpu": "m68k", "criterion": "interrupt", "action": "stop"}
    assert runner.instruction_stop_matches(interrupt_stop, instruction_event, 4, True)
    assert not runner.instruction_stop_matches(interrupt_stop, instruction_event, 4, False)
    frame_stop = {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"}
    assert not runner.instruction_stop_matches(frame_stop, instruction_event, 4, False)

    handoff_event = runner.DecompEvent()
    handoff_event.struct_size = runner.c.sizeof(runner.DecompEvent)
    handoff_event.kind = runner.EVENT_STOP
    handoff_event.cpu = runner.TRACE_M68K
    handoff_event.master_clock = 900
    handoff = handoff_event.payload.handoff
    handoff.stop.criterion = runner.STOP_CRITERION_IDS["pc"]
    handoff.stop.reason = runner.ACTION_STOP
    handoff.stop.value = 0x206
    handoff.stop.instruction_count = 4
    handoff.stop.stop_clock = 900
    handoff.master_clock = 900
    handoff.z80_master_clock = 885
    handoff.vdp_clock = 900
    handoff.ym_clock = 900
    handoff.psg_clock = 900
    handoff.io_clock = 900
    record = runner._event_dict(handoff_event)
    assert record["payload"]["criterion"] == "pc"
    assert record["payload"]["handoff"]["master_clock"] == 900
    assert record["payload"]["handoff"]["stop_clock"] == 900
    assert record["payload"]["handoff"]["z80_master_clock"] == 885
    assert record["payload"]["handoff"]["m68k"]["cycles"] == 0

    no_master_clock = {
        "version": 1, "region": "U", "model": "md1va3", "pad1": "3", "pad2": "3",
        "input": {},
        "required_capabilities": ["m68k_instruction", "m68k_state", "controlled_stop"],
        "stop": {"cpu": "m68k", "criterion": "pc", "action": "stop", "value": 0x206},
    }
    try:
        runner._validate_scenario(no_master_clock, 1)
    except runner.RunnerError as exc:
        assert "master_clock" in str(exc)
    else:
        raise AssertionError("non-frame stop accepted without a master-clock capability")
    rebase_scenario = {
        "version": 1, "region": "U", "model": "md1va3", "pad1": "3", "pad2": "3",
        "input": {},
        "required_capabilities": ["m68k_instruction", "m68k_state"],
        "stop": {"cpu": "m68k", "criterion": "frame_limit", "action": "stop"},
        "test_rebase_frame": 1,
    }
    assert runner._validate_scenario(rebase_scenario, 1)["test_rebase_frame"] == 1
    for invalid_rebase_frame in (0, 2, True):
        try:
            runner._validate_scenario({**rebase_scenario,
                                       "test_rebase_frame": invalid_rebase_frame}, 1)
        except runner.RunnerError as exc:
            assert "test_rebase_frame" in str(exc)
        else:
            raise AssertionError("out-of-range synthetic rebase frame was accepted")

    timed_out = runner.supervise_child(
        [sys.executable, "-c", "import time; time.sleep(2)"], timeout_seconds=0.05)
    assert timed_out.timed_out and timed_out.returncode != 0

    timeout_out = base / "timeout-status"
    parent_args = argparse.Namespace(
        mode="estimate", core=capable_no_libretro, rom=rom, scenario=scenario,
        frames=3, estimate_file=None, out=timeout_out, max_seconds=1, max_bytes=1024 * 1024,
        evidence="full", records_digest=False,
    )
    original_supervisor = runner.supervise_child

    def timed_out_worker(command, timeout_seconds):
        output = pathlib.Path(command[command.index("--out") + 1])
        (output / "sample.nmdh.partial").write_bytes(b"incomplete NMDH")
        (output / "sample.nmdt.partial").write_bytes(b"incomplete NMDT")
        return runner.ChildResult(-15, "", "", True)

    runner.supervise_child = timed_out_worker
    try:
        try:
            runner._run_parent(parent_args)
        except runner.RunnerError:
            pass
        else:
            raise AssertionError("timed out run was accepted")
    finally:
        runner.supervise_child = original_supervisor
    timeout_status = json.loads((timeout_out / "status.json").read_text())
    assert timeout_status["status"] == "incomplete_timeout"
    assert (timeout_out / "sample.nmdh.partial").is_file()
    assert (timeout_out / "sample.nmdt.partial").is_file()
    assert not (timeout_out / "manifest.json").exists()

    storage_out = base / "storage-limit-status"
    storage_parent_args = argparse.Namespace(
        mode="capture", core=capable_no_libretro, rom=rom, scenario=scenario,
        frames=3, estimate_file=estimate_file, out=storage_out, max_seconds=1,
        max_bytes=1024,
        evidence="full", records_digest=False,
    )

    def storage_limited_worker(command, timeout_seconds):
        output = pathlib.Path(command[command.index("--out") + 1])
        (output / "capture.nmdh.partial").write_bytes(b"incomplete NMDH")
        (output / "capture.nmdt.partial").write_bytes(b"incomplete NMDT")
        (output / "worker-status.json").write_text(json.dumps({
            "status": "incomplete_storage_limit",
            "reason": "NMDT v2 stream exceeded its storage limit",
            "identities": matching_metadata["identities"],
        }))
        return runner.ChildResult(1, "", "storage limit")

    runner.supervise_child = storage_limited_worker
    try:
        try:
            runner._run_parent(storage_parent_args)
        except runner.RunnerError:
            pass
        else:
            raise AssertionError("storage-limited run was accepted")
    finally:
        runner.supervise_child = original_supervisor
    storage_status = json.loads((storage_out / "status.json").read_text())
    assert storage_status["status"] == "incomplete_storage_limit"
    assert (storage_out / "capture.nmdh.partial").is_file()
    assert (storage_out / "capture.nmdt.partial").is_file()
    assert not (storage_out / "manifest.json").exists()

print("decomp runner checks: ok")
