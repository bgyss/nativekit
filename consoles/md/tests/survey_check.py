#!/usr/bin/env python3
"""Bounded survey tests using generated cartridges and a fake runner."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

root = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / "consoles/md/tools"))
sys.path.insert(0, str(root / "consoles/md/tests/oracle"))
from survey import render_markdown, survey_directory
from trace import Event, M68K, VDP, READ, WRITE, write as write_trace


CAPS = [
    "m68k_cpu_access", "z80_cpu_access", "vdp_port", "vdp_68k_dma_source",
    "vdp_dma_fill", "vdp_dma_copy", "vdp_hint_lines", "psg_write",
    "ym2612_write", "ym2612_port_write",
]


def rom_bytes(title, size=1024, marker=b""):
    data = bytearray(size)
    data[0x100:0x104] = b"SEGA"
    encoded = title.encode("ascii", "replace")[:48]
    data[0x150:0x180] = encoded.ljust(48, b" ")
    if marker:
        data[0x300:0x300 + len(marker)] = marker
    return data


class SurveyCheck(unittest.TestCase):
    def test_fixed_runs_statuses_sanitization_and_aggregates(self):
        with tempfile.TemporaryDirectory() as temp:
            tmp = pathlib.Path(temp)
            rom_dir = tmp / "roms"
            rom_dir.mkdir()
            (rom_dir / "one.bin").write_bytes(rom_bytes("DUPLICATE"))
            (rom_dir / "two.gen").write_bytes(rom_bytes("DUPLICATE"))
            (rom_dir / "pathlike.md").write_bytes(rom_bytes("../../unsafe/title"))
            (rom_dir / "unsupported.bin").write_bytes(
                rom_bytes("Unsupported mapper", marker=b"UNSUPPORTED_MAPPER"))
            (rom_dir / "incomplete.bin").write_bytes(rom_bytes("Incomplete", marker=b"INCOMPLETE"))
            (rom_dir / "failed.bin").write_bytes(rom_bytes("Failed", marker=b"FAIL_RUN"))
            (rom_dir / "invalid.bin").write_bytes(b"too short")

            calls = []

            def fake_runner(command, capture_output, text, timeout):
                self.assertTrue(capture_output)
                self.assertTrue(text)
                rom_path = pathlib.Path(command[command.index("--rom") + 1])
                out_dir = pathlib.Path(command[command.index("--out") + 1])
                frames = int(command[command.index("--frames") + 1])
                input_path = None
                if "--input" in command:
                    input_path = pathlib.Path(command[command.index("--input") + 1])
                input_script = {} if input_path is None else json.loads(input_path.read_text())
                calls.append((rom_path.name, frames, input_script))
                self.assertIn("--feature-only", command)
                if b"UNSUPPORTED_MAPPER" in rom_path.read_bytes():
                    return subprocess.CompletedProcess(
                        command, 1, "", f"unsupported mapper at {rom_path}")
                if b"FAIL_RUN" in rom_path.read_bytes():
                    return subprocess.CompletedProcess(command, 2, "", f"failed at {rom_path}")
                if b"INCOMPLETE" in rom_path.read_bytes():
                    return subprocess.CompletedProcess(command, 0, "", "")
                out_dir.mkdir(parents=True, exist_ok=True)
                events = [Event(100, VDP, WRITE, 2, 4, 0x8104)]
                with (out_dir / "bus.trace").open("w+b") as stream:
                    write_trace(stream, events)
                manifest = {
                    "core_sha256": "c" * 64,
                    "frames": frames,
                    "capture_mode": "feature-only",
                    "trace_scope": "survey-feature-filter-v1",
                    "trace_filters": {
                        "m68k_cpu_access": ["ym_ports", "pad_ports", "mapper_registers"],
                        "z80_cpu_access": ["ym_ports", "bank_registers", "bank_window"],
                        "device_events": ["all"],
                    },
                    "trace_capabilities": CAPS,
                    "trace_events": len(events),
                    "input": input_script,
                }
                (out_dir / "manifest.json").write_text(json.dumps(manifest))
                return subprocess.CompletedProcess(command, 0, "", "")

            report = survey_directory(rom_dir, pathlib.Path("unused-core"),
                                      pathlib.Path("unused-runner"), frames=3,
                                      start_frame=2, executor=fake_runner)
            markdown = render_markdown(report)
            repeated = survey_directory(rom_dir, pathlib.Path("unused-core"),
                                        pathlib.Path("unused-runner"), frames=3,
                                        start_frame=2, executor=fake_runner)
            self.assertEqual(markdown, render_markdown(repeated))
            self.assertNotIn(str(rom_dir), markdown)
            self.assertNotIn(temp, markdown)
            self.assertNotIn("UNSUPPORTED_MAPPER", markdown)
            self.assertNotIn("../../", markdown)
            self.assertIn("survey-feature-filter-v1", markdown)
            self.assertIn('"Observed" describes captured evidence, not feature use', markdown)
            self.assertNotIn("FAIL_RUN", markdown)
            self.assertIn("DUPLICATE", markdown)
            self.assertIn("DUPLICATE [2]", markdown)
            self.assertIn("unsafe title", markdown)

            titles = {item["title"]: item for item in report["titles"]}
            self.assertEqual(titles["untitled"]["no_input"]["status"], "invalid")
            self.assertEqual(titles["Unsupported mapper"]["no_input"]["status"], "unsupported")
            self.assertEqual(titles["Unsupported mapper"]["start"]["reason"], "unsupported mapper")
            self.assertEqual(titles["Incomplete"]["no_input"]["status"], "incomplete")
            self.assertEqual(titles["Failed"]["no_input"]["status"], "failed")
            self.assertEqual(titles["DUPLICATE"]["no_input"]["status"], "completed")
            self.assertEqual(report["aggregate"]["no_input"]["statuses"]["completed"], 3)
            self.assertEqual(report["aggregate"]["no_input"]["statuses"]["incomplete"], 1)
            self.assertEqual(report["aggregate"]["no_input"]["statuses"]["failed"], 1)
            self.assertEqual(report["aggregate"]["start"]["statuses"]["invalid"], 1)
            self.assertEqual(len(calls), 24)
            self.assertTrue(all(frames == 3 for _, frames, _ in calls))
            self.assertTrue(all(not input_script or input_script == {"2": {"1": ["START"]}}
                                for _, _, input_script in calls))

    def test_invalid_frame_and_start_bounds_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            with self.assertRaises(ValueError):
                survey_directory(directory, pathlib.Path("core"), pathlib.Path("runner"),
                                 frames=0, start_frame=1)
            with self.assertRaises(ValueError):
                survey_directory(directory, pathlib.Path("core"), pathlib.Path("runner"),
                                 frames=2, start_frame=3)

    def test_sram_header_window_is_passed_to_trace_and_reported(self):
        with tempfile.TemporaryDirectory() as temp:
            tmp = pathlib.Path(temp)
            rom_dir = tmp / "roms"
            rom_dir.mkdir()
            rom = rom_bytes("SRAM HEADER")
            rom[0x1B0:0x1B2] = b"RA"
            rom[0x1B4:0x1B8] = (0x200001).to_bytes(4, "big")
            rom[0x1B8:0x1BC] = (0x20FFFE).to_bytes(4, "big")
            (rom_dir / "sram.bin").write_bytes(rom)

            def fake_runner(command, capture_output, text, timeout):
                self.assertIn("--sram-window", command)
                window = command[command.index("--sram-window") + 1]
                self.assertEqual(window, "0x200000:0x210000")
                input_script = {}
                if "--input" in command:
                    input_script = json.loads(pathlib.Path(
                        command[command.index("--input") + 1]).read_text())
                output = pathlib.Path(command[command.index("--out") + 1])
                frames = int(command[command.index("--frames") + 1])
                events = [Event(100, VDP, WRITE, 2, 4, 0x8104),
                          Event(120, M68K, READ, 1, 0x200010, 0xA5)]
                output.mkdir(parents=True, exist_ok=True)
                with (output / "bus.trace").open("w+b") as stream:
                    write_trace(stream, events)
                filters = {
                    "m68k_cpu_access": ["ym_ports", "pad_ports", "mapper_registers", "sram_window"],
                    "z80_cpu_access": ["ym_ports", "bank_registers", "bank_window"],
                    "device_events": ["all"],
                }
                manifest = {
                    "core_sha256": "d" * 64,
                    "frames": frames,
                    "capture_mode": "feature-only",
                    "trace_scope": "survey-feature-filter-v1",
                    "trace_filters": filters,
                    "trace_capabilities": CAPS,
                    "trace_events": len(events),
                    "input": input_script,
                }
                (output / "manifest.json").write_text(json.dumps(manifest))
                return subprocess.CompletedProcess(command, 0, "", "")

            report = survey_directory(rom_dir, pathlib.Path("unused-core"),
                                      pathlib.Path("unused-runner"), frames=3,
                                      start_frame=2, executor=fake_runner)
            title = report["titles"][0]
            for scenario in ("no_input", "start"):
                sram = title[scenario]["features"]["sram_access"]
                self.assertEqual(sram["status"], "observed")
                self.assertEqual(sram["value"], {"start": 0x200000, "end": 0x210000})
                self.assertEqual(sram["evidence"][0]["address"], 0x200010)


if __name__ == "__main__":
    unittest.main()
