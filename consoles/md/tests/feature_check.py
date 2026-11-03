#!/usr/bin/env python3
"""Test conservative feature extraction from bounded candidate traces."""
import pathlib
import json
import subprocess
import sys
import tempfile
import unittest

root = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / "consoles/md/tools"))
sys.path.insert(0, str(root / "consoles/md/tests/oracle"))
from features import FeatureError, extract_features
from trace import Event, M68K, Z80, VDP, YM2612, FETCH, READ, WRITE, DMA, IRQ, write as write_trace


CAPS = [
    "m68k_cpu_access", "z80_cpu_access", "vdp_port", "vdp_68k_dma_source",
    "vdp_dma_fill", "vdp_dma_copy", "vdp_hint_lines", "ym2612_write", "ym2612_port_write", "psg_write",
]


def manifest(events, capabilities=None, **extra):
    result = {
        "core_sha256": "a" * 64,
        "frames": 4,
        "trace_capabilities": CAPS if capabilities is None else capabilities,
        "trace_events": len(events),
        "input": {},
    }
    result.update(extra)
    return result


def control(clock, register, value):
    return Event(clock, VDP, WRITE, 2, 4, 0x8000 | (register << 8) | value)


class FeatureCheck(unittest.TestCase):
    def test_register_modes_dma_and_interrupt_limit(self):
        events = [
            control(100, 0, 0x14),
            control(200, 1, 0x04),
            control(300, 12, 0x0E),
            Event(400, VDP, DMA, 2, 0x400, 0xA55A),
            Event(500, VDP, DMA, 1, 0x10000001, 0xA5),
            Event(600, VDP, DMA, 1, 0x20000004, 0x5A),
            Event(700, VDP, IRQ, 1, 120, 1),
        ]
        result = extract_features(manifest(events), events)
        features = result["features"]
        self.assertEqual(features["vdp_registers"]["status"], "observed")
        self.assertEqual(features["vdp_mode"]["value"]["mode"], "mode5")
        self.assertEqual(features["interlace"]["value"],
                         {"enabled": True, "double_resolution": True, "register_value": 0x0e})
        self.assertEqual(features["shadow_highlight"]["value"]["enabled"], True)
        self.assertEqual(features["hint_enabled"]["value"]["enabled"], True)
        for name in ("dma_68k_to_vdp", "dma_fill", "dma_copy"):
            self.assertEqual(features[name]["status"], "observed")
        self.assertEqual(features["hint_lines"]["status"], "observed")
        self.assertEqual(features["hint_lines"]["evidence"][0]["address"], 120)

    def test_unobserved_is_distinct_from_unavailable(self):
        events = []
        result = extract_features(manifest(events, ["m68k_cpu_access", "vdp_port"]), events)
        features = result["features"]
        self.assertEqual(features["vdp_registers"]["status"], "not_observed")
        self.assertEqual(features["dma_fill"]["status"], "unavailable")
        self.assertEqual(features["hint_lines"]["status"], "unavailable")
        self.assertEqual(features["sram_access"]["status"], "unavailable")

    def test_vdp_mode_is_unavailable_without_port_trace_capability(self):
        cases = ([], [control(10, 0, 0x04), control(20, 1, 0x04)])
        for events in cases:
            with self.subTest(event_count=len(events)):
                features = extract_features(manifest(events, ["m68k_cpu_access"]), events)["features"]
                self.assertEqual(features["vdp_mode"]["status"], "unavailable")
                self.assertIn("vdp_port", features["vdp_mode"]["reason"])

    def test_z80_dac_and_direct_68000_ym_evidence(self):
        events = [
            Event(100, Z80, WRITE, 1, 0x4000, 0x2A),
            Event(145, Z80, WRITE, 1, 0x4001, 0x40),
            Event(100, YM2612, WRITE, 1, 0, 0x2A),
            Event(145, YM2612, WRITE, 1, 1, 0x40),
            Event(168, YM2612, WRITE, 1, 0x2A, 0x40),
            Event(200, Z80, WRITE, 1, 0x4000, 0x2A),
            Event(245, Z80, WRITE, 1, 0x4001, 0x50),
            Event(200, YM2612, WRITE, 1, 0, 0x2A),
            Event(245, YM2612, WRITE, 1, 1, 0x50),
            Event(252, YM2612, WRITE, 1, 0x2A, 0x50),
            Event(300, M68K, WRITE, 1, 0xA04000, 0x40),
            Event(328, M68K, WRITE, 1, 0xA04001, 0x7F),
            Event(328, YM2612, WRITE, 1, 1, 0x7F),
        ]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["z80_dac_streaming"]["status"], "observed")
        self.assertEqual(features["direct_68k_ym_writes"]["status"], "observed")

    def test_single_dac_sample_and_missing_capability_are_not_promoted(self):
        events = [
            Event(100, Z80, WRITE, 1, 0x4000, 0x2A),
            Event(145, Z80, WRITE, 1, 0x4001, 0x40),
            Event(100, YM2612, WRITE, 1, 0, 0x2A),
            Event(145, YM2612, WRITE, 1, 1, 0x40),
            Event(168, YM2612, WRITE, 1, 0x2A, 0x40),
        ]
        bounded = extract_features(manifest(events), events)["features"]["z80_dac_streaming"]
        self.assertEqual(bounded["status"], "not_observed")
        missing = [cap for cap in CAPS if cap != "ym2612_write"]
        unavailable = extract_features(manifest(events, missing), events)["features"]["z80_dac_streaming"]
        self.assertEqual(unavailable["status"], "unavailable")

    def test_six_button_probe_requires_a_four_state_poll_sequence(self):
        events = []
        clock = 100
        for value in (0x00, 0x40, 0x00, 0x40):
            events.append(Event(clock, M68K, WRITE, 1, 0xA10003, value))
            clock += 10
            events.append(Event(clock, M68K, READ, 1, 0xA10003, value ^ 0xFF))
            clock += 10
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["six_button_probe"]["status"], "observed")
        pair = events[:4]
        self.assertEqual(extract_features(manifest(pair), pair)["features"]["six_button_probe"]["status"],
                         "not_observed")

    def test_mapper_and_sram_evidence_needs_window_metadata(self):
        events = [
            Event(100, M68K, WRITE, 1, 0xA13000, 3),
            Event(200, M68K, READ, 1, 0x200010, 0xFF),
        ]
        unknown_map = extract_features(manifest(events), events)["features"]
        self.assertEqual(unknown_map["mapper_register_access"]["status"], "observed")
        self.assertEqual(unknown_map["sram_access"]["status"], "unavailable")
        known_map = manifest(events, sram_window={"start": 0x200000, "end": 0x210000})
        known = extract_features(known_map, events)["features"]["sram_access"]
        self.assertEqual(known["status"], "observed")
        self.assertEqual(known["evidence"][0]["address"], 0x200010)

    def test_incomplete_event_count_is_rejected(self):
        events = [Event(1, M68K, FETCH, 2, 0x200, 0x4E71)]
        broken = manifest(events)
        broken["trace_events"] = 2
        with self.assertRaises(FeatureError):
            extract_features(broken, events)

    def test_filtered_scope_does_not_claim_omitted_cpu_ranges_absent(self):
        events = [
            control(10, 1, 0x04),
            Event(20, M68K, WRITE, 1, 0xA04000, 0x30),
            Event(30, M68K, WRITE, 1, 0xA13000, 2),
            Event(40, M68K, READ, 1, 0xA10003, 0xFF),
        ]
        bounded = manifest(events)
        bounded["trace_scope"] = "survey-feature-filter-v1"
        bounded["trace_filters"] = {
            "device_events": ["all"],
            "m68k_cpu_access": ["pad_ports"],
            "z80_cpu_access": ["bank_window"],
        }
        features = extract_features(bounded, events)["features"]
        self.assertEqual(features["vdp_mode"]["status"], "unavailable")
        self.assertEqual(features["direct_68k_ym_writes"]["status"], "unavailable")
        self.assertEqual(features["mapper_register_access"]["status"], "unavailable")
        self.assertEqual(features["six_button_probe"]["status"], "not_observed")
        self.assertEqual(features["z80_dac_streaming"]["status"], "unavailable")

        bounded["sram_window"] = {"start": 0x200000, "end": 0x210000}
        sram_event = Event(50, M68K, READ, 1, 0x200010, 0xFF)
        bounded["trace_events"] = len(events) + 1
        features = extract_features(bounded, events + [sram_event])["features"]
        self.assertEqual(features["sram_access"]["status"], "unavailable")

    def test_cli_emits_path_free_json(self):
        events = [control(100, 1, 0x04)]
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            trace_path = directory / "bus.trace"
            manifest_path = directory / "manifest.json"
            output_path = directory / "features.json"
            with trace_path.open("w+b") as stream:
                write_trace(stream, events)
            manifest_path.write_text(json.dumps(manifest(events)))
            result = subprocess.run([sys.executable, str(root / "consoles/md/tools/features.py"),
                                     "--manifest", str(manifest_path), "--trace", str(trace_path),
                                     "--out", str(output_path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = output_path.read_text()
            self.assertNotIn(temp, report)
            self.assertEqual(json.loads(report)["features"]["vdp_mode"]["status"], "unavailable")


if __name__ == "__main__":
    unittest.main()
