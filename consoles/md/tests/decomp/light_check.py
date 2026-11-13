#!/usr/bin/env python3
"""A light capture against a full capture of the same run.

`run.py --evidence light` feeds the core's events to `codemap.Ingest` as they
arrive and writes the evidence digest (`capture.nmde`) instead of the NMDH and
NMDT streams. On the authored intake cartridge, run on the pinned reference
core, the light capture must report the same events, picture, sound and
record digest as the full one, leave no stream behind, and give the intake
the same code map. A light capture that stops early must leave nothing the
intake accepts.
"""
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import census_fixture  # noqa: E402
import codemap  # noqa: E402
import intake  # noqa: E402

SAME = ("events", "event_counts", "instruction_counts", "nmdt_records", "frames_run",
        "video_frames", "audio_frames", "video_sha256", "audio_sha256")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def manifest_of(capture):
    return json.loads((capture / "manifest.json").read_text())


def code_map(rom_path, out, capture):
    _, _, rebuilt = intake.run_intake(rom_path, out, [capture], "light-control")
    require(rebuilt["identical"], "the skeleton did not rebuild the image")
    return (out / "codemap.json").read_bytes()


def light_equals_full(temp, rom_path, scenario_path):
    full = census_fixture.capture(rom_path, scenario_path, temp / "full")
    light = census_fixture.capture(rom_path, scenario_path, temp / "light",
                                   extra=("--evidence", "light", "--records-digest"))
    a, b = manifest_of(full), manifest_of(light)
    require(a["evidence"] == "full" and b["evidence"] == "light" and
            b["format"] == "NMDE v1 capture" and b["outputs"] == ["capture.nmde"],
            "the manifests do not name their evidence modes")
    for field in SAME:
        require(a[field] == b[field], "light capture differs in %s: %r against %r"
                % (field, b[field], a[field]))
    require(len(a["nmdh_records_sha256"]) == 64 and
            a["nmdh_records_sha256"] == b["nmdh_records_sha256"],
            "the light capture's record digest is not the full capture's")
    names = sorted(path.name for path in light.iterdir())
    require(names == ["capture.nmde", "manifest.json", "worker-result.json"],
            "a light capture left %r" % names)
    size = (light / "capture.nmde").stat().st_size
    require(b["nmde_bytes"] == b["stream_bytes"] == size and b["nmdh_bytes"] == 0 and
            b["nmdt_bytes"] == 0 and size < 65536,
            "the light manifest's sizes are wrong: %d digest bytes" % size)
    wanted = code_map(rom_path, temp / "from-full", full)
    require(code_map(rom_path, temp / "from-light", light) == wanted,
            "the light capture's code map differs from the full capture's")
    return wanted, a["stream_bytes"], size


def light_without_digest(temp, rom_path, scenario_path, wanted):
    light = census_fixture.capture(rom_path, scenario_path, temp / "plain",
                                   extra=("--evidence", "light"))
    manifest = manifest_of(light)
    require("nmdh_records_sha256" not in manifest, "a record digest nobody asked for")
    require(manifest["events"] and manifest["instruction_counts"]["m68k"],
            "a light capture without a record digest counted no events")
    require(code_map(rom_path, temp / "from-plain", light) == wanted,
            "the light capture without a record digest gives another code map")


def light_stops_early(temp, rom_path, scenario_path):
    """Over its byte cap, a light capture leaves no digest and nothing the intake takes."""
    common = [sys.executable, str(census_fixture.RUNNER), "--core", str(census_fixture.CORE),
              "--rom", str(rom_path), "--scenario", str(scenario_path), "--frames", "3",
              "--evidence", "light"]
    done = subprocess.run(common + ["--mode", "estimate", "--out", str(temp / "early-estimate")],
                          capture_output=True, text=True, timeout=450)
    require(done.returncode == 0, "light preflight failed: " + done.stderr.strip())
    estimate_path = temp / "early-estimate" / "estimate.json"
    estimate = json.loads(estimate_path.read_text())
    require(estimate["evidence"] == "light" and set(estimate["estimated_stream_bytes"]) == {"nmde"},
            "a light estimate does not describe the digest: %r" % estimate)
    # the planted estimate fits the cap; the digest itself does not
    estimate.update({"estimated_bytes": 100, "estimated_stream_bytes": {"nmde": 100}})
    estimate_path.write_text(json.dumps(estimate))
    out = temp / "early"
    done = subprocess.run(common + ["--mode", "capture", "--estimate-file", str(estimate_path),
                                    "--out", str(out), "--max-bytes", "1024"],
                          capture_output=True, text=True, timeout=450)
    require(done.returncode != 0, "a light capture over its byte cap completed")
    status = json.loads((out / "status.json").read_text())
    require(status["status"] == "incomplete_storage_limit",
            "the stopped light capture reports %r" % status)
    require(not (out / "capture.nmde").exists() and not (out / "manifest.json").exists(),
            "the stopped light capture left a digest or a manifest")
    try:
        codemap.load_evidence(out, codemap.sha256(rom_path.read_bytes()))
    except codemap.CodeMapError:
        return
    raise AssertionError("the intake accepted a light capture that stopped early")


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-light-") as temp:
        temp = pathlib.Path(temp)
        rom_path, scenario_path, _ = census_fixture.build(temp / "fixture")
        wanted, stream_bytes, digest_bytes = light_equals_full(temp, rom_path, scenario_path)
        light_without_digest(temp, rom_path, scenario_path, wanted)
        light_stops_early(temp, rom_path, scenario_path)
    print("light_check: ok (light capture: same events, picture, sound and record digest as "
          "the full one, %d digest bytes for %d stream bytes, same code map with and without "
          "the record digest; a capture over its byte cap leaves nothing the intake takes)"
          % (digest_bytes, stream_bytes))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("light_check: " + str(exc))
