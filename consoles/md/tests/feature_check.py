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
YM_PORT_M68K_TAG = 0x80000000
YM_PORT_Z80_TAG = 0x40000000


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
                         {"enabled": True, "ever_enabled": True,
                          "double_resolution": True, "register_value": 0x0e})
        self.assertEqual(features["shadow_highlight"]["value"]["enabled"], True)
        self.assertEqual(features["hint_enabled"]["value"]["enabled"], True)
        for name in ("dma_68k_to_vdp", "dma_fill", "dma_copy"):
            self.assertEqual(features[name]["status"], "observed")
        self.assertEqual(features["hint_lines"]["status"], "observed")
        self.assertEqual(features["hint_lines"]["evidence"][0]["address"], 120)

    def test_dma_fill_accepts_byte_and_word_destination_events(self):
        events = [Event(10, VDP, DMA, 1, 0x10000001, 0xa5),
                  Event(20, VDP, DMA, 2, 0x10000002, 0x1234)]
        fill = extract_features(manifest(events), events)["features"]["dma_fill"]
        self.assertEqual(fill["status"], "observed")
        self.assertEqual([event["width"] for event in fill["evidence"]], [1, 2])

    def test_dma_feature_evidence_omits_rom_payload_values(self):
        events = [Event(10, VDP, DMA, 2, 0x400, 0xC0DE),
                  Event(20, VDP, DMA, 1, 0x10000001, 0xA5),
                  Event(30, VDP, DMA, 1, 0x20000004, 0x5A)]
        features = extract_features(manifest(events), events)["features"]
        for name in ("dma_68k_to_vdp", "dma_fill", "dma_copy"):
            with self.subTest(feature=name):
                self.assertEqual(features[name]["status"], "observed")
                self.assertTrue(features[name]["evidence"])
                self.assertTrue(all("value" not in event for event in features[name]["evidence"]))

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

    def test_vdp_mode_classifies_all_mode4_and_mode5_bit_combinations(self):
        for mode4, mode5, expected in ((0, 0, "inactive"), (4, 0, "mode4"),
                                       (0, 4, "mode5"), (4, 4, "mode5")):
            with self.subTest(mode4=mode4, mode5=mode5):
                events = [control(10, 0, mode4), control(20, 1, mode5)]
                mode = extract_features(manifest(events), events)["features"]["vdp_mode"]
                self.assertEqual(mode["status"], "observed")
                self.assertEqual(mode["value"]["mode"], expected)

    def test_second_control_word_is_not_effective_register_write(self):
        events = [control(10, 1, 0x04), Event(20, VDP, WRITE, 2, 4, 0x4000),
                  Event(30, VDP, WRITE, 2, 4, 0x8C0E)]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["interlace"]["status"], "not_observed")
        self.assertFalse(any(write["register"] == 12 for write in features["vdp_registers"]["evidence"]))

    def test_data_port_access_cancels_pending_control_word(self):
        events = [control(10, 1, 0x04), Event(20, VDP, WRITE, 2, 4, 0x4000),
                  Event(25, VDP, WRITE, 2, 0, 0x1234), Event(30, VDP, WRITE, 2, 4, 0x8C0E)]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["interlace"]["status"], "observed")
        self.assertTrue(features["interlace"]["value"]["enabled"])

    def test_register_history_and_unique_hint_lines_are_reported(self):
        events = [
            control(10, 0, 0x10), control(20, 0, 0),
            control(30, 12, 0x0E), control(40, 12, 0x01),
            Event(50, VDP, IRQ, 1, 120, 1), Event(60, VDP, IRQ, 1, 121, 1),
            Event(70, VDP, IRQ, 1, 120, 1),
        ]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["vdp_registers"]["value"]["register_ids"], [0, 12])
        self.assertEqual(features["vdp_registers"]["value"]["last_values"], {"0": 0, "12": 1})
        self.assertEqual(features["hint_lines"]["value"]["lines"], [120, 121])
        self.assertEqual(features["interlace"]["value"]["enabled"], False)
        self.assertEqual(features["interlace"]["value"]["ever_enabled"], True)
        self.assertEqual(features["shadow_highlight"]["value"]["enabled"], False)
        self.assertEqual(features["shadow_highlight"]["value"]["ever_enabled"], True)
        self.assertEqual(features["hint_enabled"]["value"]["enabled"], False)
        self.assertEqual(features["hint_enabled"]["value"]["ever_enabled"], True)

    def test_z80_dac_and_direct_68000_ym_evidence(self):
        events = [
            Event(100, Z80, WRITE, 1, 0x4000, 0x2A),
            Event(145, Z80, WRITE, 1, 0x4001, 0x40),
            Event(100, YM2612, WRITE, 1, YM_PORT_Z80_TAG | 0, 0x2A),
            Event(145, YM2612, WRITE, 1, YM_PORT_Z80_TAG | 1, 0x40),
            Event(168, YM2612, WRITE, 1, 0x2A, 0x40),
            Event(200, Z80, WRITE, 1, 0x4000, 0x2A),
            Event(245, Z80, WRITE, 1, 0x4001, 0x50),
            Event(200, YM2612, WRITE, 1, YM_PORT_Z80_TAG | 0, 0x2A),
            Event(245, YM2612, WRITE, 1, YM_PORT_Z80_TAG | 1, 0x50),
            Event(252, YM2612, WRITE, 1, 0x2A, 0x50),
            Event(300, M68K, WRITE, 1, 0xA04000, 0x40),
            Event(328, M68K, WRITE, 1, 0xA04001, 0x7F),
            Event(328, YM2612, WRITE, 1, YM_PORT_M68K_TAG | 1, 0x7F),
        ]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["z80_dac_streaming"]["status"], "observed")
        self.assertEqual(features["direct_68k_ym_writes"]["status"], "observed")
        for name in ("z80_dac_streaming", "direct_68k_ym_writes"):
            for evidence in features[name]["evidence"]:
                for record in evidence.values():
                    self.assertNotIn("value", record)

    def test_direct_68000_ym_requires_a_committed_ym_port_write(self):
        cpu_write = Event(300, M68K, WRITE, 1, 0xA04000, 0x30)
        events = [cpu_write]
        features = extract_features(manifest(events), events)["features"]["direct_68k_ym_writes"]
        self.assertEqual(features["status"], "not_observed")

        ym_write = Event(300, YM2612, WRITE, 1, YM_PORT_M68K_TAG | 0, 0x30)
        events.append(ym_write)
        features = extract_features(manifest(events), events)["features"]["direct_68k_ym_writes"]
        self.assertEqual(features["status"], "observed")
        self.assertEqual(features["evidence"][0]["cpu_bus"]["address"], cpu_write.address)

        word_write = Event(400, M68K, WRITE, 2, 0xA04000, 0x3000)
        events = [word_write]
        features = extract_features(manifest(events), events)["features"]["direct_68k_ym_writes"]
        self.assertEqual(features["status"], "not_observed")
        events.append(Event(400, YM2612, WRITE, 1, YM_PORT_M68K_TAG | 0, 0x30))
        features = extract_features(manifest(events), events)["features"]["direct_68k_ym_writes"]
        self.assertEqual(features["status"], "observed")
        self.assertEqual(features["evidence"][0]["cpu_bus"]["width"], 2)

    def test_direct_68000_ym_does_not_pair_a_z80_port_write(self):
        events = [Event(300, M68K, WRITE, 1, 0xA04000, 0x30),
                  Event(300, Z80, WRITE, 1, 0x4000, 0x30),
                  Event(300, YM2612, WRITE, 1, YM_PORT_Z80_TAG, 0x30)]
        direct = extract_features(manifest(events), events)["features"]["direct_68k_ym_writes"]
        self.assertEqual(direct["status"], "not_observed")

    def test_single_dac_sample_and_missing_capability_are_not_promoted(self):
        events = [
            Event(100, Z80, WRITE, 1, 0x4000, 0x2A),
            Event(145, Z80, WRITE, 1, 0x4001, 0x40),
            Event(100, YM2612, WRITE, 1, YM_PORT_Z80_TAG | 0, 0x2A),
            Event(145, YM2612, WRITE, 1, YM_PORT_Z80_TAG | 1, 0x40),
            Event(168, YM2612, WRITE, 1, 0x2A, 0x40),
        ]
        bounded = extract_features(manifest(events), events)["features"]["z80_dac_streaming"]
        self.assertEqual(bounded["status"], "not_observed")
        missing = [cap for cap in CAPS if cap != "ym2612_write"]
        unavailable = extract_features(manifest(events, missing), events)["features"]["z80_dac_streaming"]
        self.assertEqual(unavailable["status"], "unavailable")

    def test_six_button_probe_requires_three_rising_edges_and_extra_phase_read(self):
        events = [Event(10, M68K, WRITE, 1, 0xA10009, 0x40),
                  Event(20, M68K, WRITE, 1, 0xA10003, 0x00),
                  Event(30, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(40, M68K, WRITE, 1, 0xA10003, 0x00),
                  Event(50, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(60, M68K, WRITE, 1, 0xA10003, 0x00),
                  Event(70, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(80, M68K, READ, 1, 0xA10003, 0xFF)]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["six_button_probe"]["status"], "observed")

    def test_ordinary_pad_polling_does_not_probe_six_button_phase(self):
        events = [Event(10, M68K, WRITE, 1, 0xA10009, 0x40),
                  Event(20, M68K, WRITE, 1, 0xA10003, 0x00),
                  Event(30, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(40, M68K, READ, 1, 0xA10003, 0xFF),
                  Event(56050, M68K, WRITE, 1, 0xA10003, 0x00),
                  Event(56060, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(56070, M68K, READ, 1, 0xA10003, 0xFF)]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["six_button_probe"]["status"], "not_observed")

    def test_six_button_transitions_require_th_output_direction(self):
        events = [Event(clock, M68K, WRITE, 1, 0xA10003, value)
                  for clock, value in ((10, 0x00), (20, 0x40), (30, 0x00),
                                       (40, 0x40), (50, 0x00), (60, 0x40))]
        events.append(Event(70, M68K, READ, 1, 0xA10003, 0xFF))
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["six_button_probe"]["status"], "not_observed")

    def test_six_button_probe_does_not_count_th_output_enable_as_a_rise(self):
        events = [Event(10, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(20, M68K, WRITE, 1, 0xA10009, 0x40),
                  Event(30, M68K, WRITE, 1, 0xA10003, 0x00),
                  Event(40, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(50, M68K, WRITE, 1, 0xA10003, 0x00),
                  Event(60, M68K, WRITE, 1, 0xA10003, 0x40),
                  Event(70, M68K, READ, 1, 0xA10003, 0xFF)]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["six_button_probe"]["status"], "not_observed")

    def test_six_button_probe_normalizes_word_width_io_accesses(self):
        events = [Event(10, M68K, WRITE, 2, 0xA10008, 0x0040),
                  Event(20, M68K, WRITE, 2, 0xA10002, 0x0000),
                  Event(30, M68K, WRITE, 2, 0xA10002, 0x0040),
                  Event(40, M68K, WRITE, 2, 0xA10002, 0x0000),
                  Event(50, M68K, WRITE, 2, 0xA10002, 0x0040),
                  Event(60, M68K, WRITE, 2, 0xA10002, 0x0000),
                  Event(70, M68K, WRITE, 2, 0xA10002, 0x0040),
                  Event(80, M68K, READ, 2, 0xA10002, 0xFFFF)]
        features = extract_features(manifest(events), events)["features"]
        self.assertEqual(features["six_button_probe"]["status"], "observed")

    def test_malformed_filtered_scope_metadata_is_unavailable(self):
        events = [control(10, 0, 0x10), control(20, 1, 0x04)]
        bounded = manifest(events)
        bounded["trace_scope"] = "survey-feature-filter-v1"
        bounded["trace_filters"] = {"device_events": "all", "m68k_cpu_access": 3}
        features = extract_features(bounded, events)["features"]
        self.assertEqual(features["vdp_mode"]["status"], "unavailable")
        self.assertIn("filter metadata", features["vdp_mode"]["reason"])
        self.assertEqual(features["six_button_probe"]["status"], "unavailable")

    def test_feature_only_missing_scope_cannot_claim_full_coverage(self):
        events = [control(10, 0, 0x10), control(20, 1, 0x04)]
        bounded = manifest(events)
        bounded["capture_mode"] = "feature-only"
        result = extract_features(bounded, events)
        features = result["features"]
        self.assertEqual(result["trace_scope"], "unknown")
        self.assertEqual(features["vdp_mode"]["status"], "unavailable")
        self.assertIn("scope", features["vdp_mode"]["reason"])

    def test_cpu_stats_manifest_does_not_change_trace_features(self):
        events = [control(10, 0, 0x10), control(20, 1, 0x04)]
        original = manifest(events)
        profiled = manifest(events, cpu_stats_file="cpu.stats", cpu_stats_format=1,
                            cpu_stats_frames=4,
                            cpu_stats_method="per-CPU event clock deltas; exploratory")
        self.assertEqual(extract_features(profiled, events), extract_features(original, events))

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
        self.assertNotIn("value", known["evidence"][0])

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
