#!/usr/bin/env python3
"""Run a fixed no-input/Start survey without recording ROM paths or bytes."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import unicodedata

from features import FeatureError, extract_features
from trace import read as read_trace

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
RUNNER = REPO_ROOT / "consoles/md/tests/oracle/run.py"
DEFAULT_CORE = REPO_ROOT / "build/toolchains/blastem/libblastem.dylib"
DEFAULT_FRAMES = 120
DEFAULT_START_FRAME = 60
ROM_EXTENSIONS = {".bin", ".gen", ".md", ".rom"}
ROM_MIN_BYTES = 0x200
TITLE_RANGE = (0x150, 0x180)
ROM_SIGNATURE_RANGE = (0x100, 0x104)
SRAM_ID_RANGE = (0x1B0, 0x1B2)
SRAM_START_RANGE = (0x1B4, 0x1B8)
SRAM_END_RANGE = (0x1B8, 0x1BC)
FEATURE_NAMES = (
    "direct_68k_ym_writes", "dma_68k_to_vdp", "dma_copy", "dma_fill",
    "hint_enabled", "hint_lines", "interlace", "mapper_register_access",
    "shadow_highlight", "six_button_probe", "sram_access", "vdp_mode",
    "vdp_registers", "z80_dac_streaming",
)
STATUS_ORDER = ("completed", "invalid", "unsupported", "incomplete", "failed")
SCENARIOS = ("no_input", "start")
SURVEY_TRACE_SCOPE = "survey-feature-filter-v1"
SURVEY_TRACE_FILTERS = {
    "m68k_cpu_access": {"ym_ports", "pad_ports", "mapper_registers"},
    "z80_cpu_access": {"ym_ports", "bank_registers", "bank_window"},
    "device_events": {"all"},
}


def sanitize_title(raw_title):
    """Make a ROM header title safe for a report cell, never a path."""
    title = unicodedata.normalize("NFKD", raw_title).encode("ascii", "ignore").decode("ascii")
    title = title.replace("/", " ").replace("\\", " ")
    title = re.sub(r"[^A-Za-z0-9 _().+-]", "_", title)
    title = re.sub(r"\s+", " ", title).strip(" ._")
    title = re.sub(r"\.{2,}", ".", title).strip(" ._")
    return title[:64] or "untitled"


def _rom_header(path):
    size = path.stat().st_size
    if size < ROM_MIN_BYTES:
        return {"valid": False, "title": "untitled", "sram_window": None,
                "reason": "cartridge is shorter than its header"}
    with path.open("rb") as stream:
        header = stream.read(max(TITLE_RANGE[1], SRAM_END_RANGE[1]))
    if header[ROM_SIGNATURE_RANGE[0]:ROM_SIGNATURE_RANGE[1]] != b"SEGA":
        return {"valid": False, "title": "untitled", "sram_window": None,
                "reason": "cartridge header signature is missing"}
    title = sanitize_title(header[TITLE_RANGE[0]:TITLE_RANGE[1]].decode("ascii", "ignore").strip("\0 "))
    sram_window = None
    if header[SRAM_ID_RANGE[0]:SRAM_ID_RANGE[1]] == b"RA":
        start = int.from_bytes(header[SRAM_START_RANGE[0]:SRAM_START_RANGE[1]], "big") & 0xFFFFFE
        inclusive_end = int.from_bytes(header[SRAM_END_RANGE[0]:SRAM_END_RANGE[1]], "big") | 1
        end = inclusive_end + 1
        if start < end <= 0x800000:
            sram_window = {"start": start, "end": end}
    return {"valid": True, "title": title, "sram_window": sram_window}


def _sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _unique_title(title, counts):
    count = counts.get(title.casefold(), 0) + 1
    counts[title.casefold()] = count
    return title if count == 1 else f"{title} [{count}]"


def _classify_failure(stderr, returncode):
    message = (stderr or "").lower()
    if "unsupported mapper" in message:
        return "unsupported", "unsupported mapper"
    if "unsupported" in message or "refused cartridge" in message:
        return "unsupported", "unsupported cartridge or core setting"
    if "invalid" in message or "empty cartridge" in message:
        return "invalid", "invalid cartridge"
    return "failed", f"runner failed (exit {returncode})"


def _run_scenario(rom, core, runner, frames, start_frame, scenario, sram_window,
                  expected_core_sha256, executor):
    with tempfile.TemporaryDirectory(prefix="nmd-survey-") as temp:
        work = pathlib.Path(temp)
        output = work / "capture"
        command = [sys.executable, str(runner), "--core", str(core), "--rom", str(rom),
                   "--frames", str(frames), "--out", str(output), "--trace", "--feature-only"]
        if sram_window:
            command.extend(("--sram-window", f"{sram_window['start']:#x}:{sram_window['end']:#x}"))
        expected_input = {}
        if scenario == "start":
            input_path = work / "input.json"
            expected_input = {str(start_frame): {"1": ["START"]}}
            input_path.write_text(json.dumps(expected_input))
            command.extend(("--input", str(input_path)))
        try:
            completed = executor(command, capture_output=True, text=True, timeout=300)
        except (OSError, subprocess.SubprocessError):
            return {"status": "failed", "reason": "runner could not complete"}
        if completed.returncode:
            status, reason = _classify_failure(completed.stderr, completed.returncode)
            return {"status": status, "reason": reason}
        try:
            manifest_path = output / "manifest.json"
            trace_path = output / "bus.trace"
            if not manifest_path.is_file() or not trace_path.is_file():
                return {"status": "incomplete", "reason": "runner output is missing trace metadata"}
            manifest = json.loads(manifest_path.read_text())
            if not isinstance(manifest, dict):
                return {"status": "incomplete", "reason": "runner manifest is not an object"}
            manifest_sha256 = manifest.get("core_sha256")
            if (not isinstance(manifest_sha256, str) or
                    re.fullmatch(r"[0-9a-f]{64}", manifest_sha256) is None or
                    manifest_sha256 != expected_core_sha256):
                return {"status": "incomplete", "reason": "runner core identity is invalid or mismatched"}
            if manifest.get("capture_mode") != "feature-only":
                return {"status": "incomplete", "reason": "runner capture mode is not feature-only"}
            if manifest.get("trace_scope") != SURVEY_TRACE_SCOPE:
                return {"status": "incomplete", "reason": "runner trace scope is missing or unsupported"}
            filters = manifest.get("trace_filters")
            if not isinstance(filters, dict):
                return {"status": "incomplete", "reason": "runner trace filters are invalid"}
            for key, required_ranges in SURVEY_TRACE_FILTERS.items():
                recorded = filters.get(key)
                if (not isinstance(recorded, list) or
                        any(not isinstance(item, str) for item in recorded) or
                        ("all" not in recorded and not required_ranges.issubset(recorded))):
                    return {"status": "incomplete", "reason": "runner trace filter coverage is invalid"}
            if sram_window:
                recorded = filters["m68k_cpu_access"]
                if "all" not in recorded and "sram_window" not in recorded:
                    return {"status": "incomplete", "reason": "runner SRAM trace coverage is invalid"}
            if manifest.get("frames") != frames or manifest.get("input") != expected_input:
                return {"status": "incomplete", "reason": "bounded frame/input coverage mismatch"}
            if sram_window:
                manifest["sram_window"] = sram_window
            with trace_path.open("rb") as stream:
                events = list(read_trace(stream))
            extracted = extract_features(manifest, events)
            return {"status": "completed", "features": extracted["features"],
                    "core_sha256": extracted.get("core_sha256")}
        except (OSError, json.JSONDecodeError, ValueError):
            return {"status": "incomplete", "reason": "runner trace could not be validated"}


def _aggregate(titles):
    report = {}
    for scenario in SCENARIOS:
        statuses = {status: 0 for status in STATUS_ORDER}
        features = {name: {status: 0 for status in ("observed", "not_observed", "unavailable")}
                    for name in FEATURE_NAMES}
        for title in titles:
            run = title[scenario]
            status = run["status"]
            statuses[status] = statuses.get(status, 0) + 1
            observations = run.get("features", {})
            for name in FEATURE_NAMES:
                feature_status = observations.get(name, {}).get("status", "unavailable")
                features[name][feature_status] = features[name].get(feature_status, 0) + 1
        report[scenario] = {"statuses": statuses, "features": features}
    return report


def survey_directory(rom_dir, core, runner=RUNNER, frames=DEFAULT_FRAMES,
                     start_frame=DEFAULT_START_FRAME, executor=subprocess.run):
    """Survey a directory with fixed frame/input bounds; output contains no paths."""
    rom_dir = pathlib.Path(rom_dir)
    core = pathlib.Path(core)
    runner = pathlib.Path(runner)
    if not rom_dir.is_dir():
        raise ValueError("ROM directory is unavailable")
    if not isinstance(frames, int) or isinstance(frames, bool) or frames < 1:
        raise ValueError("frames must be positive")
    if (not isinstance(start_frame, int) or isinstance(start_frame, bool) or
            not 1 <= start_frame <= frames):
        raise ValueError("Start frame must be within the bounded run")
    try:
        expected_core_sha256 = _sha256_file(core)
    except OSError as exc:
        raise ValueError("reference core is unavailable") from exc

    candidates = sorted((path for path in rom_dir.rglob("*")
                         if path.is_file() and path.suffix.lower() in ROM_EXTENSIONS),
                        key=lambda path: path.relative_to(rom_dir).as_posix().casefold())
    title_counts = {}
    titles = []
    core_sha256 = None
    for path in candidates:
        try:
            header = _rom_header(path)
        except OSError:
            header = {"valid": False, "title": "untitled", "sram_window": None,
                      "reason": "cartridge header is unavailable"}
        title = _unique_title(header["title"], title_counts)
        if not header["valid"]:
            invalid = {"status": "invalid", "reason": header.get("reason", "invalid cartridge header")}
            titles.append({"title": title, "no_input": invalid, "start": dict(invalid)})
            continue
        record = {"title": title}
        for scenario in SCENARIOS:
            record[scenario] = _run_scenario(path, core, runner, frames, start_frame,
                                             scenario, header["sram_window"], expected_core_sha256, executor)
            if record[scenario]["status"] == "completed":
                digest = record[scenario].get("core_sha256")
                if core_sha256 is None:
                    core_sha256 = digest
                elif digest != core_sha256:
                    record[scenario] = {"status": "incomplete", "reason": "core changed during survey"}
        titles.append(record)

    return {
        "core_sha256": core_sha256,
        "frames_per_run": frames,
        "start_press": {"frame": start_frame, "port": 1, "button": "START", "duration_frames": 1},
        "titles": titles,
        "aggregate": _aggregate(titles),
    }


def _format_run(run):
    status = run["status"]
    if status != "completed":
        return f"{status}: {run.get('reason', '')}".rstrip(": ")
    observations = run.get("features", {})
    pieces = []
    for name in FEATURE_NAMES:
        observation = observations.get(name, {"status": "unavailable"})
        detail = ""
        value = observation.get("value")
        if value is not None:
            detail = ":" + json.dumps(value, sort_keys=True, separators=(",", ":"))
        pieces.append(f"{name}={observation['status']}{detail}")
    return "completed: " + "; ".join(pieces)


def render_markdown(report):
    lines = [
        "# Mega Drive / Genesis bounded feature survey",
        "",
        f"- Core SHA-256: `{report.get('core_sha256') or 'unavailable'}`",
        f"- Bounded run: {report['frames_per_run']} frames per scenario.",
        f"- Start scenario: port {report['start_press']['port']}, frame "
        f"{report['start_press']['frame']}, one-frame {report['start_press']['button']} snapshot.",
        "- Trace scope: survey-feature-filter-v1; CPU events are filtered to feature ranges, device events are retained.",
        "- VDP mode classification uses the M5 bit when set; otherwise the M4 bit selects Mode 4.",
        ('- "Observed" describes captured evidence, not feature use; boolean `enabled` values show '
         'the final setting and `ever_enabled` shows whether it was enabled during the run.'),
        "- ROM paths and contents are not included in this report.",
        "",
        "## Run status counts",
        "",
        "| Scenario | Completed | Invalid | Unsupported | Incomplete | Failed |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for scenario in SCENARIOS:
        counts = report["aggregate"][scenario]["statuses"]
        lines.append("| %s | %d | %d | %d | %d | %d |" %
                     (scenario.replace("_", " "), *(counts.get(status, 0) for status in STATUS_ORDER)))

    lines.extend(("", "## Feature status counts", "",
                  "| Feature | Scenario | Observed | Not observed | Unavailable |",
                  "|---|---|---:|---:|---:|"))
    for name in FEATURE_NAMES:
        for scenario in SCENARIOS:
            counts = report["aggregate"][scenario]["features"][name]
            lines.append("| %s | %s | %d | %d | %d |" %
                         (name, scenario.replace("_", " "), counts["observed"],
                          counts["not_observed"], counts["unavailable"]))

    lines.extend(("", "## Per-title observations", "",
                  "| Sanitized title | No-input run | Start run |",
                  "|---|---|---|"))
    for title in report["titles"]:
        safe_title = title["title"].replace("|", "_").replace("\n", " ")
        lines.append(f"| {safe_title} | {_format_run(title['no_input'])} | {_format_run(title['start'])} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom_dir", type=pathlib.Path)
    parser.add_argument("--core", type=pathlib.Path, default=DEFAULT_CORE)
    parser.add_argument("--runner", type=pathlib.Path, default=RUNNER)
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    parser.add_argument("--start-frame", type=int, default=DEFAULT_START_FRAME)
    parser.add_argument("--out", type=pathlib.Path, default=REPO_ROOT / "consoles/md/docs/survey.md")
    args = parser.parse_args()
    if not args.core.is_file():
        parser.error("candidate core is unavailable; build tools:md first")
    report = survey_directory(args.rom_dir, args.core, args.runner, args.frames, args.start_frame)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(render_markdown(report))
    counts = report["aggregate"]["no_input"]["statuses"]
    print("survey: %d titles; %d no-input runs completed" %
          (len(report["titles"]), counts.get("completed", 0)))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, FeatureError) as exc:
        sys.exit(f"survey: {exc}")
