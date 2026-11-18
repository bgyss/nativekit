#!/usr/bin/env python3
"""Measure a bounded combined-CPU NMDH capture with input identities."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[4]
RUNNER = ROOT / "consoles/md/tools/decomp/run.py"
FIXTURES = ROOT / "consoles/md/tests/decomp/fixtures.py"
PINS = ROOT / "tools/toolchains/pins.env"
CORE = ROOT / "build/toolchains/blastem/libblastem.dylib"
M68K_ASSEMBLER = ROOT / "build/toolchains/vasm/vasmm68k_mot"
Z80_ASSEMBLER = ROOT / "build/toolchains/vasm/vasmz80_oldstyle"
OUTPUT_ROOT = ROOT / "build/decomp/synthetic-nmdh"


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def pin_value(name):
    for line in PINS.read_text().splitlines():
        if line.startswith(name + "="):
            return line.split("=", 1)[1].split("#", 1)[0].strip()
    raise RuntimeError("missing tool pin: " + name)


def latest_build_stamps():
    candidates = [path for path in (ROOT / "build/toolchains/src").glob("blastem-build-*")
                  if (path / ".nativekit-patch-stamp").is_file() and
                  (path / ".nativekit-source-stamp").is_file() and
                  (path / "libblastem.dylib").is_file()]
    if not candidates:
        raise RuntimeError("no built pinned BlastEm source stamp was found")
    build = max(candidates, key=lambda path: (path / ".nativekit-source-stamp").stat().st_mtime)
    return {
        "candidate_patch_stamp": (build / ".nativekit-patch-stamp").read_text().strip(),
        "candidate_source_stamp": (build / ".nativekit-source-stamp").read_text().strip(),
    }


def summarize_measurement(result, wall_seconds, peak_rss_bytes, max_seconds, max_bytes):
    frames = result.get("frames_run")
    events = result.get("events")
    stream_bytes = result.get("stream_bytes")
    worker_seconds = result.get("elapsed_seconds")
    if (type(frames) is not int or frames < 1 or type(events) is not int or events < 0 or
            type(stream_bytes) is not int or stream_bytes < 0 or
            type(worker_seconds) not in (int, float) or worker_seconds <= 0 or
            wall_seconds < 0 or peak_rss_bytes < 0 or max_seconds <= 0 or max_bytes <= 0):
        raise ValueError("invalid capture measurements")
    return {
        "frames": frames,
        "events": events,
        "nmdt_records": result.get("nmdt_records"),
        "events_per_second": events / worker_seconds,
        "bytes_per_frame": stream_bytes / frames,
        "stream_bytes": stream_bytes,
        "worker_elapsed_seconds": worker_seconds,
        "capture_wall_seconds": wall_seconds,
        "runner_overhead_seconds": max(0.0, wall_seconds - worker_seconds),
        "peak_worker_rss_bytes": peak_rss_bytes,
        "within_time_limit": wall_seconds <= max_seconds,
        "within_storage_limit": stream_bytes <= max_bytes,
    }


def run_capture(command, max_seconds):
    started = time.monotonic()
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                timeout=max_seconds + 30)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError("benchmark capture exceeded its outer timeout") from exc
    wall_seconds = time.monotonic() - started
    if result.returncode:
        raise RuntimeError("benchmark capture failed: " +
                           (result.stderr or result.stdout).strip())
    return wall_seconds


def run_estimate(command, timeout_seconds):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            timeout=timeout_seconds + 30)
    if result.returncode:
        raise RuntimeError("benchmark estimate failed: " + (result.stderr or result.stdout).strip())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--max-seconds", type=float, default=420)
    parser.add_argument("--max-bytes", type=int, default=268435456)
    args = parser.parse_args(argv)
    if args.frames < 1 or args.max_seconds <= 0 or args.max_bytes < 1024:
        raise ValueError("frames and resource limits must be positive")
    for path in (CORE, M68K_ASSEMBLER, Z80_ASSEMBLER):
        if not path.is_file():
            raise RuntimeError("pinned benchmark tool is missing: " + path.name)

    sys.path.insert(0, str(ROOT / "consoles/md/tests/decomp"))
    from fixtures import build_benchmark_fixture

    run_dir = OUTPUT_ROOT / ("benchmark-" + str(time.time_ns()) + "-" + uuid.uuid4().hex[:8])
    estimate_dir = run_dir / "estimate"
    capture_dir = run_dir / "capture"
    run_dir.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix="nmdh-benchmark-") as temporary:
        rom_path, scenario_path, fixture = build_benchmark_fixture(
            temporary, M68K_ASSEMBLER, Z80_ASSEMBLER)
        common = [sys.executable, str(RUNNER), "--core", str(CORE), "--rom", str(rom_path),
                  "--scenario", str(scenario_path), "--frames", str(args.frames),
                  "--max-seconds", str(args.max_seconds), "--max-bytes", str(args.max_bytes)]
        run_estimate([*common, "--mode", "estimate", "--out", str(estimate_dir)],
                     args.max_seconds)
        estimate = json.loads((estimate_dir / "estimate.json").read_text())
        if estimate.get("status") != "complete":
            raise RuntimeError("benchmark estimate is incomplete")
        wall_seconds = run_capture(
            [*common, "--mode", "capture", "--estimate-file",
             str(estimate_dir / "estimate.json"), "--out", str(capture_dir)],
            args.max_seconds)

    manifest_path = capture_dir / "manifest.json"
    if not manifest_path.is_file():
        raise RuntimeError("benchmark capture omitted its manifest")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("status") != "complete" or manifest.get("frames_run") != args.frames:
        raise RuntimeError("benchmark capture is incomplete or ran the wrong frame count")
    counts = manifest.get("instruction_counts", {})
    if counts.get("m68k", 0) <= 0 or counts.get("z80", 0) <= 0:
        raise RuntimeError("benchmark fixture did not execute both CPUs")
    peak_rss_bytes = manifest.get("peak_worker_rss_bytes")
    if type(peak_rss_bytes) is not int or peak_rss_bytes <= 0:
        raise RuntimeError("benchmark capture omitted positive peak worker RSS")

    metrics = summarize_measurement(manifest, wall_seconds, peak_rss_bytes,
                                    args.max_seconds, args.max_bytes)
    metrics["instruction_counts"] = manifest.get("instruction_counts", {})
    metrics["event_counts"] = manifest.get("event_counts", {})
    files = manifest.get("outputs", [])
    stream_sizes = {name: (capture_dir / name).stat().st_size for name in files}
    metrics["nmdh_bytes"] = stream_sizes.get("capture.nmdh", 0)
    metrics["nmdt_bytes"] = stream_sizes.get("capture.nmdt", 0)
    per_stream_cap = args.max_bytes // 2
    metrics["within_split_storage_limit"] = all(
        size <= per_stream_cap for size in stream_sizes.values())

    identities = dict(manifest["metadata"]["identities"])
    identities.update({
        "blastem_pin": pin_value("BLASTEM_PIN"),
        "vasm_pin": pin_value("VASM_PIN"),
        **latest_build_stamps(),
        "m68k_assembler_sha256": sha256_file(M68K_ASSEMBLER),
        "z80_assembler_sha256": sha256_file(Z80_ASSEMBLER),
        "fixture_source_sha256": sha256_file(FIXTURES),
        "benchmark_source_sha256": sha256_file(Path(__file__)),
    })
    report = {
        "format": "NMDH D01 synthetic resource budget v1",
        "status": "complete",
        "identities": identities,
        "workload": {
            **fixture,
            "m68k_loop": "DIVU.W and DBRA delay, then DIVU.W loop",
            "z80_loop": "DJNZ loop during the bounded M68K delay; BUSREQ holds Z80 afterward",
        },
        "limits": {"max_seconds": args.max_seconds, "max_bytes": args.max_bytes,
                   "per_stream_bytes": per_stream_cap},
        "estimate": estimate,
        "metrics": metrics,
        "artifacts": {"directory": run_dir.relative_to(ROOT).as_posix(),
                      "capture_manifest": (capture_dir / "manifest.json").relative_to(ROOT).as_posix()},
    }
    violations = []
    if not metrics["within_time_limit"]:
        violations.append("capture exceeded max_seconds")
    if not metrics["within_storage_limit"] or not metrics["within_split_storage_limit"]:
        violations.append("capture exceeded max_bytes")
    if violations:
        report["status"] = "incomplete"
        report["violations"] = violations
    report_path = run_dir / "benchmark.json"
    report_path.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print("benchmark: frames=%d events=%d events_per_second=%.1f bytes_per_frame=%.1f runner_overhead_seconds=%.6f peak_worker_rss_bytes=%d status=%s" %
          (metrics["frames"], metrics["events"], metrics["events_per_second"],
           metrics["bytes_per_frame"], metrics["runner_overhead_seconds"],
           metrics["peak_worker_rss_bytes"], report["status"]))
    print("benchmark report: " + report_path.relative_to(ROOT).as_posix())
    if violations:
        raise RuntimeError("; ".join(violations))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        print("benchmark: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
