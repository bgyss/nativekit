#!/usr/bin/env python3
"""Actual-core capture checks and planted failure controls (no commercial ROM)."""
import json
import runpy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave
import zlib

from capture_fixture import write_rom

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "consoles/nes/tools/nnes-capture"


class ActualCapture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nnes-capture-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.rom = self.root / "authored.nes"
        write_rom(self.rom)

    def command(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)],
                              capture_output=True, text=True)

    def capture(self, name, *args, ok=True):
        out = self.root / name
        result = self.command("run", "--rom", self.rom, "--out", out,
                              "--frames", 8, *args)
        self.assertEqual(result.returncode == 0, ok, result.stderr)
        manifest = json.loads((out / "manifest.json").read_text())
        self.assertEqual(manifest["ok"], ok)
        return out, manifest

    def test_repeated_captures_are_identical_and_include_typed_bus(self):
        left, first = self.capture("left", "--trace")
        right, second = self.capture("right", "--trace")
        for name in ("ram.bin", "oam.bin", "vram.bin", "final.png", "audio.wav", "bus.jsonl", "state.json"):
            self.assertEqual((left / "artifacts" / name).read_bytes(),
                             (right / "artifacts" / name).read_bytes(), name)
        self.assertEqual(first["validation"]["completion"]["frames"], 8)
        with (left / "artifacts/bus.jsonl").open() as source:
            operations = {json.loads(line).get("operation") for line in source}
        # The pinned debugger delivers opcode callbacks but excludes operand
        # callbacks; operand cycles remain in profile gaps as native work.
        self.assertTrue({"execOpCode", "dummyRead", "read", "write"} <= operations)
        with wave.open(str(left / "artifacts/audio.wav")) as audio:
            self.assertTrue(any(audio.readframes(audio.getnframes())))
        self.assertEqual(self.command("validate", left).returncode, 0)
        ram = left / "artifacts/ram.bin"
        ram.write_bytes(bytes(2048))
        result = self.command("validate", left)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hash mismatch", result.stderr)

    def test_start_input_reaches_emulated_controller(self):
        out, _ = self.capture("start", "--scenario", "start", "--start-frame", 3,
                               "--start-frames", 6)
        ram = (out / "artifacts/ram.bin").read_bytes()
        self.assertGreater(ram[0x10], 0)
        self.assertEqual(ram[0x20], 0x10)
        plain, _ = self.capture("plain")
        self.assertEqual((plain / "artifacts/ram.bin").read_bytes()[0x20], 0)

    def test_gameplay_releases_buttons_and_addresses_player_two(self):
        inputs = {str(frame): {"1": ["A"]} for frame in range(3, 8)}
        inputs["8"] = {"2": ["B"]}
        schedule = self.root / "input.json"
        schedule.write_text(json.dumps(inputs))
        out, _ = self.capture("gameplay", "--scenario", "gameplay", "--input", schedule)
        ram = (out / "artifacts/ram.bin").read_bytes()
        self.assertEqual(ram[0x20], 0)  # player 1 released on the final frame
        self.assertEqual(ram[0x21], 0x40)  # player 2 B is sampled independently

    def test_truncated_media_fails_structural_validation(self):
        out, _ = self.capture("truncated")
        api = runpy.run_path(str(TOOL))
        for name, keep in (("final.png", 33), ("final.mss", 10), ("audio.wav", 48)):
            path = out / "artifacts" / name
            original = path.read_bytes()
            path.write_bytes(original[:keep])
            with self.assertRaises((ValueError, EOFError, wave.Error, zlib.error)):
                api["validate_artifacts"](out, json.loads((out / "manifest.json").read_text())["rom"]["sha1"], [])
            path.write_bytes(original)

    def test_script_error_and_never_finished_collector_fail(self):
        for code, name in (("error('planted script failure')", "syntax"),
                           ("emu.addEventCallback(function() error('callback failure') end, emu.eventType.endFrame)", "callback"),
                           ("emu.log('no completion')", "unfinished")):
            script = self.root / f"{name}.lua"
            script.write_text(code)
            out, result = self.capture(name, "--scenario", "script", "--lua", script, ok=False)
            self.assertIn("failed", result["error"])
        self.assertIn("planted script failure", (self.root / "syntax/stderr.log").read_text())

    def test_timeout_event_cap_and_missing_custom_output_fail(self):
        script = self.root / "loop.lua"
        script.write_text("while true do end")
        _, result = self.capture("timeout", "--scenario", "script", "--lua", script,
                                 "--timeout", 1, ok=False)
        self.assertIn("timeout", result["error"])
        self.capture("events", "--trace", "--max-events", 2, ok=False)
        self.capture("missing", "--expect-jsonl", "missing.jsonl", ok=False)

    def test_profile_normalizes_and_custom_lua_stops(self):
        out, manifest = self.capture("profile", "--scenario", "profile", "--warmup", 0)
        raw = out / "artifacts/profile-raw.jsonl"
        result = subprocess.run([sys.executable, str(TOOL.parent / "nnes-profile"),
                                 "normalize", str(raw), "--out", str(self.root / "normalized.jsonl")],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(manifest["validation"]["completion"]["reason"], "script_stop")
        script = self.root / "custom.lua"
        script.write_text("emu.addEventCallback(function() emu.stop(0) end, emu.eventType.endFrame)")
        out, manifest = self.capture("custom", "--scenario", "script", "--lua", script)
        self.assertEqual(manifest["validation"]["completion"]["frames"], 1)
        script.write_text("emu.addMemoryCallback(function() emu.stop(0) end, emu.callbackType.exec, 0x8000, 0x8000, emu.cpuType.nes, emu.memType.nesMemory)")
        out, manifest = self.capture("midframe", "--scenario", "script", "--lua", script)
        self.assertEqual(manifest["validation"]["completion"]["frames"], 0)


if __name__ == "__main__":
    unittest.main()
