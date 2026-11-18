"""Summarize saved verification runs. Supply one evidence manifest per scenario.

A manifest contains name, lockstep, calls and census paths for MD, or name,
start and assembly paths for Dragon Warrior. Paths are relative to the manifest.
"""
import argparse
from collections import Counter
import json
from pathlib import Path


def summarize(path):
    manifest = json.loads(path.read_text())

    def read(key, schema):
        data = json.loads((path.parent / manifest[key]).read_text())
        if data.get("schema", data.get("schema_version")) != schema:
            raise ValueError(f"{key}: expected {schema}")
        return data

    result = {"scenario": manifest["name"], "evidence": str(path.resolve()),
              "native_video": "unmeasured", "native_audio": "unmeasured",
              "native_frame_drift": "unmeasured"}
    if "lockstep" in manifest:
        run = read("lockstep", "nmd-lockstep")
        calls = read("calls", "nmd-calls")
        census = read("census", "nmd-census")
        identity = run["identities"]
        if any(identity[key] != calls["identities"][key] for key in
               ("rom_sha256", "core_sha256", "scenario_sha256")):
            raise ValueError("call and lockstep evidence identities differ")
        if census["rom"]["sha256"] != identity["rom_sha256"]:
            raise ValueError("census belongs to another ROM")
        if calls["window"]["first_frame"] != 1 or calls["window"]["last_frame"] != run["frames"]:
            raise ValueError("call and lockstep horizons differ")
        reasons = Counter()
        for row in run.get("routines", []):
            reasons.update(row["left_to_original"])
        result.update(rom_sha256=identity["rom_sha256"], rebuild_identical=census["rebuild_identical"],
                      coverage_only=calls.get("coverage_only"), routines=len(calls["routines"]),
                      reached=sum(r["calls"] > 0 for r in calls["routines"]),
                      routine_verdicts=dict(Counter(r["verdict"] for r in calls["routines"])),
                      lockstep=run["verdict"], certified=run["certified"],
                      requested_frames=run["frames"], matched_frames=run.get("frames_compared"),
                      substituted=run.get("calls_substituted"), left_to_original=dict(reasons),
                      divergence=run.get("divergence"), reason=run.get("reason"))
    else:
        run = read("start", "nnes.dw1-start-input-check.v2")
        assembly = read("assembly", "nnes.dw1-asm-fragment-check.v1")
        if run["source_revision_sha256"] != assembly["source_revision_sha256"]:
            raise ValueError("assembly belongs to another ROM")
        result.update(rom_sha256=run["source_revision_sha256"], rebuild_identical="unmeasured",
                      assembled_fragment_bytes=assembly["assembled_fragment_bytes"],
                      drafted_bus_events_matched=run.get("draft_bus_events_matched"),
                      fragments_identical=assembly["byte_exact"], native_stop=run["native_stop_address"],
                      full_bus_match=run["full_start_route_bus_matches"],
                      nmi_event_index_deltas=run["nmi_event_index_deltas"],
                      polls=run["wait_poll_counts_per_nmi"])
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps([summarize(path) for path in args.manifests], indent=2))
    except (KeyError, ValueError, OSError, TypeError) as error:
        parser.exit(1, f"decomp report: {error}\n")
