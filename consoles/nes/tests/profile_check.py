#!/usr/bin/env python3
"""Regression checks for nnes-profile trace normalization."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


PROFILE_TOOL = Path(__file__).resolve().parents[1] / "tools" / "nnes-profile"


class NormalizeTraceTests(unittest.TestCase):
    def test_classifies_memory_kinds_and_removes_dummy_cycles(self) -> None:
        header = {
            "type": "header",
            "format": "nnes-cpu-op-trace",
            "version": 1,
            "rom_sha1": "1" * 40,
            "reference": "test fixture",
            "operation_types_included": True,
            "cycle_basis": "cpu_cycles_including_dma_stalls",
            "kind_map": "nnes-profile-kinds-v1",
        }
        accesses = [
            {"type": "access", "frame": 0, "cycle": 0,
             "operation": "read", "address": 0x0002, "kind": "wram"},
            {"type": "access", "frame": 0, "cycle": 1,
             "operation": "dummy_read", "address": 0xFF7C},
            {"type": "access", "frame": 0, "cycle": 2,
             "operation": "opcode", "address": 0xFF78},
            {"type": "access", "frame": 0, "cycle": 3,
             "operation": "write", "address": 0x6000, "kind": "cart_ram"},
        ]

        with tempfile.TemporaryDirectory() as temporary:
            raw = Path(temporary) / "raw.jsonl"
            normalized = Path(temporary) / "normalized.jsonl"
            raw.write_text("".join(json.dumps(item) + "\n"
                                     for item in [header, *accesses]))
            run = subprocess.run(
                [sys.executable, str(PROFILE_TOOL), "normalize", str(raw),
                 "--out", str(normalized)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(run.returncode, 0, run.stderr)
            records = [json.loads(line) for line in normalized.read_text().splitlines()]

        self.assertTrue(records[0]["dummy_access_cycles_removed"])
        self.assertEqual(records[1]["counts"], [1, 0, 0, 0, 0, 0, 1, 0])
        self.assertEqual(records[1]["gap_cycles"], 1)


if __name__ == "__main__":
    unittest.main()
