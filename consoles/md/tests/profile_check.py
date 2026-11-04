#!/usr/bin/env python3
"""Check candidate CPU statistics and generated nmd cost profiles."""
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
ORACLE = ROOT / "consoles/md/tests/oracle"
RUNNER = ORACLE / "run.py"
TOOL = ROOT / "consoles/md/tools/nmd-profile"
sys.path.insert(0, str(ORACLE))
import trace  # noqa: E402


def candidate_core():
    configured = os.environ.get("NMD_ORACLE_CORE")
    if configured:
        return pathlib.Path(configured)
    extension = "dylib" if sys.platform == "darwin" else "so"
    return ROOT / "build/toolchains/blastem" / ("libblastem." + extension)


def fixture_rom(path):
    rom = bytearray(32768)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT PROFILE".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT PROFILE".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x202] = bytes.fromhex("60fe")
    path.write_bytes(rom)
    return bytes(rom)


class ProfileTests(unittest.TestCase):
    def load_tool(self):
        if not TOOL.is_file():
            self.fail("nmd-profile is not implemented")
        loader = importlib.machinery.SourceFileLoader("nmd_profile_tool", str(TOOL))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module

    def test_stats_rows_fit_known_weights(self):
        tool = self.load_tool()
        m68k_rows = []
        for frame, (rom_reads, ram_writes) in enumerate(((20, 10), (40, 5), (10, 30)), 1):
            counts = [0] * (2 * tool.NMD_KINDS)
            counts[tool.M68K_KIND_NAMES.index("rom")] = rom_reads
            counts[tool.NMD_KINDS + tool.M68K_KIND_NAMES.index("work ram")] = ram_writes
            work = 2.5 * rom_reads + 3.0 * ram_writes
            bus = 28 * (rom_reads + ram_writes)
            m68k_rows.append((frame, counts, work, bus))

        fitted = tool.fit_cpu(m68k_rows, tool.NMD_KINDS, min_accesses=1, active_clocks=0)
        self.assertAlmostEqual(fitted["weights"][tool.M68K_KIND_NAMES.index("rom")], 2.5)
        self.assertAlmostEqual(
            fitted["weights"][tool.NMD_KINDS + tool.M68K_KIND_NAMES.index("work ram")], 3.0)
        perturbed_rows = [(frame, list(counts), work, bus)
                          for frame, counts, work, bus in m68k_rows]
        frame, counts, work, bus = perturbed_rows[0]
        perturbed_rows[0] = (frame, counts, work + 5, bus)
        perturbed = tool.fit_cpu(perturbed_rows, tool.NMD_KINDS,
                                 min_accesses=1, active_clocks=0)
        self.assertNotAlmostEqual(
            perturbed["weights"][tool.M68K_KIND_NAMES.index("rom")], 2.5, places=2)

        z80_rows = []
        for frame, (ram_reads, psg_writes) in enumerate(((20, 10), (40, 5), (10, 30)), 1):
            counts = [0] * (2 * tool.NMD_Z80_KINDS)
            counts[tool.Z80_KIND_NAMES.index("z80 ram")] = ram_reads
            counts[tool.NMD_Z80_KINDS + tool.Z80_KIND_NAMES.index("z80 psg")] = psg_writes
            work = 30.0 * ram_reads + 45.0 * psg_writes
            bus = 45 * (ram_reads + psg_writes)
            z80_rows.append((frame, counts, work, bus))
        fitted = tool.fit_cpu(z80_rows, tool.NMD_Z80_KINDS, min_accesses=1, active_clocks=0)
        self.assertAlmostEqual(fitted["weights"][tool.Z80_KIND_NAMES.index("z80 ram")], 30.0)
        self.assertAlmostEqual(
            fitted["weights"][tool.NMD_Z80_KINDS + tool.Z80_KIND_NAMES.index("z80 psg")], 45.0)

    def test_poll_and_gap_filters(self):
        self.assertTrue(hasattr(trace, "CPUStats"), "candidate CPU stats are not implemented")
        stats = trace.CPUStats()
        stats.begin_frame(1)
        clock = 0
        for _ in range(65):
            clock += 28
            stats.add(trace.Event(clock, trace.M68K, trace.READ, 1, 0x100, 0))
        clock += 28
        stats.add(trace.Event(clock, trace.M68K, trace.WRITE, 1, 0x100, 0))
        clock += 1025
        stats.add(trace.Event(clock, trace.M68K, trace.FETCH, 2, 0x200, 0))
        clock += 28
        stats.add(trace.Event(clock, trace.M68K, trace.READ, 1, 0x100, 0))
        clock += 28
        stats.add(trace.Event(clock, trace.M68K, trace.WRITE, 1, 0x100, 0))
        stats.end_frame()

        row = next(row for row in stats.rows() if row.cpu == "m68k")
        kinds = trace.M68K_KIND_COUNT
        self.assertEqual(sum(row.counts[:kinds]), 0)
        self.assertEqual(sum(row.counts[kinds:]), 2)

    def test_m68k_longword_matches_two_native_bus_accesses(self):
        stats = trace.CPUStats()
        stats.begin_frame(1)
        stats.add(trace.Event(56, trace.M68K, trace.READ, 4, 0x100, 0))
        stats.add(trace.Event(112, trace.M68K, trace.WRITE, 4, 0x200, 0))
        stats.end_frame()
        row = next(row for row in stats.rows() if row.cpu == "m68k")
        self.assertEqual(row.counts[0], 2)
        self.assertEqual(row.counts[trace.M68K_KIND_COUNT], 2)
        self.assertEqual(row.bus_clocks, 112)

    def test_decomp_count_ratios_keep_processors_distinct(self):
        tool = self.load_tool()
        m68k = [0] * (2 * tool.NMD_KINDS)
        z80 = [0] * (2 * tool.NMD_Z80_KINDS)
        m68k[tool.M68K_KIND_NAMES.index("rom")] = 4
        z80[tool.Z80_KIND_NAMES.index("z80 ram")] = 3
        reference = [trace.CPUStatsRow(1, "m68k", m68k, 0, 0),
                     trace.CPUStatsRow(1, "z80", z80, 0, 0)]
        native_counts = (m68k[:tool.NMD_KINDS] + z80[:tool.NMD_Z80_KINDS] +
                         m68k[tool.NMD_KINDS:] + z80[tool.NMD_Z80_KINDS:])
        native_counts[0] = 2
        comparison = tool.compare_counts(reference, {1: native_counts})
        ratios = dict(comparison["ratios"])
        self.assertEqual(ratios["read.m68k.rom"], "0.5000")
        self.assertEqual(ratios["read.z80.z80_ram"], "1.0000")
        self.assertEqual(len(comparison["mismatches"]), 1)

    def test_cpu_stats_reader_rejects_missing_vblank_rows(self):
        zero_m68k = trace.CPUStatsRow(1, "m68k", [0] * (2 * trace.M68K_KIND_COUNT), 0, 0)
        zero_z80 = trace.CPUStatsRow(1, "z80", [0] * (2 * trace.Z80_KIND_COUNT), 0, 0)
        later_m68k = trace.CPUStatsRow(3, "m68k", [0] * (2 * trace.M68K_KIND_COUNT), 0, 0)
        later_z80 = trace.CPUStatsRow(3, "z80", [0] * (2 * trace.Z80_KIND_COUNT), 0, 0)
        sidecar = io.StringIO()
        trace.write_cpu_stats(sidecar, (zero_m68k, zero_z80, later_m68k, later_z80))
        with self.assertRaisesRegex(ValueError, "each frame"):
            trace.read_cpu_stats(io.StringIO(sidecar.getvalue()))

    def test_profile_text_and_c_generation(self):
        tool = self.load_tool()
        m68k = [[0] * tool.NMD_KINDS for _ in range(2)]
        z80 = [[0] * tool.NMD_Z80_KINDS for _ in range(2)]
        m68k[0][tool.M68K_KIND_NAMES.index("rom")] = 3
        z80[1][tool.Z80_KIND_NAMES.index("z80 psg")] = 10
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = root / "profile.txt"
            source = root / "profile.c"
            tool.write_profile(profile, rom_hashes=["a" * 64], candidate_sha256="b" * 64,
                               model="md1va6", region="E", m68k_tenths=m68k,
                               z80_tenths=z80, source_sha256="-", checked=False)
            tool.write_c(source, "fixture", m68k, z80)
            text = profile.read_text()
            generated = source.read_text()
            self.assertIn("nmd-cost-profile 1", text)
            self.assertIn("cpu m68k read rom 0.3", text)
            self.assertIn("cpu z80 write z80 psg 1.0", text)
            self.assertIn('#include "nmd/nmd.h"', generated)
            self.assertIn("NMD_COST_PROFILE_VERSION", generated)
            self.assertEqual(tool.c_identifier("_private"), "nmd__private")
            compiler = shutil.which("cc") or shutil.which("clang")
            self.assertIsNotNone(compiler, "C compiler is required to check generated profiles")
            subprocess.run([compiler, "-std=c11", "-fsyntax-only", "-I",
                            str(ROOT / "consoles/md/include"), str(source)], check=True)

    def test_profile_source_hash_is_deterministic(self):
        tool = self.load_tool()
        with tempfile.TemporaryDirectory() as temp:
            source = pathlib.Path(temp)
            (source / "z.c").write_text("z\n")
            (source / "sub").mkdir()
            (source / "sub/a.c").write_text("a\n")
            first = tool.source_hash(source)
            second = tool.source_hash(source)
            self.assertEqual(first, second)
            (source / "sub/a.c").write_text("changed\n")
            self.assertNotEqual(first, tool.source_hash(source))

    def test_profile_stale_source_warning(self):
        tool = self.load_tool()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "source"
            source.mkdir()
            file = source / "game.c"
            file.write_text("before\n")
            profile = root / "profile.txt"
            m68k = [[0] * tool.NMD_KINDS for _ in range(2)]
            z80 = [[0] * tool.NMD_Z80_KINDS for _ in range(2)]
            tool.write_profile(profile, rom_hashes=[], candidate_sha256="b" * 64,
                               model="md1va3", region="U", m68k_tenths=m68k,
                               z80_tenths=z80, source_sha256=tool.source_hash(source), checked=True)
            file.write_text("after\n")
            output = io.StringIO()
            status = tool.check_profile(profile, source, output)
            self.assertEqual(status, 1)
            self.assertIn("warning", output.getvalue())
            self.assertIn("stale", output.getvalue())

    def test_profile_reports_no_rom_paths(self):
        tool = self.load_tool()
        core = candidate_core()
        if not core.is_file():
            self.skipTest("pinned candidate core has not been built")
        decomp = ROOT / "build/null/tests/profile_decomp_fixture"
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            rom = root / "private-rom-name.bin"
            contents = fixture_rom(rom)
            input_path = root / "private-input-name.json"
            input_path.write_text(json.dumps({
                "1": {"1": ["START"], "2": ["B"]},
                "2": {"1": ["START"], "2": ["B"]},
            }))
            output = root / "profile-output"
            result = subprocess.run(
                [sys.executable, str(TOOL), "measure", str(rom), "--core", str(core),
                 "--frames", "2", "--name", "fixture", "--out", str(output),
                 "--input", str(input_path), "--decomp", str(decomp)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            digest = hashlib.sha256(contents).hexdigest()
            generated = "\n".join((output / name).read_text()
                                    for name in ("profile.txt", "profile.c", "report.txt"))
            self.assertIn(digest, (output / "profile.txt").read_text())
            self.assertIn("checked yes", (output / "profile.txt").read_text())
            self.assertIn("native --counts comparison", (output / "report.txt").read_text())
            self.assertIn("read.m68k.rom", (output / "profile.txt").read_text())
            self.assertNotIn(str(root), generated)
            self.assertNotIn("private-rom-name.bin", generated)
            self.assertNotIn(contents.decode("latin1"), generated)

    def test_runner_emits_cpu_stats_sidecar(self):
        core = candidate_core()
        if not core.is_file():
            self.skipTest("pinned candidate core has not been built")
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            rom = root / "fixture.bin"
            fixture_rom(rom)
            output = root / "oracle-output"
            result = subprocess.run(
                [sys.executable, str(RUNNER), "--core", str(core), "--rom", str(rom),
                 "--frames", "2", "--out", str(output), "--trace", "--cpustats"],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((output / "cpu.stats").is_file())
            with (output / "cpu.stats").open("r") as stats_file:
                rows = list(trace.read_cpu_stats(stats_file))
            self.assertEqual([(row.frame, row.cpu) for row in rows],
                             [(1, "m68k"), (1, "z80"), (2, "m68k"), (2, "z80")])
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["cpu_stats_frames"], 2)
            self.assertEqual(manifest["cpu_stats_file"], "cpu.stats")


if __name__ == "__main__":
    unittest.main(verbosity=2)
