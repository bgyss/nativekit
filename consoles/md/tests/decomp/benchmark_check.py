#!/usr/bin/env python3
"""Check the resource-budget calculations used by the decomp benchmark."""
import importlib.util
import pathlib

root = pathlib.Path(__file__).resolve().parents[4]
benchmark_path = root / "consoles/md/tools/decomp/benchmark.py"
if not benchmark_path.is_file():
    raise SystemExit("benchmark_check: decomp benchmark is not implemented")
spec = importlib.util.spec_from_file_location("nmdh_benchmark", benchmark_path)
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)

result = {
    "events": 600,
    "frames_run": 3,
    "elapsed_seconds": 2.0,
    "stream_bytes": 12000,
}
summary = benchmark.summarize_measurement(
    result, wall_seconds=2.5, peak_rss_bytes=10 * 1024 * 1024,
    max_seconds=420, max_bytes=256 * 1024 * 1024,
)
assert summary["events_per_second"] == 300
assert summary["bytes_per_frame"] == 4000
assert summary["runner_overhead_seconds"] == 0.5
assert summary["peak_worker_rss_bytes"] == 10 * 1024 * 1024
assert summary["within_time_limit"] is True
assert summary["within_storage_limit"] is True

over_limit = benchmark.summarize_measurement(
    result, wall_seconds=421, peak_rss_bytes=11,
    max_seconds=420, max_bytes=11999,
)
assert over_limit["within_time_limit"] is False
assert over_limit["within_storage_limit"] is False
print("decomp benchmark calculations: ok")
