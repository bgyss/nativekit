#!/usr/bin/env python3
"""Configure, build, and run the standalone nmd consumer projects.

md_consumer is the 1.0 idle cart. md_native is a game with native execution:
it must boot through nmd_main from its reset vector, run main logic over
many frames, follow the script's changing input, take interrupts, drive the
Z80's sound work, and repeat exactly. The same assertions must report each
planted error (NMD_NATIVE_PLANT) and an idle cart as failures.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
compiler = os.environ.get("CC")
if not compiler and sys.platform == "darwin":
    compiler = "/usr/bin/clang"
compiler_words = shlex.split(compiler) if compiler else ["cc"]
if not compiler_words:
    raise SystemExit("consumer: CC must name a C compiler")
env = os.environ.copy()
env["CC"] = compiler or compiler_words[0]
env.pop("NMD_NATIVE_PLANT", None)
build = ROOT / "build" / ("md-consumer-" + pathlib.Path(compiler_words[0]).name)
suffix = ".exe" if sys.platform == "win32" else ""

subprocess.run(["cmake", "-S", str(HERE), "-B", str(build),
                "-DNATIVEKIT_ROOT=" + str(ROOT)], check=True, env=env)
subprocess.run(["cmake", "--build", str(build), "-j8"], check=True, env=env)
subprocess.run([str(build / ("md_consumer" + suffix)), "--headless", "--frames", "5"],
               check=True, env=env)

BUTTONS = {"UP": 0x01, "DOWN": 0x02, "LEFT": 0x04, "RIGHT": 0x08,
           "B": 0x10, "C": 0x20, "A": 0x40, "START": 0x80}
# (buttons held, frames); a dump follows the steps marked with a name
STEPS = [("", 10, None), ("A", 20, "one"), ("B+RIGHT", 15, None),
         ("START", 5, None), ("", 10, "two"), ("UP+C", 8, "three")]
DUMPS = [name for _, _, name in STEPS if name]


def script_text():
    lines = []
    for held, count, name in STEPS:
        lines.append(f"hold {held} {count}" if held else f"wait {count}")
        if name:
            lines.append(f"dump {name}")
            lines.append(f"shot {name}")
    return "\n".join(lines) + "\n"


def frame_pads(upto):
    """The pad byte the script holds during each frame, up to dump `upto`."""
    pads = []
    for held, count, name in STEPS:
        mask = 0
        for button in filter(None, held.split("+")):
            mask |= BUTTONS[button]
        pads += [mask] * count
        if name == upto:
            return pads
    raise AssertionError(upto)


def run_native(directory, plant=None):
    directory.mkdir(parents=True)
    script = directory / "native.script"
    script.write_text(script_text())
    run_env = env.copy()
    if plant:
        run_env["NMD_NATIVE_PLANT"] = plant
    return subprocess.run(
        [str(build / ("md_native" + suffix)), "--headless", "--script", str(script),
         "--out", str(directory), "--sound", str(directory / "sound.raw")],
        env=run_env, capture_output=True, text=True)


def be(data, at, size):
    return int.from_bytes(data[at:at + size], "big")


def examine(directory, result):
    """Every reason this run is not a native game that ran; empty when it is."""
    problems = []
    if result.returncode:
        return [f"exit status {result.returncode}: {result.stderr.strip()}"]
    previous_frames = 0
    pictures = []
    for name in DUMPS:
        wram_path = directory / (name + ".wram")
        zram_path = directory / (name + ".zram")
        shot_path = directory / (name + ".ppm")
        if not wram_path.is_file() or not zram_path.is_file() or not shot_path.is_file():
            problems.append(f"{name}: missing dump or shot")
            continue
        wram = wram_path.read_bytes()
        zram = zram_path.read_bytes()
        pictures.append(shot_path.read_bytes())
        if len(wram) != 0x10000 or len(zram) != 0x2000:
            problems.append(f"{name}: dump sizes {len(wram)}, {len(zram)}")
            continue
        pads = frame_pads(name)
        ran = len(pads)
        # The interrupt of the blank that ends frame n is taken when frame
        # n+1 resumes the 68000, and reads that frame's input.
        want_pads = pads[1:]
        vints = be(wram, 6, 2)
        frames = be(wram, 4, 2)
        if wram[0:4] != b"NMD2":
            problems.append(f"{name}: the reset routine left no boot marker")
        if vints != ran - 1:
            problems.append(f"{name}: {vints} vertical interrupts in {ran} frames")
        if frames != ran - 1:
            problems.append(f"{name}: main ran {frames} frames of {ran}")
        if frames <= previous_frames:
            problems.append(f"{name}: main logic did not advance between dumps")
        previous_frames = frames
        history = list(wram[0x101:0x101 + len(want_pads)])
        if history != want_pads:
            problems.append(f"{name}: pad history does not follow the script")
        total = 0
        for pad in want_pads:
            total = (total * 31 + pad) & 0xFFFFFFFF
        if be(wram, 0xA, 4) != total:
            problems.append(f"{name}: main's running sum lost state")
        changes = sum(1 for i, pad in enumerate(want_pads)
                      if i == 0 or pad != want_pads[i - 1])
        if be(wram, 0x10, 2) != changes:
            problems.append(f"{name}: {be(wram, 0x10, 2)} Z80 commands, want {changes}")
        if want_pads and (zram[0x1F00] != want_pads[-1] or zram[0x1F01] != changes & 0xFF):
            problems.append(f"{name}: the Z80 command bytes are not main's last")
        if zram[0x1F02] != changes & 0xFF or zram[0x1F03] != changes & 0xFF:
            problems.append(f"{name}: the Z80 driver played {zram[0x1F03]} of {changes} commands")
        if zram[0x1F04] != (ran - 1) & 0xFF:
            problems.append(f"{name}: {zram[0x1F04]} Z80 interrupts in {ran} frames")
    if len(set(pictures)) != len(DUMPS):
        problems.append("the picture did not follow the input")
    sound_path = directory / "sound.raw"
    sound = sound_path.read_bytes() if sound_path.is_file() else b""
    if not any(sound):
        problems.append("the sound capture is missing or silent")
    return problems


with tempfile.TemporaryDirectory(prefix="nmd-native-") as temp:
    temp = pathlib.Path(temp)
    first = temp / "first"
    second = temp / "second"
    problems = examine(first, run_native(first))
    if problems:
        sys.exit("md_native: " + "; ".join(problems))
    problems = examine(second, run_native(second))
    if problems:
        sys.exit("md_native (second run): " + "; ".join(problems))
    names = sorted(p.name for p in first.iterdir())
    for name in names:
        if (first / name).read_bytes() != (second / name).read_bytes():
            sys.exit(f"md_native: {name} differs between two runs")

    # The gate must fail for an idle cart and for each planted error.
    caught = {}
    for plant in ("skip-reset", "lose-state", "bad-return"):
        directory = temp / plant
        problems = examine(directory, run_native(directory, plant))
        if not problems:
            sys.exit(f"md_native: planted error {plant} passed the execution gate")
        caught[plant] = problems[0]

print(f"md_native: ok ({len(frame_pads(DUMPS[-1]))} frames, {len(names)} files identical "
      f"across two runs)")
for plant, problem in caught.items():
    print(f"md_native: planted {plant} caught: {problem}")
