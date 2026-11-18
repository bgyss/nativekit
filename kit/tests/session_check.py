#!/usr/bin/env python3
"""session_check: the recorder (--record) and the frame log (--frame-log) on
   the session console, windowed on the null host with keys from
   PORT_EVENTS. The play's buttons as script lines frame by frame, the
   recorder's marks, its pause and single steps; a headless replay of the
   recorded input matching every frame of the play's log; and planted
   errors: an input change dropped from the recording (caught at its frame),
   the host's clock in the machine (caught), --record in a headless run
   (refused). With --sound the log counts the sound file's samples."""
import os, subprocess, sys, tempfile

CONSOLE = os.path.abspath(sys.argv[1])
fails = 0

F1, F5, F6, F7, F9, F10 = 0x70, 0x74, 0x75, 0x76, 0x78, 0x79
A, B, RET = ord("A"), ord("B"), 0x0D


def fail(msg):
    global fails
    print("session_check: " + msg)
    fails += 1


def run(args, env=None, expect=0):
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run([CONSOLE] + args, capture_output=True, text=True, env=e)
    if r.returncode != expect:
        fail("%s: exit %d, wanted %d: %s" % (" ".join(args), r.returncode, expect, r.stderr.strip()))
    return r


def log_lines(path):
    return [l.split() for l in open(path) if l.startswith("F ")]


def frames_of(script):
    """the script's buttons for each frame (wait and hold lines)"""
    out = []
    for line in open(script):
        w = line.split("#")[0].split()
        if not w:
            continue
        if w[0] == "wait":
            out += [""] * int(w[1])
        elif w[0] == "hold":
            out += [w[1]] * int(w[2])
        else:
            fail("a recorded line not wait or hold: " + line.strip())
    return out


def first_difference(a, b):
    for x, y in zip(a, b):
        if x != y:
            return int(x[1])
    return None if len(a) == len(b) else min(len(a), len(b)) + 1


def record(d, events, env=None):
    ev = os.path.join(d, "events.txt")
    with open(ev, "w") as f:
        for n, vk, down in events:
            f.write("%d %d %d\n" % (n, vk, down))
    e = {"PORT_EVENTS": ev, "PORT_SAVEDIR": d}
    e.update(env or {})
    rec = os.path.join(d, "rec")
    run(["--record", rec], e)
    return rec


with tempfile.TemporaryDirectory() as poll_dir:
    polls = os.path.join(poll_dir, "polls.bin")
    script = os.path.join(poll_dir, "input.nks")
    with open(script, "w") as stream:
        stream.write("hold A 3\nwait 3\n")
    recorded = os.path.join(poll_dir, "record.log")
    replayed = os.path.join(poll_dir, "replay.log")
    run(["--headless", "--frames", "6", "--script", script,
         "--poll-log", polls, "--frame-log", recorded])
    run(["--headless", "--frames", "6", "--replay-polls", polls, "--frame-log", replayed])
    if [x[2] for x in log_lines(recorded)] != [x[2] for x in log_lines(replayed)]:
        fail("poll-indexed replay changed a picture")
    with open(polls, "r+b") as stream:
        stream.seek(4)
        stream.write(bytes([1]))
    run(["--headless", "--frames", "6", "--replay-polls", polls], expect=70)


EVENTS = [
    (10, A, 1), (20, B, 1), (25, B, 0), (30, F5, 1), (30, F5, 0), (40, A, 0),
    (60, F6, 1), (70, F7, 1), (80, F1, 1),
    (100, F9, 1), (105, F10, 1), (110, F10, 1), (120, F9, 1),
    (150, RET, 1), (152, RET, 0), (200, A, 1), (200, B, 1), (230, A, 0), (230, B, 0),
]

d = tempfile.mkdtemp()
rec = record(d, EVENTS)
play = log_lines(os.path.join(rec, "frames.log"))
# 400 passes of the loop, 20 held by the pause (passes 101-120) less the 2 steps
if len(play) != 382:
    fail("%d frames logged, wanted 382" % len(play))
if [int(l[1]) for l in play] != list(range(1, len(play) + 1)):
    fail("the play's log does not count frames 1, 2, ...")

buttons = frames_of(os.path.join(rec, "input.nks"))
if len(buttons) != len(play):
    fail("%d frames of input for %d frames run" % (len(buttons), len(play)))
want = {11: "A", 21: "A+B", 25: "A+B", 26: "A", 40: "A", 41: "", 10: ""}
for f, b in want.items():
    if f <= len(buttons) and buttons[f - 1] != b:
        fail("frame %d ran with '%s', wanted '%s'" % (f, buttons[f - 1], b))
# Return down in pass 150, when 132 frames had run: frames 133 and 134 hold START
for f, b in {132: "", 133: "START", 134: "START", 135: ""}.items():
    if buttons[f - 1] != b:
        fail("frame %d ran with '%s', wanted '%s'" % (f, buttons[f - 1], b))

marks = [l.split()[:-1] for l in open(os.path.join(rec, "marks.log"))]
if marks != [["mark", "30"], ["range", "60", "70"]]:
    fail("marks %s, wanted mark 30 and range 60 70" % marks)

# the replay matches every frame
rlog = os.path.join(d, "replay.log")
run(["--headless", "--script", os.path.join(rec, "input.nks"), "--frame-log", rlog])
replay = log_lines(rlog)
at = first_difference(play, replay)
if at is not None:
    fail("the replay differs from the play at frame %s" % at)

# planted: the A+B frames recorded as A alone
text = open(os.path.join(rec, "input.nks")).read()
if "hold A+B 5\n" not in text:
    fail("no 'hold A+B 5' line in the recording")
else:
    bad = os.path.join(d, "dropped.nks")
    open(bad, "w").write(text.replace("hold A+B 5\n", "hold A 5\n", 1))
    blog = os.path.join(d, "dropped.log")
    run(["--headless", "--script", bad, "--frame-log", blog])
    at = first_difference(play, log_lines(blog))
    if at != 21:
        fail("a dropped input change found at frame %s, wanted 21" % at)

# planted: the host's clock in the machine
d2 = tempfile.mkdtemp()
rec2 = record(d2, EVENTS[:6], {"SESSION_NONDET": "1", "SESSION_QUIT": "80"})
run(["--headless", "--script", os.path.join(rec2, "input.nks"), "--frame-log", os.path.join(d2, "r.log")],
    {"SESSION_NONDET": "1"})
if first_difference(log_lines(os.path.join(rec2, "frames.log")), log_lines(os.path.join(d2, "r.log"))) is None:
    fail("a machine stirred by the clock replayed the same")

# refused: --record in a headless run
r = run(["--headless", "--record", os.path.join(d, "x")], expect=70)
if "--record is for a windowed run" not in r.stderr:
    fail("--record headless: " + r.stderr.strip())

# --sound: the log counts the file's samples (none before the first input)
snd = os.path.join(d, "s.raw")
slog = os.path.join(d, "s.log")
run(["--headless", "--script", os.path.join(rec, "input.nks"), "--frame-log", slog, "--sound", snd])
counts = [int(l[4]) for l in log_lines(slog)]
if counts[:3] != [0, 4, 8] or counts[-1] != 4 * (len(counts) - 1):
    fail("sample counts %s ... %s" % (counts[:3], counts[-1:]))
if os.path.getsize(snd) != 4 * counts[-1]:
    fail("the sound file holds %d bytes, the log says %d samples" % (os.path.getsize(snd), counts[-1]))
head = [l for l in open(slog) if l.startswith("#")]
if head[:2] != ["# nk frame log 1\n", "# console session_console\n"] or "# regions .state .frames .players\n" not in head:
    fail("the log's head: %s" % head)

print("session_check: FAIL" if fails else "session_check: ok")
sys.exit(1 if fails else 0)
