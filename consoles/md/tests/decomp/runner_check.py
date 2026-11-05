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

    timed_out = runner.supervise_child(
        [sys.executable, "-c", "import time; time.sleep(2)"], timeout_seconds=0.05)
    assert timed_out.timed_out and timed_out.returncode != 0

    timeout_out = base / "timeout-status"
    parent_args = argparse.Namespace(
        mode="estimate", core=capable_no_libretro, rom=rom, scenario=scenario,
        frames=3, estimate_file=None, out=timeout_out, max_seconds=1, max_bytes=1024 * 1024,
    )
    original_supervisor = runner.supervise_child
    runner.supervise_child = lambda command, timeout_seconds: runner.ChildResult(
        -15, "", "", True)
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

print("decomp runner checks: ok")
