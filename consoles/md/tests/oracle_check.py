#!/usr/bin/env python3
"""Smoke-test the candidate runner with a newly generated cartridge."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "oracle"))
from trace import M68K, FETCH, READ, WRITE, read as read_trace

root = pathlib.Path(__file__).resolve().parents[3]
runner = root / "consoles/md/tests/oracle/run.py"
extension = "dylib" if sys.platform == "darwin" else "so"
core = root / "build/toolchains/blastem" / ("libblastem." + extension)

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
        frame = (out / "frame_000003.ppm").read_bytes()
        if (manifest["frames"] != 3 or manifest["frame_sizes"] != [[283, 243]] * 3
                or not frame.startswith(b"P6\n283 243\n255\n")):
            sys.exit("oracle_check: missing frame")
        if manifest["sample_rate"] <= 0 or not (out / "audio.raw").stat().st_size:
            sys.exit("oracle_check: missing audio")
        outputs.append((frame, (out / "audio.raw").read_bytes(), manifest))
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
    subprocess.run(["cc", link_flag, "-fPIC", str(fake_source), "-o", str(fake_library)], check=True)
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
        move.b #$40,$a10009
        move.b #0,$a10003
loop:   move.l #$c0000000,$c00004
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
    asm.write_text("        move.w #$1234,$00ff0000\n"
                   "        move.w $00ff0000,d0\n"
                   "loop:   bra.s loop\n")
    bus_program = base / "bus.bin"
    assembled = subprocess.run([str(assembler), "-Fbin", "-o", str(bus_program), str(asm)],
                               capture_output=True, text=True)
    if assembled.returncode:
        sys.exit("oracle_check: bus fixture did not assemble: " + assembled.stderr)
    bus_rom = bytearray(rom)
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
    if bus_manifest.get("trace_capabilities") != ["m68k_cpu_access"] or bus_manifest.get("trace_events") != len(events):
        sys.exit("oracle_check: trace coverage metadata mismatch")
    if not any(e.device == M68K and e.operation == WRITE and e.address == 0xFF0000 and e.value == 0x1234
               for e in events):
        sys.exit("oracle_check: missing work-RAM write event")
    if not any(e.device == M68K and e.operation == READ and e.address == 0xFF0000 and e.value == 0x1234
               for e in events):
        sys.exit("oracle_check: missing work-RAM read event")
    if not any(e.device == M68K and e.operation == FETCH and e.address == 0x200 for e in events):
        sys.exit("oracle_check: missing explicit prefetch event: " + repr(events[:12]))
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
                             "--frames", "8", "--out", str(out)], capture_output=True, text=True, timeout=60)
    if result.returncode:
        sys.exit("oracle_check: PSG run failed: " + result.stderr)
    samples = (out / "audio.raw").read_bytes()
    if not any(samples):
        sys.exit("oracle_check: PSG tone produced only silence")
print("oracle_check: ok")
