#!/usr/bin/env python3
"""session.py: record a play on a kit console, replay it exactly, and pull
out a frame range (docs/superpowers/plans/2026-11-17-session-record-replay.md,
phases P1 and P2).

  session.py record --out DIR [--env NAME]... [--source REPO]... -- BINARY [ARG]...
      Copies BINARY into DIR/bin, notes its identity (SHA-256, the git
      commits of nativekit and of each --source repository, each --env
      variable and the SHA-256 of a file it names) in DIR/session.json, and
      runs the copy windowed with --record DIR. Play; the recorder's keys:
      F1 frame counter, F5 mark, F6/F7 range start/end, F9 pause, F10 step.

  session.py marks DIR
      The play's marks and ranges, with the extract command for each.

  session.py verify DIR
      Replays the play headless and checks every frame's picture and state
      against the play's log. A play that does not replay exactly is
      reported at its first differing frame.

  session.py extract DIR --range A-B [--context K] [--out OUT] [--no-dumps]
      Replays to frame B+K and writes frames A-K to B+K as PNG, the range's
      sound as WAV, each frame's memories (the console's dump regions), an
      index.json and an index.html to look through them. Each extracted
      frame is checked against the play's log.

A session holds input, marks, hashes and the binary it was played on. The
binary holds whatever the game's build put in it, and extracts are pictures,
sound and memory of the game: keep sessions out of version control and do
not publish them.
"""
import argparse
import datetime
import hashlib
import html
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class SessionError(Exception):
    pass


# ---- files ----

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def git_identity(repo):
    def git(*args):
        r = subprocess.run(["git", "-C", str(repo)] + list(args), capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None
    head = git("rev-parse", "HEAD")
    if head is None:
        return {"path": str(repo), "commit": None}
    return {"path": str(repo), "commit": head, "dirty": bool(git("status", "--porcelain", "--untracked-files=no"))}


def read_log(path):
    """a frame log (kit/include/nk/session.h): its head and its frames"""
    head, frames = {}, []
    with open(path) as f:
        for line in f:
            if line.startswith("# "):
                words = line[2:].split()
                if words[:3] == ["nk", "frame", "log"]:
                    head["version"] = int(words[3])
                elif words:
                    head[words[0]] = words[1:]
            elif line.startswith("F "):
                w = line.split()
                frames.append({"frame": int(w[1]), "picture": w[2], "state": w[3],
                               "samples": int(w[4]) if len(w) > 4 else None})
    if head.get("version") != 1:
        raise SessionError("%s is not a version 1 frame log" % path)
    return head, frames


def read_input(path):
    """a recorded input script: the buttons of each frame ('' none)"""
    out = []
    with open(path) as f:
        for number, line in enumerate(f, 1):
            w = line.split("#")[0].split()
            if not w:
                continue
            if w[0] == "wait" and len(w) == 2:
                out += [""] * int(w[1])
            elif w[0] == "hold" and len(w) == 3:
                out += [w[1]] * int(w[2])
            else:
                raise SessionError("%s:%d: not a recorded line: %s" % (path, number, line.strip()))
    return out


def read_marks(path):
    marks = []
    if not path.exists():
        return marks
    for line in path.read_text().splitlines():
        w = line.split()
        if w and w[0] == "mark":
            marks.append({"kind": "mark", "first": int(w[1]), "last": int(w[1]), "seconds": float(w[2])})
        elif w and w[0] == "range":
            marks.append({"kind": "range", "first": int(w[1]), "last": int(w[2]), "seconds": float(w[3])})
    return marks


def script_for(buttons, upto, takes):
    """a script of frames 1..upto's buttons; after frame f, takes(f)'s lines"""
    lines, run, run_n = [], None, 0

    def flush():
        nonlocal run_n
        if run_n:
            lines.append("hold %s %d" % (run, run_n) if run else "wait %d" % run_n)
            run_n = 0
    for f in range(1, upto + 1):
        b = buttons[f - 1]
        if run_n and b != run:
            flush()
        run = b
        run_n += 1
        extra = takes(f)
        if extra:
            flush()
            lines += extra
    flush()
    return "\n".join(lines) + "\n"


def write_png(path, ppm):
    data = Path(ppm).read_bytes()
    parts = data.split(b"\n", 3)
    if parts[0] != b"P6" or parts[2] != b"255":
        raise SessionError("%s is not a P6 picture" % ppm)
    w, h = (int(x) for x in parts[1].split())
    pixels = parts[3]
    raw = b"".join(b"\0" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    Path(path).write_bytes(png)
    return w, h


def write_wav(path, raw, first_sample, last_sample, rate):
    """stereo 16-bit samples [first_sample, last_sample) of the raw file"""
    with open(raw, "rb") as f:
        f.seek(4 * first_sample)
        data = f.read(4 * (last_sample - first_sample))
    if sys.byteorder != "little":
        data = b"".join(struct.pack("<h", v) for v in struct.unpack("=%dh" % (len(data) // 2), data))
    head = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE", b"fmt ", 16, 1, 2, rate,
                       rate * 4, 4, 16, b"data", len(data))
    Path(path).write_bytes(head + data)


# ---- the session ----

def load_session(d):
    d = Path(d)
    meta_path = d / "session.json"
    if not meta_path.exists():
        raise SessionError("%s has no session.json (made by session.py record)" % d)
    meta = json.loads(meta_path.read_text())
    if meta.get("version") != 1:
        raise SessionError("%s: unsupported session version" % meta_path)
    return d, meta


def replay_env(d, meta):
    env = dict(os.environ)
    env.pop("PORT_EVENTS", None)
    for name, v in meta["env"].items():
        if v.get("sha256") and (not os.path.isfile(v["value"]) or sha256(v["value"]) != v["sha256"]):
            raise SessionError("%s=%s is not the file the play had (SHA-256 %s)" % (name, v["value"],
                                                                                    v["sha256"][:12]))
        env[name] = v["value"]
    return env


def binary_of(d, meta):
    b = d / meta["binary"]["copy"]
    if not b.exists() or sha256(b) != meta["binary"]["sha256"]:
        raise SessionError("%s is missing or not the binary the play ran" % b)
    return b


def replay(d, meta, script, out_dir, log, sound=None):
    args = [str(binary_of(d, meta)), "--headless", "--script", str(script), "--out", str(out_dir),
            "--frame-log", str(log)]
    if sound:
        args += ["--sound", str(sound)]
    r = subprocess.run(args, env=replay_env(d, meta), capture_output=True, text=True)
    if r.returncode != 0:
        raise SessionError("the replay failed (exit %d): %s" % (r.returncode, r.stderr.strip()[-2000:]))


def first_difference(play, again):
    for a, b in zip(play, again):
        if a["frame"] != b["frame"]:
            return a["frame"], "frame numbering"
        what = [k for k in ("picture", "state") if a[k] != b[k]]
        if what:
            return a["frame"], " and ".join(what)
    if len(play) != len(again):
        return min(len(play), len(again)) + 1, "length (%d frames played, %d replayed)" % (len(play), len(again))
    return None


# ---- commands ----

def cmd_record(args):
    out = Path(args.out)
    if out.exists() and any(out.iterdir()):
        raise SessionError("%s exists and is not empty" % out)
    if not args.command:
        raise SessionError("no binary: session.py record --out DIR -- BINARY [ARG]...")
    binary = Path(args.command[0])
    if not binary.is_file():
        raise SessionError("%s is not a file" % binary)
    (out / "bin").mkdir(parents=True, exist_ok=True)
    copy = out / "bin" / binary.name
    shutil.copy2(binary, copy)
    env = {}
    for name in args.env:
        if name not in os.environ:
            raise SessionError("--env %s: not set" % name)
        v = os.environ[name]
        env[name] = {"value": v, "sha256": sha256(v) if os.path.isfile(v) else None}
    meta = {
        "version": 1,
        "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "binary": {"source": str(binary.resolve()), "copy": "bin/" + binary.name, "sha256": sha256(copy)},
        "args": args.command[1:],
        "env": env,
        "sources": [git_identity(ROOT)] + [git_identity(Path(s).resolve()) for s in args.source],
    }
    (out / "session.json").write_text(json.dumps(meta, indent=2) + "\n")
    print("session: recording to %s (F1 counter, F5 mark, F6/F7 range, F9 pause, F10 step)" % out)
    r = subprocess.run([str(copy)] + args.command[1:] + ["--record", str(out), "--poll-log", str(out / "polls.bin")])
    head, frames = read_log(out / "frames.log")
    meta["console"] = head.get("console", ["?"])[0]
    meta["frames"] = len(frames)
    meta["exit"] = r.returncode
    (out / "session.json").write_text(json.dumps(meta, indent=2) + "\n")
    marks = read_marks(out / "marks.log")
    print("session: %d frames, %d marks; exit %d" % (len(frames), len(marks), r.returncode))
    return 0


def cmd_marks(args):
    d, meta = load_session(args.dir)
    marks = read_marks(d / "marks.log")
    print("%s: %s frames" % (d, meta.get("frames", "?")))
    if not marks:
        print("  no marks")
    for i, m in enumerate(marks, 1):
        span = str(m["first"]) if m["kind"] == "mark" else "%d-%d" % (m["first"], m["last"])
        print("  %d. %-5s frame%s %-11s at %6.1f s   extract: session.py extract %s --range %d-%d" % (
            i, m["kind"], "s" if m["kind"] == "range" else " ", span, m["seconds"], d, m["first"], m["last"]))
    return 0


def cmd_verify(args):
    d, meta = load_session(args.dir)
    _, play = read_log(d / "frames.log")
    work = d / "verify"
    work.mkdir(exist_ok=True)
    replay(d, meta, d / "input.nks", work, work / "frames.log")
    _, again = read_log(work / "frames.log")
    diff = first_difference(play, again)
    if diff:
        print("verify: %s does NOT replay: first difference at frame %d (%s)" % (d, diff[0], diff[1]))
        return 1
    print("verify: %s replays exactly: %d frames, every picture and state the same" % (d, len(play)))
    return 0


def cmd_extract(args):
    d, meta = load_session(args.dir)
    head, play = read_log(d / "frames.log")
    buttons = read_input(d / "input.nks")
    try:
        a, b = (int(x) for x in args.range.split("-"))
    except ValueError:
        raise SessionError("--range A-B, frames A to B")
    if not 1 <= a <= b:
        raise SessionError("--range: 1 <= A <= B")
    last_played = min(len(play), len(buttons))
    if a > last_played:
        raise SessionError("--range starts after the play's last frame (%d)" % last_played)
    lo, hi = max(1, a - args.context), min(last_played, b + args.context)
    out = Path(args.out) if args.out else d / "extract" / ("%d-%d" % (a, b))
    raw = out / "raw"
    for sub in (out, raw, out / "frames"):
        sub.mkdir(parents=True, exist_ok=True)
    if not args.no_dumps:
        (out / "dumps").mkdir(exist_ok=True)

    def takes(f):
        if lo <= f <= hi:
            return ["shot f%06d" % f] + ([] if args.no_dumps else ["dump f%06d" % f])
        return []
    script = out / "replay.nks"
    script.write_text(script_for(buttons, hi, takes))
    replay(d, meta, script, raw, raw / "frames.log", raw / "sound.raw")
    _, again = read_log(raw / "frames.log")

    by_frame = {f["frame"]: f for f in again}
    regions = head.get("regions", [])
    frames, mismatched = [], []
    for f in range(lo, hi + 1):
        mine, theirs = by_frame.get(f), play[f - 1]
        same = bool(mine) and mine["picture"] == theirs["picture"] and mine["state"] == theirs["state"]
        if not same:
            mismatched.append(f)
        png = out / "frames" / ("f%06d.png" % f)
        w, h = write_png(png, raw / ("f%06d.ppm" % f))
        (raw / ("f%06d.ppm" % f)).unlink()
        entry = {"frame": f, "in_range": a <= f <= b, "png": str(png.relative_to(out)), "width": w, "height": h,
                 "picture": theirs["picture"], "state": theirs["state"], "buttons": buttons[f - 1],
                 "matches_play": same}
        if not args.no_dumps:
            entry["dumps"] = []
            for ext in regions:
                src = raw / ("f%06d%s" % (f, ext))
                if src.exists():
                    dst = out / "dumps" / src.name
                    src.replace(dst)
                    entry["dumps"].append(str(dst.relative_to(out)))
        frames.append(entry)

    rate = int(round(float(head.get("audio_hz", ["0"])[0])))
    first_sample = by_frame[lo - 1]["samples"] if lo > 1 else 0
    last_sample = by_frame[hi]["samples"]
    write_wav(out / "sound.wav", raw / "sound.raw", first_sample, last_sample, rate)
    (raw / "sound.raw").unlink()

    marks = [m for m in read_marks(d / "marks.log") if m["last"] >= lo and m["first"] <= hi]
    index = {"session": str(d.resolve()), "range": [a, b], "context": [lo, hi], "console": meta.get("console"),
             "audio_hz": rate, "sound": "sound.wav", "sound_first_frame": lo, "marks": marks, "frames": frames,
             "all_match_play": not mismatched}
    (out / "index.json").write_text(json.dumps(index, indent=2) + "\n")
    write_index_html(out, index)
    print("extract: frames %d-%d (range %d-%d) to %s" % (lo, hi, a, b, out))
    if mismatched:
        print("extract: %d frames do NOT match the play, first %d: the session does not replay exactly"
              % (len(mismatched), mismatched[0]))
        return 1
    print("extract: every frame matches the play")
    return 0


def write_index_html(out, index):
    rows = []
    for f in index["frames"]:
        cls = "in" if f["in_range"] else "ctx"
        bad = "" if f["matches_play"] else " bad"
        rows.append('<figure class="%s%s"><img src="%s" width="%d" height="%d" alt="frame %d">'
                    '<figcaption>%d<span>%s</span></figcaption></figure>'
                    % (cls, bad, html.escape(f["png"]), f["width"], f["height"], f["frame"], f["frame"],
                       html.escape(f["buttons"] or "-")))
    marks = "".join("<li>%s %s</li>" % (m["kind"], m["first"] if m["kind"] == "mark"
                                         else "%d-%d" % (m["first"], m["last"])) for m in index["marks"])
    page = """<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Frames %d-%d</title>
<style>
:root { --bg: #fff; --fg: #111; --muted: #666; --hi: #c33; }
@media (prefers-color-scheme: dark) { :root { --bg: #111; --fg: #eee; --muted: #999; --hi: #f66; } }
body { background: var(--bg); color: var(--fg); font: 14px system-ui, sans-serif; margin: 16px; }
.grid { display: flex; flex-wrap: wrap; gap: 8px; }
figure { margin: 0; } figure img { image-rendering: pixelated; width: 320px; height: auto; display: block; }
figure.ctx { opacity: .55; } figure.bad figcaption { color: var(--hi); }
figcaption { font: 12px ui-monospace, monospace; color: var(--muted); display: flex; justify-content: space-between; }
</style></head><body>
<h1>Frames %d-%d <small>(context %d-%d)</small></h1>
<p>Console %s. Every frame matches the play: %s. Marks here: </p><ul>%s</ul>
<audio controls src="%s"></audio>
<div class="grid">%s</div>
</body></html>
""" % (index["range"][0], index["range"][1], index["range"][0], index["range"][1], index["context"][0],
       index["context"][1], html.escape(str(index["console"])), "yes" if index["all_match_play"] else "NO",
       marks or "<li>none</li>", index["sound"], "\n".join(rows))
    (out / "index.html").write_text(page)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record")
    r.add_argument("--out", required=True)
    r.add_argument("--env", action="append", default=[], help="an environment variable the run reads (MD_ROM)")
    r.add_argument("--source", action="append", default=[], help="a git repository the binary was built from")
    r.add_argument("command", nargs=argparse.REMAINDER)
    m = sub.add_parser("marks")
    m.add_argument("dir")
    v = sub.add_parser("verify")
    v.add_argument("dir")
    e = sub.add_parser("extract")
    e.add_argument("dir")
    e.add_argument("--range", required=True)
    e.add_argument("--context", type=int, default=0)
    e.add_argument("--out")
    e.add_argument("--no-dumps", action="store_true")
    args = p.parse_args(argv)
    if args.cmd == "record" and args.command[:1] == ["--"]:
        args.command = args.command[1:]
    try:
        return {"record": cmd_record, "marks": cmd_marks, "verify": cmd_verify, "extract": cmd_extract}[args.cmd](args)
    except SessionError as err:
        print("session: %s" % err, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
