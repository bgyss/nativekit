#!/usr/bin/env python3
"""Capture CLI contracts; no installed emulator is needed for these checks."""
import json
import runpy
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools" / "nnes-capture"


class CaptureContracts(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)],
                              capture_output=True, text=True)

    def test_help_and_doctor_do_not_launch_desktop(self):
        result = self.run_tool("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("doctor", result.stdout)
        result = self.run_tool("doctor", "--toolchain", "/missing/nnes-toolchain")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tools:nes", result.stderr)

    def test_rejects_nonempty_output_before_starting(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            marker = out / "keep.txt"
            marker.write_text("keep")
            result = self.run_tool("run", "--rom", "/missing.nes", "--out", out)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("empty", result.stderr)
            self.assertEqual(marker.read_text(), "keep")

    def test_rejects_bad_input_before_starting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rom = root / "test.nes"
            rom.write_bytes(b"NES\x1a" + bytes(12))
            inputs = root / "input.json"
            inputs.write_text('{"0":{"1":["TURBO"]}}')
            result = self.run_tool("run", "--rom", rom, "--out", root / "out",
                                   "--input", inputs)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("input", result.stderr.lower())

    def test_rejects_zero_frame_budget(self):
        result = self.run_tool("run", "--rom", "/missing.nes", "--out", "/unused",
                               "--frames", "0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("frames", result.stderr)

    def test_rejects_changed_toolchain_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "nnes-mesen").write_bytes(b"changed binary")
            (root / "identity.json").write_text(json.dumps({
                "format": "nnes-mesen-toolchain", "version": 1,
                "host_backends": [], "operation_types": True,
                "executable_sha256": "0" * 64}))
            result = self.run_tool("doctor", "--toolchain", root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("hash mismatch", result.stderr)

    def test_trace_validation_rejects_wrong_rom_missing_footer_and_reordering(self):
        validate = runpy.run_path(str(TOOL))["validate_jsonl"]
        header = {"type": "header", "format": "nnes-typed-bus-trace", "rom_sha1": "1" * 40}
        access = {"type": "access", "event": 0, "cycle": 1, "address": 0}
        footer = {"type": "footer", "access_count": 1}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bus.jsonl"
            def write(items):
                path.write_text("".join(json.dumps(item) + "\n" for item in items))
            write([header, access, footer])
            self.assertEqual(validate(path, "1" * 40)["access_count"], 1)
            with self.assertRaisesRegex(ValueError, "ROM"):
                validate(path, "2" * 40)
            write([header, access])
            with self.assertRaisesRegex(ValueError, "footer"):
                validate(path, "1" * 40)
            write([header, {**access, "event": 4}, footer])
            with self.assertRaisesRegex(ValueError, "order"):
                validate(path, "1" * 40)

    def test_setup_refuses_changed_or_added_source_but_allows_build_output(self):
        setup = runpy.run_path(str(TOOL.parents[3] / "tools/toolchains/setup_nes.py"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "original.cpp"
            source.write_text("original")
            recorded = {source.name: setup["digest"](source)}
            (root / "original.o").write_bytes(b"compiled")
            setup["check_sources"](root, recorded)
            source.write_text("changed")
            with self.assertRaisesRegex(ValueError, "edited source"):
                setup["check_sources"](root, recorded)
            source.write_text("original")
            (root / "extra.cpp").write_text("unrecorded")
            with self.assertRaisesRegex(ValueError, "added source"):
                setup["check_sources"](root, recorded)

    def test_missing_identity_inputs_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "nnes-mesen").write_bytes(b"binary")
            reference = json.loads((TOOL.parents[3] / "tools/toolchains/mesen.json").read_text())
            identity = {"format": "nnes-mesen-toolchain", "version": 1,
                        "source_commit": reference["source_commit"], "host_backends": [],
                        "operation_types": True, "inputs": {},
                        "executable_sha256": hashlib.sha256(b"binary").hexdigest()}
            (root / "identity.json").write_text(json.dumps(identity))
            self.assertNotEqual(self.run_tool("doctor", "--toolchain", root).returncode, 0)

    def test_changed_or_unattested_objects_are_rebuilt(self):
        setup = runpy.run_path(str(TOOL.parents[3] / "tools/toolchains/setup_nes.py"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            obj = root / "core.o"
            obj.write_bytes(b"unattested")
            setup["prepare_objects"](root)
            self.assertFalse(obj.exists())
            obj.write_bytes(b"good")
            (root / ".nativekit-objects.json").write_text(json.dumps(setup["object_hashes"](root)))
            setup["prepare_objects"](root)
            self.assertTrue(obj.exists())
            obj.write_bytes(b"tampered")
            setup["prepare_objects"](root)
            self.assertFalse(obj.exists())


if __name__ == "__main__":
    unittest.main()
