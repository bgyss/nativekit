#!/usr/bin/env python3
"""session_check: session.py on the kit's session console (null host,
   PORT_EVENTS keys). record notes the binary and an environment file and
   keeps the recorder's files; marks lists them; verify passes a true play
   and finds a dropped input change at its frame; extract writes the range
   with its context as PNG (each decoded and hashed back to the play's
   picture hash), WAV (the range's samples exactly) and dumps. Planted: a
   changed binary and a changed environment file are refused."""
import json, os, struct, subprocess, sys, tempfile, zlib
from pathlib import Path

CONSOLE = Path(sys.argv[1]).resolve()
TOOL = Path(__file__).resolve().parent / "session.py"
fails = 0


def fail(msg):
    global fails
    print("session_check: " + msg)
    fails += 1


def tool(*args, env=None, expect=0):
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run([sys.executable, str(TOOL)] + [str(a) for a in args], capture_output=True, text=True, env=e)
    if r.returncode != expect:
        fail("%s: exit %d, wanted %d: %s %s" % (args[0], r.returncode, expect, r.stdout.strip(), r.stderr.strip()))
    return r


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def png_picture_hash(path):
    """the kit's picture hash of a PNG's pixels (opaque ARGB, host order)"""
    data = path.read_bytes()
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    px = bytearray()
    for y in range(h):
        row = raw[y * (1 + 3 * w) + 1:(y + 1) * (1 + 3 * w)]
        for x in range(w):
            r, g, b = row[3 * x:3 * x + 3]
            px += struct.pack("<I", 0xFF000000 | r << 16 | g << 8 | b)
    hh = fnv(0xCBF29CE484222325, struct.pack("<i", w))
    hh = fnv(hh, struct.pack("<i", h))
    return "%016x" % fnv(hh, px)


d = Path(tempfile.mkdtemp())
events = d / "events.txt"
events.write_text("10 65 1\n20 66 1\n25 66 0\n30 116 1\n40 65 0\n60 117 1\n70 118 1\n")
cart = d / "cart.bin"
cart.write_bytes(b"not a cartridge")
env = {"PORT_EVENTS": str(events), "PORT_SAVEDIR": str(d), "SESSION_QUIT": "120", "SESSION_CART": str(cart)}
s = d / "s"
tool("record", "--out", s, "--env", "SESSION_CART", "--", CONSOLE, env=env)
meta = json.loads((s / "session.json").read_text())
if meta.get("frames") != 120 or meta.get("console") != "session_console":
    fail("session.json: %s frames, console %s" % (meta.get("frames"), meta.get("console")))
if not meta["env"]["SESSION_CART"]["sha256"] or meta["sources"][0].get("commit") is None:
    fail("session.json lacks the environment file's hash or nativekit's commit")
for name in ("input.nks", "frames.log", "marks.log", meta["binary"]["copy"]):
    if not (s / name).exists():
        fail("no " + name)
tool("record", "--out", s, "--", CONSOLE, env=env, expect=2)   # refused: not empty

out = tool("marks", s).stdout
if "--range 30-30" not in out or "--range 60-70" not in out:
    fail("marks: " + out)

out = tool("verify", s).stdout
if "replays exactly: 120 frames" not in out:
    fail("verify: " + out)

# planted: an input change dropped
nks = s / "input.nks"
good = nks.read_text()
nks.write_text(good.replace("hold A+B 5\n", "hold A 5\n"))
out = tool("verify", s, expect=1).stdout
if "first difference at frame 21 (picture and state)" not in out:
    fail("verify of a dropped change: " + out)
nks.write_text(good)

r = tool("extract", s, "--range", "60-70", "--context", "3")
x = s / "extract" / "60-70"
index = json.loads((x / "index.json").read_text())
frames = index["frames"]
if [f["frame"] for f in frames] != list(range(57, 74)) or not index["all_match_play"]:
    fail("extract: frames %s, all match %s" % ([f["frame"] for f in frames][:3], index["all_match_play"]))
if [f["frame"] for f in frames if f["in_range"]] != list(range(60, 71)):
    fail("extract: the range's frames")
play = {int(l.split()[1]): l.split()[2] for l in (s / "frames.log").read_text().splitlines() if l.startswith("F ")}
for f in frames:
    got = png_picture_hash(x / f["png"])
    if got != play[f["frame"]]:
        fail("frame %d's PNG hashes %s, the play %s" % (f["frame"], got, play[f["frame"]]))
        break
    if len(f["dumps"]) != 3:
        fail("frame %d: dumps %s" % (f["frame"], f["dumps"]))
        break
state = (x / "dumps" / "f000060.frames").read_bytes()
if int.from_bytes(state, "little") != 60:
    fail("frame 60's dump says %d frames" % int.from_bytes(state, "little"))
# 4 samples a frame after the first: frames 57-73 make 17 * 4
wav = (x / "sound.wav").read_bytes()
if wav[:4] != b"RIFF" or len(wav) - 44 != 17 * 4 * 4:
    fail("sound.wav holds %d bytes of samples, wanted %d" % (len(wav) - 44, 17 * 16))
if "frames 57-73" not in r.stdout or not (x / "index.html").exists():
    fail("extract: " + r.stdout)

# planted: another binary, another cartridge
copy = s / meta["binary"]["copy"]
keep = copy.read_bytes()
copy.write_bytes(keep + b"\0")
if "not the binary the play ran" not in tool("verify", s, expect=2).stderr:
    fail("a changed binary was not refused")
copy.write_bytes(keep)
cart.write_bytes(b"another cartridge")
if "is not the file the play had" not in tool("verify", s, expect=2).stderr:
    fail("a changed environment file was not refused")

print("session_check: FAIL" if fails else "session_check: ok")
sys.exit(1 if fails else 0)
