#!/usr/bin/env python3
"""A clock rebase that lands inside a 68000 instruction must not keep the
reference core inside one frame call.

The core subtracts a large constant from every component clock about once
every 4,783 NTSC frames, when a frame ends. The generated 68000 execute loop
keeps its target cycle in a local, so when the frame ended inside an
instruction (a VDP port access synchronises the VDP) the loop was left with a
target the rebased clock could not reach: retro_run went on presenting
frames and never returned (first seen on a title at frame 4,783). Patch 0004
moves the loop's target by the same amount. The cartridge is authored: the
68000 does nothing but read the VDP status port, as a program waiting for
vertical blank does, so nearly every frame ends inside a read, and the run asks for a rebase at the end of its fourth frame. The
oracle runner requires one frame per retro_run call; on the unpatched core
the fourth call presents three (the capture runner cannot show this: it
stops by counting frames, not calls).
"""
import hashlib
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import census_fixture  # noqa: E402

ORACLE = census_fixture.ROOT / "consoles/md/tests/oracle/run.py"

FRAMES = 6
REBASE_FRAME = 4

M68K = """\
        org     $200
start:  lea     ($C00004).l,a5
again:
        rept    64
        move.w  (a5),d0
        endr
        bra.w   again
        org     $3F0
stray:  rte
"""


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def build(directory):
    directory.mkdir(parents=True, exist_ok=True)
    code = census_fixture._assemble("m68k", "vasmm68k_mot", M68K, directory, ("-m68000", "-no-opt"))
    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x3F0).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT REBASE STALL CONTROL".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT REBASE STALL CONTROL".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x200 + len(code)] = code
    rom_path = directory / "rebase-stall.bin"
    rom_path.write_bytes(rom)
    return rom_path


def run(rom_path, out, rebase_frame):
    """Run the oracle runner; return (error text or None, digest of frames and audio)."""
    if not census_fixture.CORE.is_file():
        raise RuntimeError("the pinned reference core is not built")
    command = [sys.executable, str(ORACLE), "--core", str(census_fixture.CORE), "--rom", str(rom_path),
               "--frames", str(FRAMES), "--pad1", "3", "--pad2", "3", "--out", str(out)]
    if rebase_frame:
        command += ["--test-rebase-frame", str(rebase_frame)]
    try:
        done = subprocess.run(command, capture_output=True, text=True, timeout=120, cwd=ORACLE.parent)
    except subprocess.TimeoutExpired:
        return "timed out", None
    if done.returncode:
        return (done.stderr.strip().splitlines() or ["failed"])[-1], None
    digest = hashlib.sha256()
    names = sorted(path.name for path in out.glob("frame_*.ppm"))
    require(len(names) == FRAMES, "%d frame files for %d calls" % (len(names), FRAMES))
    for name in names + ["audio.raw"]:
        digest.update((out / name).read_bytes())
    return None, digest.hexdigest()


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-rebase-stall-") as temp:
        temp = pathlib.Path(temp)
        rom_path = build(temp)
        error, plain = run(rom_path, temp / "plain", None)
        require(error is None, "the control without a rebase failed: %s" % error)
        error, rebased = run(rom_path, temp / "rebased", REBASE_FRAME)
        require(error is None, "a rebase inside an instruction kept the core in one call: %s" % error)
        # a rebase only relabels clocks: the same picture and sound come out
        require(rebased == plain, "the rebase changed the picture or the sound")
    print("rebase_stall_check: ok (%d frames in %d calls with a rebase at the end of frame %d; "
          "picture and sound equal to the run without it)" % (FRAMES, FRAMES, REBASE_FRAME))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("rebase_stall_check: " + str(exc))
