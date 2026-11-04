#!/usr/bin/env python3
"""Smoke-test the candidate runner with a newly generated cartridge."""
import hashlib
import json
import os
import pathlib
import shlex
import subprocess
import sys
import struct
import tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "oracle"))
from trace import M68K, Z80, VDP, YM2612, PSG, FETCH, READ, WRITE, DMA, IRQ, read as read_trace

root = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / "consoles/md/tools"))
from features import extract_features

YM_PORT_Z80_ADDR_TAG = 0x40000000
YM_PORT_M68K_ADDR_TAG = 0x80000000
runner = root / "consoles/md/tests/oracle/run.py"
extension = "dylib" if sys.platform == "darwin" else "so"
core = root / "build/toolchains/blastem" / ("libblastem." + extension)
compiler = shlex.split(os.environ.get("CC", "/usr/bin/clang" if sys.platform == "darwin" else "cc"))

with tempfile.TemporaryDirectory() as temp:
    base = pathlib.Path(temp)
    rom = bytearray(32768)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT SYNTHETIC".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT SYNTHETIC".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x202] = bytes.fromhex("60fe")
    cart = base / "fixture.bin"
    cart.write_bytes(rom)
    sys.path.insert(0, str(runner.parent))
    from run import _game_descriptor
    memory_game, memory_content = _game_descriptor(cart, False)
    if (memory_game.path is not None or memory_game.data is None or
            memory_game.size != len(rom) or memory_content.raw[:len(rom)] != bytes(rom)):
        sys.exit("oracle_check: memory-backed libretro content descriptor is invalid")
    path_game, path_content = _game_descriptor(cart, True)
    if (path_content is not None or path_game.path != str(cart.resolve()).encode() or
            path_game.data is not None or path_game.size != 0):
        sys.exit("oracle_check: full-path libretro content descriptor is invalid")
    outputs = []
    for index in range(2):
        out = base / str(index)
        result = subprocess.run([sys.executable, str(runner), "--core", str(core),
                                 "--rom", str(cart), "--frames", "3", "--out", str(out)],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            sys.exit("oracle_check: run failed: " + result.stderr)
        manifest = json.loads((out / "manifest.json").read_text())
        if str(base) in json.dumps(manifest):
            sys.exit("oracle_check: local path leaked into manifest")
        if manifest.get("core_sha256") != hashlib.sha256(core.read_bytes()).hexdigest():
            sys.exit("oracle_check: core digest missing or incorrect")
        required = {"blastem_model": "md1va3", "blastem_fm_dac": "zero_offset",
                    "blastem_gain": "0.0", "blastem_fm_gain": "0.0",
                    "blastem_psg_gain": "0.0", "blastem_overscan_top": "0",
                    "blastem_overscan_bottom": "0", "blastem_overscan_left": "0",
                    "blastem_overscan_right": "0"}
        if any(manifest["options"].get(key) != value for key, value in required.items()):
            sys.exit("oracle_check: required options were not fixed")
        if not set(manifest["options"]).issubset(manifest["queried_options"]):
            sys.exit("oracle_check: core did not request every fixed option")
        frames = [(out / f"frame_{frame_index:06}.ppm").read_bytes()
                  for frame_index in range(1, 4)]
        if (manifest["frames"] != 3 or manifest["frame_sizes"] != [[283, 243]] * 3
                or any(not frame.startswith(b"P6\n283 243\n255\n") for frame in frames)):
            sys.exit("oracle_check: missing frame")
        if manifest["sample_rate"] <= 0 or not (out / "audio.raw").stat().st_size:
            sys.exit("oracle_check: missing audio")
        outputs.append((frames, (out / "audio.raw").read_bytes(), manifest))
    if outputs[0] != outputs[1]:
        sys.exit("oracle_check: repeated runs differ")
    missing = subprocess.run([sys.executable, str(runner), "--core", str(base / "missing"),
                              "--rom", str(cart), "--frames", "3", "--out", str(base / "bad")],
                             capture_output=True, text=True, timeout=10)
    if missing.returncode == 0:
        sys.exit("oracle_check: missing core accepted")
    empty_cart = base / "empty.bin"
    empty_cart.write_bytes(b"")
    failed_out = base / "empty-cart"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(empty_cart),
                             "--frames", "1", "--out", str(failed_out)],
                            capture_output=True, text=True, timeout=60)
    if result.returncode == 0 or (failed_out / "manifest.json").exists():
        sys.exit("oracle_check: invalid cartridge accepted")
    fake_source = base / "unsupported.c"
    fake_library = base / ("unsupported." + extension)
    fake_source.write_text("""\
#include <stdbool.h>
struct variable { const char *key, *value; };
void retro_set_environment(bool (*environment)(unsigned, void *)) {
    struct variable vars[] = { {"blastem_model", "Model; unsupported"}, {0, 0} };
    environment(16, vars);
}
void retro_set_video_refresh(void *p) { (void)p; }
void retro_set_audio_sample(void *p) { (void)p; }
void retro_set_audio_sample_batch(void *p) { (void)p; }
void retro_set_input_poll(void *p) { (void)p; }
void retro_set_input_state(void *p) { (void)p; }
""")
    link_flag = "-dynamiclib" if sys.platform == "darwin" else "-shared"
    subprocess.run([*compiler, link_flag, "-fPIC", str(fake_source), "-o", str(fake_library)], check=True)
    result = subprocess.run([sys.executable, str(runner), "--core", str(fake_library), "--rom", str(cart),
                             "--frames", "1", "--out", str(base / "unsupported")],
                            capture_output=True, text=True, timeout=10)
    if result.returncode == 0 or "unsupported required options" not in result.stderr:
        sys.exit("oracle_check: unsupported options were not rejected clearly")
    invalid = subprocess.run([sys.executable, str(runner), "--core", str(core),
                              "--rom", str(cart), "--frames", "0", "--out", str(base / "bad")],
                             capture_output=True, text=True, timeout=10)
    if invalid.returncode == 0:
        sys.exit("oracle_check: zero frames accepted")
    blocked = base / "blocked"
    blocked.mkdir()
    (blocked / "frame_000001.ppm").mkdir()
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(cart),
                             "--frames", "1", "--out", str(blocked)], capture_output=True, text=True, timeout=60)
    if result.returncode == 0 or (blocked / "manifest.json").exists():
        sys.exit("oracle_check: frame-write failure was accepted")
    occupied = base / "occupied"
    occupied.mkdir()
    sentinel = occupied / "manifest.json"
    sentinel.write_text("keep this result")
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(cart),
                             "--frames", "1", "--out", str(occupied)], capture_output=True, text=True, timeout=60)
    if result.returncode == 0 or sentinel.read_text() != "keep this result":
        sys.exit("oracle_check: existing output was overwritten")
    # A separate cartridge reads the first pad's Start bit and writes the
    # result to backdrop colour 0. Both code and cartridge are synthetic.
    asm = base / "pad.s"
    program = base / "pad.bin"
    asm.write_text("""\
        move.w #$8104,$c00004
        move.w #$0040,$a10008
        move.w #0,$a10002
loop:   move.l #$c0000000,$c00004
        move.w $a10002,d2
        btst #5,$a10003
        bne.s released
        move.w #$000e,$c00000
        bra.s loop
released:
        move.w #$0000,$c00000
        bra.s loop
""")
    assembler = root / "build/toolchains/vasm/vasmm68k_mot"
    subprocess.run([str(assembler), "-Fbin", "-o", str(program), str(asm)],
                   check=True, capture_output=True)
    pad_rom = bytearray(rom)
    code = program.read_bytes()
    pad_rom[0x200:0x200 + len(code)] = code
    pad_cart = base / "pad.bin"
    pad_cart.write_bytes(pad_rom)
    out = base / "pad-word-io-trace"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(pad_cart),
                             "--frames", "1", "--out", str(out), "--trace", "--feature-only"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: word-width pad I/O run failed: " + result.stderr)
    with (out / "bus.trace").open("rb") as stream:
        pad_events = list(read_trace(stream))
    if not any(e.device == M68K and e.operation == WRITE and e.width == 2 and
               e.address == 0xA10008 and e.value == 0x0040 for e in pad_events):
        sys.exit("oracle_check: missing word-width pad control write")
    if not any(e.device == M68K and e.operation == WRITE and e.width == 2 and
               e.address == 0xA10002 and e.value == 0 for e in pad_events):
        sys.exit("oracle_check: missing word-width pad data write")
    if not any(e.device == M68K and e.operation == READ and e.width == 2 and
               e.address == 0xA10002 and e.value >> 8 == e.value & 0xFF for e in pad_events):
        sys.exit("oracle_check: missing byte-replicated word-width pad read")
    script = base / "input.json"
    script.write_text(json.dumps({str(frame): {"1": ["START"]} for frame in range(1, 5)}))
    frames = []
    for label, extra in (("released", []), ("pressed", ["--input", str(script)])):
        out = base / label
        result = subprocess.run([sys.executable, str(runner), "--core", str(core),
                                 "--rom", str(pad_cart), "--frames", "4", "--out", str(out), *extra],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            sys.exit("oracle_check: input run failed: " + result.stderr)
        manifest = json.loads((out / "manifest.json").read_text())
        expected_input = {} if label == "released" else {str(frame): {"1": ["START"]} for frame in range(1, 5)}
        if manifest.get("input") != expected_input:
            sys.exit("oracle_check: input snapshots missing from manifest")
        frames.append((out / "frame_000004.ppm").read_bytes())
    if frames[0] == frames[1]:
        sys.exit("oracle_check: scripted Start did not affect the synthetic cartridge")
    out = base / "released-again"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(pad_cart),
                             "--frames", "8", "--out", str(out), "--input", str(script)],
                            capture_output=True, text=True, timeout=60)
    if result.returncode or (out / "frame_000008.ppm").read_bytes() != frames[0]:
        sys.exit("oracle_check: unspecified frames did not release Start")
    out = base / "three-button"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(pad_cart),
                             "--frames", "4", "--out", str(out), "--input", str(script), "--pad1", "3"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode or (out / "frame_000004.ppm").read_bytes() != frames[1]:
        sys.exit("oracle_check: three-button Start did not match")
    if json.loads((out / "manifest.json").read_text())["options"]["blastem_io_1"] != "gamepad3.1":
        sys.exit("oracle_check: three-button configuration not recorded")
    script.write_text(json.dumps({"1": {"1": ["X"]}}))
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(pad_cart),
                             "--frames", "4", "--out", str(base / "bad-pad"), "--input", str(script),
                             "--pad1", "3"], capture_output=True, text=True, timeout=60)
    if result.returncode == 0:
        sys.exit("oracle_check: six-button-only input accepted for a three-button pad")
    asm.write_text("        move.w #$8104,$c00004\n"
                   "        move.l #$c0000000,$c00004\n"
                   "        move.w #$000e,$c00000\n"
                   "        move.w $c00004,d1\n"
                   "        move.w #$8114,$c00004\n"
                   "        move.w #$9301,$c00004\n"
                   "        move.w #$9400,$c00004\n"
                   "        move.w #$9500,$c00004\n"
                   "        move.w #$9602,$c00004\n"
                   "        move.w #$9700,$c00004\n"
                   "        move.l #$40000080,$c00004\n"
                   "        move.l #$00000000,$c00004\n"
                   "        move.w $c00000,d2\n"
                   "        move.w #$1234,$00ff0000\n"
                   "        move.w $00ff0000,d0\n"
                   "        move.b #$5a,$00ff0002\n"
                   "        move.b $00ff0002,d1\n"
                   "        move.l #$deadbeef,d0\n"
                   "        move.b d0,$00ff0004\n"
                   "        move.w d0,$00ff0006\n"
                   "        move.w #0,$00a11200\n"
                   "        move.w #$0100,$00a11100\n"
                   "        move.w #$0100,$00a11200\n"
                   "wait_z80: move.w $00a11100,d1\n"
                   "        btst #8,d1\n"
                   "        bne.s wait_z80\n"
                   "        move.w #$3000,$00a04000\n"
                   "loop:   bra.s loop\n")
    bus_program = base / "bus.bin"
    assembled = subprocess.run([str(assembler), "-Fbin", "-o", str(bus_program), str(asm)],
                               capture_output=True, text=True)
    if assembled.returncode:
        sys.exit("oracle_check: bus fixture did not assemble: " + assembled.stderr)
    bus_rom = bytearray(rom)
    bus_rom[0x400:0x402] = bytes.fromhex("a5 5a")
    code = bus_program.read_bytes()
    bus_rom[0x200:0x200 + len(code)] = code
    bus_cart = base / "bus-cart.bin"
    bus_cart.write_bytes(bus_rom)
    out = base / "bus"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(bus_cart),
                             "--frames", "1", "--out", str(out), "--trace"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: bus trace run failed: " + result.stderr)
    with (out / "bus.trace").open("rb") as f:
        events = list(read_trace(f))
    bus_manifest = json.loads((out / "manifest.json").read_text())
    if bus_manifest.get("trace_capabilities") != ["m68k_cpu_access", "z80_cpu_access", "vdp_port", "vdp_68k_dma_source", "vdp_dma_fill", "vdp_dma_copy", "vdp_hint_lines", "psg_write", "ym2612_write", "ym2612_port_write", "chip_audio", "chip_audio_source_samples"] or bus_manifest.get("trace_events") != len(events):
        sys.exit("oracle_check: trace coverage metadata mismatch")
    ym_source = bus_manifest.get("audio_source_streams", {})
    ym_source_data = ym_source.get("ym2612", {})
    ym_source_path = out / ym_source_data.get("file", "")
    if (ym_source.get("format") != "s16-native-endian-stereo" or
            ym_source_data.get("sample_rate", 0) <= 0 or ym_source_data.get("channels") != 2 or
            ym_source_data.get("frames", 0) <= 0 or
            not ym_source_path.is_file() or
            ym_source_path.stat().st_size != ym_source_data["frames"] * 4):
        sys.exit("oracle_check: raw YM source-sample stream metadata mismatch")
    psg_source = bus_manifest.get("audio_source_streams", {}).get("psg", {})
    psg_source_path = out / psg_source.get("file", "")
    if (psg_source.get("sample_rate", 0) <= 0 or psg_source.get("channels") != 2 or
            psg_source.get("frames", 0) <= 0 or not psg_source_path.is_file() or
            psg_source_path.stat().st_size != psg_source["frames"] * 4):
        sys.exit("oracle_check: raw PSG source-sample stream metadata mismatch")
    if not any(e.device == M68K and e.operation == WRITE and e.address == 0xFF0000 and e.value == 0x1234
               for e in events):
        sys.exit("oracle_check: missing work-RAM write event")
    if not any(e.device == M68K and e.operation == READ and e.address == 0xFF0000 and e.value == 0x1234
               for e in events):
        sys.exit("oracle_check: missing work-RAM read event")
    if not any(e.device == M68K and e.operation == WRITE and e.width == 1 and
               e.address == 0xFF0002 and e.value == 0x5a for e in events):
        sys.exit("oracle_check: missing work-RAM byte write event")
    if not any(e.device == M68K and e.operation == READ and e.width == 1 and
               e.address == 0xFF0002 and e.value == 0x5a for e in events):
        sys.exit("oracle_check: missing work-RAM byte read event")
    if not any(e.device == M68K and e.operation == WRITE and e.width == 1 and
               e.address == 0xFF0004 and e.value == 0xef for e in events):
        sys.exit("oracle_check: 68000 byte write trace value was not narrowed: " +
                 repr([e for e in events if e.device == M68K and e.address == 0xFF0004]))
    if not any(e.device == M68K and e.operation == WRITE and e.width == 2 and
               e.address == 0xFF0006 and e.value == 0xbeef for e in events):
        sys.exit("oracle_check: 68000 word write trace value was not narrowed: " +
                 repr([e for e in events if e.device == M68K and e.address == 0xFF0006]))
    if not any(e.device == M68K and e.operation == WRITE and e.width == 2 and
               e.address == 0xA04000 and e.value == 0x3000 for e in events):
        sys.exit("oracle_check: missing word-width direct YM bus write")
    direct_bus = next(e for e in events if e.device == M68K and e.operation == WRITE and
                      e.width == 2 and e.address == 0xA04000 and e.value == 0x3000)
    direct_port = next((e for e in events if e.device == YM2612 and e.operation == WRITE and
                        e.address == (YM_PORT_M68K_ADDR_TAG | 0) and e.value == 0x30), None)
    if direct_port is None or direct_port.clock != direct_bus.clock:
        sys.exit("oracle_check: word-width direct YM transaction was not source-tagged: " +
                 repr((direct_bus, direct_port,
                       [event for event in events if event.device == YM2612],
                       [event for event in events if event.device == M68K and
                        event.address in (0xA11100, 0xA11200, 0xA04000)][-8:])))
    word_write = next(e for e in events if e.device == M68K and e.operation == WRITE and
                      e.width == 2 and e.address == 0xFF0000 and e.value == 0x1234)
    word_read = next(e for e in events if e.device == M68K and e.operation == READ and
                     e.width == 2 and e.address == 0xFF0000 and e.value == 0x1234)
    byte_write = next(e for e in events if e.device == M68K and e.operation == WRITE and
                      e.width == 1 and e.address == 0xFF0002 and e.value == 0x5a)
    byte_read = next(e for e in events if e.device == M68K and e.operation == READ and
                     e.width == 1 and e.address == 0xFF0002 and e.value == 0x5a)
    # Motorola's M68000 User's Manual, Table 8-2: MOVE.B/W abs.l,Dn is
    # 16 processor clocks; this fixture pins the MD1 divider to 7 master clocks.
    if (word_read.clock - word_write.clock, byte_read.clock - byte_write.clock) != (112, 112):
        sys.exit("oracle_check: absolute-long 68000 read timing mismatch: " +
                 repr((word_write, word_read, byte_write, byte_read)))
    if not any(e.device == M68K and e.operation == FETCH and e.address == 0x200 for e in events):
        sys.exit("oracle_check: missing explicit prefetch event: " + repr(events[:12]))
    if not any(e.device == VDP and e.operation == WRITE and e.address == 0 and e.value == 0x000E
               for e in events):
        sys.exit("oracle_check: missing VDP data-port write event")
    if not any(e.device == VDP and e.operation == WRITE and e.address == 4 and e.value == 0x8104
               for e in events):
        sys.exit("oracle_check: missing VDP control-port write event")
    if not any(e.device == VDP and e.operation == READ and e.width == 2 and e.address == 4
               for e in events):
        sys.exit("oracle_check: missing VDP control-port read event")
    if not any(e.device == VDP and e.operation == READ and e.width == 2 and
               e.address == 0 and e.value == 0xa55a for e in events):
        sys.exit("oracle_check: missing expected DMA data-port readback: " + repr(
            [e for e in events if e.device == VDP and e.operation == READ and e.address == 0]))

    # Pin the candidate's device-port times against the 68000 completion events
    # for this fixed four-access program. Device-side events follow the VDP
    # scheduler; CPU events are emitted after the mapped handler returns.
    port_timing_asm = base / "vdp-port-timing.s"
    port_timing_asm.write_text("        move.w #$8104,$c00004\n"
                               "        move.w #$000e,$c00000\n"
                               "        move.w $c00004,d0\n"
                               "        move.w $c00000,d1\n"
                               "loop:   bra.s loop\n")
    port_timing_program = base / "vdp-port-timing.bin"
    subprocess.run([str(assembler), "-Fbin", "-o", str(port_timing_program), str(port_timing_asm)],
                   check=True, capture_output=True)
    port_timing_rom = bytearray(rom)
    port_timing_code = port_timing_program.read_bytes()
    port_timing_rom[0x200:0x200 + len(port_timing_code)] = port_timing_code
    port_timing_cart = base / "vdp-port-timing.binrom"
    port_timing_cart.write_bytes(port_timing_rom)
    port_timing_out = base / "vdp-port-timing"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom",
                             str(port_timing_cart), "--frames", "1", "--out", str(port_timing_out),
                             "--trace"], capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: VDP port timing run failed: " + result.stderr)
    with (port_timing_out / "bus.trace").open("rb") as stream:
        port_timing_events = list(read_trace(stream))
    port_cpu_control_write = next(e for e in port_timing_events if e.device == M68K and
                                  e.operation == WRITE and e.width == 2 and
                                  e.address == 0xC00004 and e.value == 0x8104)
    port_vdp_control_write = next(e for e in port_timing_events if e.device == VDP and
                                  e.operation == WRITE and e.width == 2 and
                                  e.address == 4 and e.value == 0x8104)
    port_cpu_data_write = next(e for e in port_timing_events if e.device == M68K and
                               e.operation == WRITE and e.width == 2 and
                               e.address == 0xC00000 and e.value == 0x000E)
    port_vdp_data_write = next(e for e in port_timing_events if e.device == VDP and
                               e.operation == WRITE and e.width == 2 and
                               e.address == 0 and e.value == 0x000E)
    port_cpu_control_read = next(e for e in port_timing_events if e.device == M68K and
                                 e.operation == READ and e.width == 2 and e.address == 0xC00004)
    port_vdp_control_read = next(e for e in port_timing_events if e.device == VDP and
                                 e.operation == READ and e.width == 2 and e.address == 4)
    port_cpu_data_read = next(e for e in port_timing_events if e.device == M68K and
                              e.operation == READ and e.width == 2 and e.address == 0xC00000)
    port_vdp_data_read = next(e for e in port_timing_events if e.device == VDP and
                              e.operation == READ and e.width == 2 and e.address == 0)
    port_clock_deltas = (port_vdp_control_write.clock - port_cpu_control_write.clock,
                         port_vdp_data_write.clock - port_cpu_data_write.clock,
                         port_vdp_control_read.clock - port_cpu_control_read.clock,
                         port_vdp_data_read.clock - port_cpu_data_read.clock)
    if port_clock_deltas != (8, -12, -4, 0):
        sys.exit("oracle_check: VDP port event clocks drifted from the candidate fixture: " +
                 repr((port_clock_deltas, port_timing_events)))

    dma_events = [e for e in events if e.device == VDP and e.operation == DMA]
    if len(dma_events) != 1 or (dma_events[0].width, dma_events[0].address, dma_events[0].value) != (2, 0x400, 0xa55a):
        sys.exit("oracle_check: 68K-to-VDP DMA source-slot mismatch: " + repr(dma_events))
    dma_launch = next(e for e in events if e.device == VDP and e.operation == WRITE and
                      e.address == 4 and e.value == 0x0080)
    if dma_events[0].clock != dma_launch.clock:
        sys.exit("oracle_check: first DMA source slot clock mismatch: " + repr((dma_launch, dma_events[0])))
    fill_cases = (("vram", 0x40000080, 1, 0x10000001, 0x00a5),
                  ("cram", 0xc0000080, 2, 0x10000001, 0x0000),
                  ("vsram", 0x40000090, 2, 0x10000001, 0x0000))
    for name, command, width, destination, value in fill_cases:
        asm.write_text("        move.w #$8114,$c00004\n"
                       "        move.w #$8f01,$c00004\n"
                       "        move.w #$9301,$c00004\n"
                       "        move.w #$9400,$c00004\n"
                       "        move.w #$9780,$c00004\n"
                       f"        move.l #${command:08x},$c00004\n"
                       "        move.w #$a500,$c00000\n"
                       "loop:   bra.s loop\n")
        fill_program = base / f"{name}-fill.bin"
        subprocess.run([str(assembler), "-Fbin", "-o", str(fill_program), str(asm)],
                       check=True, capture_output=True)
        fill_rom = bytearray(rom)
        code = fill_program.read_bytes()
        fill_rom[0x200:0x200 + len(code)] = code
        fill_cart = base / f"{name}-fill.binrom"
        fill_cart.write_bytes(fill_rom)
        fill_out = base / f"{name}-fill"
        result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(fill_cart),
                                 "--frames", "1", "--out", str(fill_out), "--trace"],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            sys.exit(f"oracle_check: {name} fill-DMA run failed: " + result.stderr)
        with (fill_out / "bus.trace").open("rb") as f:
            fill_events = list(read_trace(f))
        fill_dma = [e for e in fill_events if e.device == VDP and e.operation == DMA]
        if (len(fill_dma) != 1 or
                (fill_dma[0].width, fill_dma[0].address, fill_dma[0].value) !=
                (width, destination, value)):
            sys.exit(f"oracle_check: missing or incorrect {name} fill-DMA slot: " + repr(fill_dma))
        if name == "vram":
            fill_seed = next(e for e in fill_events if e.device == VDP and e.operation == WRITE and
                             e.address == 0 and e.value == 0xa500)
            if fill_dma[0].clock - fill_seed.clock != 5 * 20:
                sys.exit("oracle_check: VDP fill-DMA source-slot clock mismatch: " +
                         repr((fill_seed, fill_dma[0])))
    asm.write_text("        move.w #$8114,$c00004\n"
                   "        move.w #$8f01,$c00004\n"
                   "        move.l #$40000000,$c00004\n"
                   "        move.w #$a55a,$c00000\n"
                   "        move.w #$9302,$c00004\n"
                   "        move.w #$9400,$c00004\n"
                   "        move.w #$9500,$c00004\n"
                   "        move.w #$9600,$c00004\n"
                   "        move.w #$97c0,$c00004\n"
                   "        move.l #$40040080,$c00004\n"
                   "        move.l #$00040000,$c00004\n"
                   "        move.w $c00000,d0\n"
                   "loop:   bra.s loop\n")
    copy_program = base / "copy.bin"
    subprocess.run([str(assembler), "-Fbin", "-o", str(copy_program), str(asm)],
                   check=True, capture_output=True)
    copy_rom = bytearray(rom)
    code = copy_program.read_bytes()
    copy_rom[0x200:0x200 + len(code)] = code
    copy_cart = base / "copy.binrom"
    copy_cart.write_bytes(copy_rom)
    copy_out = base / "copy"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(copy_cart),
                             "--frames", "1", "--out", str(copy_out), "--trace"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: copy DMA run failed: " + result.stderr)
    with (copy_out / "bus.trace").open("rb") as f:
        copy_events = list(read_trace(f))
    copy_readbacks = [e for e in copy_events if e.device == VDP and e.operation == READ and e.address == 0]
    if not any(e.value == 0xa55a for e in copy_readbacks):
        sys.exit("oracle_check: copy DMA data-port readback mismatch: " + repr(copy_readbacks))
    copy_dma = [e for e in copy_events if e.device == VDP and e.operation == DMA]
    if [(e.width, e.address, e.value) for e in copy_dma] != [(1, 0x20000004, 0x5a),
                                                            (1, 0x20000005, 0xa5)]:
        sys.exit("oracle_check: VDP copy-DMA byte slots mismatch: " + repr(copy_dma))
    copy_launch = next(e for e in copy_events if e.device == VDP and e.operation == WRITE and
                       e.address == 4 and e.value == 0x0080)
    if (copy_dma[0].clock - copy_launch.clock, copy_dma[1].clock - copy_dma[0].clock) != (40, 40):
        sys.exit("oracle_check: VDP copy-DMA slot clocks mismatch: " + repr((copy_launch, copy_dma)))
    fifo_asm = base / "fifo-stall.s"
    fifo_asm.write_text("        move.w #$8154,$c00004\n"
                        "        move.w #$8c01,$c00004\n"
                        "        move.w #$8f02,$c00004\n"
                        "        move.l #$40000000,$c00004\n"
                        "        lea $c00000,a0\n"
                        "        move.w #$1234,d0\n" +
                        "        move.w d0,(a0)\n" * 16 +
                        "loop:   bra.s loop\n")
    fifo_program = base / "fifo-stall.bin"
    subprocess.run([str(assembler), "-Fbin", "-o", str(fifo_program), str(fifo_asm)],
                   check=True, capture_output=True)
    fifo_rom = bytearray(rom)
    code = fifo_program.read_bytes()
    fifo_rom[0x200:0x200 + len(code)] = code
    fifo_cart = base / "fifo-stall.binrom"
    fifo_cart.write_bytes(fifo_rom)
    fifo_out = base / "fifo-stall"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(fifo_cart),
                             "--frames", "1", "--out", str(fifo_out), "--trace"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: VDP FIFO stall run failed: " + result.stderr)
    with (fifo_out / "bus.trace").open("rb") as f:
        fifo_events = list(read_trace(f))
    fifo_writes = [e for e in fifo_events if e.device == M68K and e.operation == WRITE and
                   e.width == 2 and e.address == 0xc00000 and e.value == 0x1234]
    if len(fifo_writes) != 16:
        sys.exit("oracle_check: VDP FIFO write fixture lost accesses: " + repr(fifo_writes))
    fifo_deltas = [right.clock - left.clock for left, right in zip(fifo_writes, fifo_writes[1:])]
    # Motorola Table 8-2 gives MOVE.W Dn,(An) as 8 processor clocks; MD1 uses
    # 7 master clocks per processor clock. A full VDP FIFO must extend a write.
    if max(fifo_deltas) <= 8 * 7:
        sys.exit("oracle_check: VDP FIFO fixture did not expose a stalled M68K write: " + repr(fifo_deltas))
    fifo_vdp_writes = [e for e in fifo_events if e.device == VDP and e.operation == WRITE and
                       e.width == 2 and e.address == 0 and e.value == 0x1234]
    if len(fifo_vdp_writes) != len(fifo_writes):
        sys.exit("oracle_check: VDP FIFO events do not cover every M68K write: " + repr(fifo_vdp_writes))

    # Run past the core's near-UINT32_MAX common-cycle adjustment while a
    # deliberately slow Z80 loop continues to write the YM2612 ports.
    epoch_z80 = bytes.fromhex(
        "06 ff 0e ff 0d 20 fd 10 f9 3e 30 32 00 40 3e 7f 32 01 40 c3 00 00")
    epoch_lines = ["        move.w #0,$a11200", "        move.w #$0100,$a11100",
                   "        move.w #$0100,$a11200"]
    epoch_lines.extend(f"        move.b #${value:02x},$a0{offset:04x}"
                       for offset, value in enumerate(epoch_z80))
    epoch_lines.extend(("        move.w #0,$a11100", "epoch:  move.w #$ffff,d0",
                        "delay:  dbra d0,delay", "        move.w $c00004,d1",
                        "        bra.s epoch"))
    epoch_asm = base / "epoch.s"
    epoch_asm.write_text("\n".join(epoch_lines) + "\n")
    epoch_program = base / "epoch.bin"
    subprocess.run([str(assembler), "-Fbin", "-o", str(epoch_program), str(epoch_asm)],
                   check=True, capture_output=True)
    epoch_rom = bytearray(rom)
    code = epoch_program.read_bytes()
    epoch_rom[0x200:0x200 + len(code)] = code
    epoch_cart = base / "epoch.binrom"
    epoch_cart.write_bytes(epoch_rom)
    epoch_out = base / "epoch"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(epoch_cart),
                             "--frames", "5000", "--out", str(epoch_out), "--trace", "--feature-only"],
                            capture_output=True, text=True, timeout=180)
    if result.returncode:
        sys.exit("oracle_check: long-run common epoch fixture failed: " + result.stderr)
    with (epoch_out / "bus.trace").open("rb") as f:
        epoch_events = list(read_trace(f))
    wrap = 0xFFFFFFFF
    vdp_after_wrap = [e for e in epoch_events if e.device == VDP and e.clock > wrap]
    z80_before_wrap = [e for e in epoch_events if e.device == Z80 and e.operation == WRITE and
                       e.address == 0x4001 and e.value == 0x7f and e.clock <= wrap]
    z80_after_wrap = [e for e in epoch_events if e.device == Z80 and e.operation == WRITE and
                      e.address == 0x4001 and e.value == 0x7f and e.clock > wrap]
    ym_after_wrap = [e for e in epoch_events if e.device == YM2612 and e.operation == WRITE and
                     e.address == (YM_PORT_Z80_ADDR_TAG | 1) and
                     e.value == 0x7f and e.clock > wrap]
    if not vdp_after_wrap or not z80_before_wrap or not z80_after_wrap:
        sys.exit("oracle_check: trace did not span the common-counter adjustment with VDP and Z80 events: " +
                 repr((vdp_after_wrap[-1:] if vdp_after_wrap else [],
                       z80_before_wrap[-1:] if z80_before_wrap else [],
                       z80_after_wrap[-1:] if z80_after_wrap else [])))
    if not any(z.clock == ym.clock for z in z80_after_wrap for ym in ym_after_wrap):
        sys.exit("oracle_check: post-adjustment Z80 and YM clocks diverged: " +
                 repr((z80_after_wrap[-4:], ym_after_wrap[-4:])))
    asm.write_text("        move.w #$8114,$c00004\n"
                   "        move.w #$8010,$c00004\n"
                   "        move.w #$8a01,$c00004\n"
                   "loop:   bra.s loop\n")
    hint_program = base / "hint.bin"
    subprocess.run([str(assembler), "-Fbin", "-o", str(hint_program), str(asm)],
                   check=True, capture_output=True)
    hint_rom = bytearray(rom)
    code = hint_program.read_bytes()
    hint_rom[0x200:0x200 + len(code)] = code
    hint_cart = base / "hint.binrom"
    hint_cart.write_bytes(hint_rom)
    hint_out = base / "hint"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(hint_cart),
                             "--frames", "1", "--out", str(hint_out), "--trace"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: HINT run failed: " + result.stderr)
    with (hint_out / "bus.trace").open("rb") as f:
        hint_events = list(read_trace(f))
    hint_lines = [e for e in hint_events if e.device == VDP and e.operation == IRQ]
    if not hint_lines or any(e.width != 1 or e.value != 1 or e.address >= 224 for e in hint_lines):
        sys.exit("oracle_check: VDP HINT line events mismatch: " + repr(hint_lines[:12]))
    # The NTSC candidate has 3,420 master clocks per line. This fixture's
    # reload value emits a pending event every second V-counter line.
    if (len(hint_lines) < 2 or
            any((right.address - left.address, right.clock - left.clock) != (2, 2 * 3420)
                for left, right in zip(hint_lines, hint_lines[1:]))):
        sys.exit("oracle_check: VDP HINT line clocks mismatch: " + repr(hint_lines[:12]))
    feature_path = root / "consoles/md/tools/features.py"
    feature_output = hint_out / "features.json"
    result = subprocess.run([sys.executable, str(feature_path), "--manifest", str(hint_out / "manifest.json"),
                             "--trace", str(hint_out / "bus.trace"), "--out", str(feature_output)],
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        sys.exit("oracle_check: feature extraction failed: " + result.stderr)
    hint_feature = json.loads(feature_output.read_text())["features"]["hint_lines"]
    if (hint_feature["status"] != "observed" or
            [item["address"] for item in hint_feature["evidence"]] != [event.address for event in hint_lines]):
        sys.exit("oracle_check: extracted HINT lines mismatch: " + repr(hint_feature))
    survey_out = base / "feature-only"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(bus_cart),
                             "--frames", "1", "--out", str(survey_out), "--trace", "--feature-only"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: feature-only run failed: " + result.stderr)
    survey_manifest = json.loads((survey_out / "manifest.json").read_text())
    if survey_manifest.get("trace_scope") != "survey-feature-filter-v1":
        sys.exit("oracle_check: feature-only trace scope missing")
    if any((survey_out / filename).exists() for filename in
           ("frame_000001.ppm", "audio.raw", "ym2612.raw", "psg.raw", "ym2612-source.raw",
            "psg-source.raw")):
        sys.exit("oracle_check: feature-only run wrote full media captures")
    with (survey_out / "bus.trace").open("rb") as f:
        survey_events = list(read_trace(f))
    if any(e.device == M68K and not (0xA04000 <= e.address < 0xA06000 or
                                     0xA10000 <= e.address < 0xA10020 or
                                     0xA13000 <= e.address < 0xA13100)
           for e in survey_events):
        sys.exit("oracle_check: feature-only 68000 filter leaked an unrelated access")
    if "chip_audio" in survey_manifest.get("trace_capabilities", []):
        sys.exit("oracle_check: feature-only run advertises uncaptured chip audio")
    survey_features = base / "feature-only.json"
    result = subprocess.run([sys.executable, str(feature_path), "--manifest",
                             str(survey_out / "manifest.json"), "--trace",
                             str(survey_out / "bus.trace"), "--out", str(survey_features)],
                            capture_output=True, text=True, timeout=30)
    if result.returncode or json.loads(survey_features.read_text())["features"]["dma_68k_to_vdp"]["status"] != "observed":
        sys.exit("oracle_check: feature-only extraction failed: " + result.stderr)
    script.write_text(json.dumps({"1": {"1": ["NOT_A_BUTTON"]}}))
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(pad_cart),
                             "--frames", "4", "--out", str(base / "bad-input"), "--input", str(script)],
                            capture_output=True, text=True, timeout=10)
    if result.returncode == 0:
        sys.exit("oracle_check: unknown input accepted")
    # Drive the PSG tone channel; a nonempty silent buffer is insufficient.
    asm.write_text("""\
        move.b #$80,$c00011
        move.b #$10,$c00011
        move.b #$90,$c00011
loop:   bra.s loop
""")
    subprocess.run([str(assembler), "-Fbin", "-o", str(program), str(asm)],
                   check=True, capture_output=True)
    tone_rom = bytearray(rom)
    code = program.read_bytes()
    tone_rom[0x200:0x200 + len(code)] = code
    tone_cart = base / "tone.bin"
    tone_cart.write_bytes(tone_rom)
    out = base / "tone"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(tone_cart),
                             "--frames", "8", "--out", str(out), "--trace"], capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: PSG run failed: " + result.stderr)
    samples = (out / "audio.raw").read_bytes()
    if not any(samples):
        sys.exit("oracle_check: PSG tone produced only silence")
    psg_samples = (out / "psg.raw").read_bytes()
    ym_samples = (out / "ym2612.raw").read_bytes()
    if len(psg_samples) != len(samples) or len(ym_samples) != len(samples):
        sys.exit("oracle_check: per-chip PCM is not aligned with mixed audio")
    if not any(psg_samples):
        sys.exit("oracle_check: PSG per-chip stream is silent")
    with (out / "bus.trace").open("rb") as f:
        tone_events = list(read_trace(f))
    if not any(e.device == 5 and e.operation == WRITE and e.width == 1 and e.value == 0x80
               for e in tone_events):
        sys.exit("oracle_check: missing PSG write event")
    for value in (0x80, 0x10, 0x90):
        cpu_write = next(e for e in tone_events if e.device == M68K and e.operation == WRITE and
                         e.address == 0xC00011 and e.value == value)
        chip_write = next(e for e in tone_events if e.device == PSG and e.operation == WRITE and
                          e.value == value)
        if cpu_write.clock != chip_write.clock:
            sys.exit("oracle_check: PSG write clock disagrees with its 68000 bus access: " +
                     repr((cpu_write, chip_write)))
    bank_select = "3e 01 32 00 60" + " 3e 00 32 00 60" * 8
    z80_prefix = bytes.fromhex(
        bank_select + " 3a 00 80 3a 00 40 3e 30 32 00 01 32 00 40 3e 7f 32 01 40")
    ym_registers = [(0xb0, 0x07), (0xb4, 0xc0)]
    ym_registers.extend((address, 0x01) for address in (0x30, 0x34, 0x38, 0x3c))
    ym_registers.extend((address, 0x00) for address in (0x40, 0x44, 0x48, 0x4c))
    ym_registers.extend((address, 0x1f) for address in (0x50, 0x54, 0x58, 0x5c))
    ym_registers.extend((address, 0x00) for address in (0x60, 0x64, 0x68, 0x6c,
                                                        0x70, 0x74, 0x78, 0x7c))
    ym_registers.extend((address, 0x0f) for address in (0x80, 0x84, 0x88, 0x8c))
    ym_registers.extend(((0xa0, 0x98), (0xa4, 0x22), (0x28, 0xf0)))
    ym_setup = b"".join(bytes.fromhex(
        f"3e {register:02x} 32 00 40 3e {value:02x} 32 01 40")
        for register, value in ym_registers)
    z80_program = z80_prefix + ym_setup + bytes.fromhex(
        "3e 80 32 11 7f 3e 10 32 11 7f 3e 90 32 11 7f d3 7f db 7f 18 fe")
    lines = ["        move.w #0,$a11200", "        move.w #$0100,$a11100",
             "        move.w #$0100,$a11200"]
    lines.extend(f"        move.b #${value:02x},$a0{offset:04x}"
                 for offset, value in enumerate(z80_program))
    lines.extend(("        move.w #0,$a11100", "loop:   bra.s loop"))
    asm.write_text("\n".join(lines) + "\n")
    ym_program = base / "ym.bin"
    subprocess.run([str(assembler), "-Fbin", "-o", str(ym_program), str(asm)],
                   check=True, capture_output=True)
    ym_rom = bytearray(65536)
    ym_rom[:len(rom)] = rom
    ym_rom[0x8000] = 0x5a
    code = ym_program.read_bytes()
    ym_rom[0x200:0x200 + len(code)] = code
    ym_cart = base / "ym.binrom"
    ym_cart.write_bytes(ym_rom)
    out = base / "ym"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(ym_cart),
                             "--frames", "8", "--out", str(out), "--trace"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: YM run failed: " + result.stderr)
    ym_mixed = (out / "audio.raw").read_bytes()
    ym_samples = (out / "ym2612.raw").read_bytes()
    ym_psg_samples = (out / "psg.raw").read_bytes()
    if len(ym_samples) != len(ym_mixed) or len(ym_psg_samples) != len(ym_mixed):
        sys.exit("oracle_check: per-chip PCM is not aligned with mixed audio")
    streams = json.loads((out / "manifest.json").read_text()).get("audio_streams", {})
    if (streams.get("format") != "s16-native-endian-stereo" or
            any(streams.get(name, {}).get("frames") != len(ym_mixed) // 4
                for name in ("mixed", "ym2612", "psg"))):
        sys.exit("oracle_check: per-chip audio manifest mismatch")
    ym_values = struct.unpack("=" + "h" * (len(ym_samples) // 2), ym_samples)
    if max(ym_values) - min(ym_values) < 500:
        with (out / "bus.trace").open("rb") as f:
            diagnostic_events = list(read_trace(f))
        ym_port_events = [e for e in diagnostic_events
                          if e.device == YM2612 and e.operation == WRITE]
        sys.exit("oracle_check: YM2612 per-chip signal is below the fixture amplitude: " +
                 repr((min(ym_values), max(ym_values), ym_port_events[-60:])))
    with (out / "bus.trace").open("rb") as f:
        ym_events = list(read_trace(f))
    z80_events = [e for e in ym_events if e.device == Z80]
    if not any(e.operation == FETCH and e.address == 0 for e in z80_events):
        sys.exit("oracle_check: missing Z80 instruction fetch event: " + repr(z80_events[:24]))
    if not any(e.operation == WRITE and e.width == 1 and e.address == 0x0100 and e.value == 0x30
               for e in z80_events):
        sys.exit("oracle_check: missing Z80 fast-RAM write event")
    if not any(e.operation == READ and e.width == 1 and e.address == 0x8000 and e.value == 0x5a
               for e in z80_events):
        sys.exit("oracle_check: missing expected Z80 bank-window read event: " + repr(
            [e for e in z80_events if e.address in (0x6000, 0x8000)]) + result.stderr)
    if not any(e.operation == READ and e.width == 1 and e.address == 0x4000 and e.value == 0
               for e in z80_events):
        sys.exit("oracle_check: missing mapped Z80 YM-status read event")
    if not any(e.operation == WRITE and e.width == 1 and e.address == 0x4000 and e.value == 0x30
               for e in z80_events):
        sys.exit("oracle_check: missing mapped Z80 YM-address write event")
    if not any(e.operation == WRITE and e.width == 1 and e.address == 0x4001 and e.value == 0x7f
               for e in z80_events):
        sys.exit("oracle_check: missing mapped Z80 YM-data write event")
    for value in (0x80, 0x10, 0x90):
        z80_psg_bus = next((e for e in z80_events if e.operation == WRITE and
                            e.width == 1 and e.address == 0x7F11 and e.value == value), None)
        psg_write = next((e for e in ym_events if e.device == PSG and e.operation == WRITE and
                          e.value == value), None)
        if z80_psg_bus is None or psg_write is None or z80_psg_bus.clock != psg_write.clock:
            sys.exit("oracle_check: Z80 PSG port write is missing or mistimed: " +
                     repr((value, z80_psg_bus, psg_write)))
    bank_writes = [e for e in z80_events if e.operation == WRITE and e.address == 0x6000]
    if [e.value for e in bank_writes] != [1] + [0] * 8:
        sys.exit("oracle_check: Z80 bank-select write sequence mismatch: " + repr(bank_writes))
    z80_tstate = 15  # MCLKS_PER_Z80 for the pinned md1va3 model.
    if any(right.clock - left.clock != 20 * z80_tstate
           for left, right in zip(bank_writes, bank_writes[1:])):
        sys.exit("oracle_check: Z80 bank-select instruction timing mismatch: " + repr(bank_writes))
    bank_read = next(e for e in z80_events if e.operation == READ and e.address == 0x8000)
    ym_status_read = next(e for e in z80_events
                          if e.operation == READ and e.address == 0x4000 and e.value == 0)
    fast_ram_write = next(e for e in z80_events
                          if e.operation == WRITE and e.address == 0x0100 and e.value == 0x30)
    ym_address_write = next(e for e in z80_events
                            if e.operation == WRITE and e.address == 0x4000 and e.value == 0x30)
    ym_data_write = next(e for e in z80_events
                         if e.operation == WRITE and e.address == 0x4001 and e.value == 0x7f)
    timed_deltas = [bank_read.clock - bank_writes[-1].clock,
                    ym_status_read.clock - bank_read.clock,
                    fast_ram_write.clock - ym_status_read.clock,
                    ym_address_write.clock - fast_ram_write.clock,
                    ym_data_write.clock - ym_address_write.clock]
    # 3T memory cycles use 15 master clocks per T-state; the bank-window
    # callback adds a further 3T arbitration wait before its read completes.
    if timed_deltas != [cycles * z80_tstate for cycles in (16, 13, 20, 13, 20)]:
        sys.exit("oracle_check: Z80 access-completion clocks mismatch: " + repr(timed_deltas))
    z80_landmarks = [
        bank_writes[0], bank_read, ym_status_read, fast_ram_write, ym_address_write, ym_data_write,
    ]
    if any(left.clock >= right.clock for left, right in zip(z80_landmarks, z80_landmarks[1:])):
        sys.exit("oracle_check: Z80 fetch, bank, RAM and YM events are out of order: " + repr(z80_landmarks))

    io_z80 = bytes.fromhex("3e 5a d3 7f db 7f 18 fe")
    io_lines = ["        move.w #0,$a11200", "        move.w #$0100,$a11100",
                "        move.w #$0100,$a11200"]
    io_lines.extend(f"        move.b #${value:02x},$a0{offset:04x}"
                    for offset, value in enumerate(io_z80))
    io_lines.extend(("        move.w #0,$a11100", "loop:   bra.s loop"))
    io_asm = base / "z80-io.s"
    io_asm.write_text("\n".join(io_lines) + "\n")
    io_program = base / "z80-io.bin"
    subprocess.run([str(assembler), "-Fbin", "-o", str(io_program), str(io_asm)],
                   check=True, capture_output=True)
    io_rom = bytearray(rom)
    io_code = io_program.read_bytes()
    io_rom[0x200:0x200 + len(io_code)] = io_code
    io_cart = base / "z80-io.binrom"
    io_cart.write_bytes(io_rom)
    io_out = base / "z80-io"
    result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(io_cart),
                             "--frames", "2", "--out", str(io_out), "--trace"],
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: Z80 IN/OUT timing run failed: " + result.stderr)
    with (io_out / "bus.trace").open("rb") as stream:
        io_events = [e for e in read_trace(stream) if e.device == Z80 and e.address == 0x7F and
                     e.operation in (READ, WRITE)]
    if (len(io_events) != 2 or
            [(e.operation, e.width, e.value) for e in io_events] != [(WRITE, 1, 0x5A), (READ, 1, 0xFF)]):
        sys.exit("oracle_check: Z80 IN/OUT access width or value mismatch: " + repr(io_events))
    if io_events[1].clock - io_events[0].clock != 11 * z80_tstate:
        sys.exit("oracle_check: Z80 IN/OUT completion clocks mismatch: " + repr(io_events))

    direct_feature = extract_features(bus_manifest, events)["features"]["direct_68k_ym_writes"]
    if (direct_feature["status"] != "observed" or
            not any(item["cpu_bus"]["width"] == 2 for item in direct_feature["evidence"])):
        sys.exit("oracle_check: direct YM extractor missed the accepted word bus write: " +
                 repr(direct_feature))
    if not any(e.device == YM2612 and e.operation == WRITE and
               e.address == (YM_PORT_Z80_ADDR_TAG | 0) and e.value == 0x30
               for e in ym_events):
        sys.exit("oracle_check: missing YM address-port write event: " + repr(
            [e for e in ym_events if e.device == M68K and e.operation == WRITE and e.address >= 0xA00000]))
    if not any(e.device == YM2612 and e.operation == WRITE and
               e.address == (YM_PORT_Z80_ADDR_TAG | 1) and e.value == 0x7f
               for e in ym_events):
        sys.exit("oracle_check: missing YM data-port write event")
    z80_ym_data = next(e for e in z80_events if e.operation == WRITE and e.address == 0x4001 and e.value == 0x7f)
    ym_port_data = next(e for e in ym_events
                        if e.device == YM2612 and e.operation == WRITE and
                        e.address == (YM_PORT_Z80_ADDR_TAG | 1) and e.value == 0x7f)
    ym_register_data = next(e for e in ym_events
                            if e.device == YM2612 and e.operation == WRITE and e.address == 0x30 and e.value == 0x7f)
    if z80_ym_data.clock != ym_port_data.clock:
        sys.exit("oracle_check: YM port and Z80 bus clocks disagree: " + repr((z80_ym_data, ym_port_data)))
    ym_clock_step = 42  # 7 master clocks per YM clock, with six operators per sample period.
    expected_register_clock = (z80_ym_data.clock + ym_clock_step - 1) // ym_clock_step * ym_clock_step
    if ym_register_data.clock != expected_register_clock:
        sys.exit("oracle_check: YM register commit is not on the next YM clock: " +
                 repr((z80_ym_data, ym_register_data, expected_register_clock)))
    if not any(e.device == YM2612 and e.operation == WRITE and e.address == 0x28 and e.value == 0xf0
               for e in ym_events):
        sys.exit("oracle_check: missing YM channel key-on write event")
    ym_repeat = base / "ym-repeat"
    repeat_result = subprocess.run([sys.executable, str(runner), "--core", str(core), "--rom", str(ym_cart),
                                    "--frames", "8", "--out", str(ym_repeat), "--trace"],
                                   capture_output=True, text=True, timeout=60)
    if repeat_result.returncode:
        sys.exit("oracle_check: repeated YM run failed: " + repeat_result.stderr)
    for filename in ("audio.raw", "ym2612.raw", "psg.raw", "bus.trace"):
        if (out / filename).read_bytes() != (ym_repeat / filename).read_bytes():
            sys.exit("oracle_check: repeated YM output differs: " + filename)
print("oracle_check: ok")
