#!/usr/bin/env python3
"""Check the MD synthetic golden comparator catches changes in key outputs."""
import copy
import importlib.machinery
import importlib.util
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
GOLDEN = ROOT / "consoles/md/tests/golden.py"


class GoldenCheck(unittest.TestCase):
    def load_golden(self):
        if not GOLDEN.is_file():
            self.fail("MD synthetic golden comparator is not implemented")
        loader = importlib.machinery.SourceFileLoader("md_golden", str(GOLDEN))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module

    def test_planted_pixel_bus_and_pcm_changes_are_detected(self):
        golden = self.load_golden()
        expected = {
            "format": "nmd-synthetic-golden-1",
            "cases": {
                "render": {"status": "match", "outputs": {"frame_000001.ppm": "a" * 64}},
                "bus": {"status": "match", "outputs": {"oracle/bus.trace": "b" * 64}},
                "sound": {"status": "match", "outputs": {"native-mixed.raw": "c" * 64}},
            },
        }
        for case_name, path in (("render", "frame_000001.ppm"),
                                ("bus", "oracle/bus.trace"),
                                ("sound", "native-mixed.raw")):
            actual = copy.deepcopy(expected)
            actual["cases"][case_name]["outputs"][path] = "d" * 64
            differences = golden.compare_manifests(expected, actual)
            self.assertTrue(any(case_name in difference and path in difference
                                for difference in differences), differences)

    def test_added_or_removed_case_is_detected(self):
        golden = self.load_golden()
        expected = {"format": "nmd-synthetic-golden-1", "cases": {}}
        actual = {"format": "nmd-synthetic-golden-1",
                  "cases": {"io_pad6_sequence": {"status": "match", "outputs": {}}}}
        self.assertTrue(golden.compare_manifests(expected, actual))

    def test_output_hashes_ignore_workspace_paths_in_process_streams(self):
        golden = self.load_golden()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = root / "first" / "bus_map"
            second = root / "second" / "bus_map"
            first.mkdir(parents=True)
            second.mkdir(parents=True)
            (first / "candidate.stdout").write_text("system_dir: %s/oracle\n" % first)
            (second / "candidate.stdout").write_text("system_dir: %s/oracle\n" % second)
            self.assertEqual(golden.snapshot_tree(first), golden.snapshot_tree(second))


if __name__ == "__main__":
    unittest.main(verbosity=2)
