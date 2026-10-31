#!/usr/bin/env python3
"""states_check: the kit's script runner on the stub console. Steps and
   their errors (with line numbers, the console's own steps, unknown steps
   named with the console's name); dumps of every region (an optional one
   absent, an empty one written empty); save and load with fork and with
   STUB_SCRIPT_NOFORK=1, every dump the same both ways and the machine each
   load names; a load of a state the script never saved, rebuilt from the
   play in STUB_STATES that saves it."""
import filecmp, os, subprocess, sys, tempfile

STUB = os.path.abspath(sys.argv[1])
fails = 0


def run(script, env=None, expect=0):
    global fails
    d = tempfile.mkdtemp()
    s = os.path.join(d, "s.txt")
    with open(s, "w") as f:
        f.write(script)
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run([STUB, "--script", s, "--out", d], capture_output=True, text=True, env=e, cwd=d)
    if r.returncode != expect:
        print("states_check: exit %d, wanted %d: %s" % (r.returncode, expect, r.stderr.strip()))
        fails += 1
    return d, r.stderr


def same(a, b, names):
    global fails
    for n in names:
        if not (os.path.exists(os.path.join(a, n)) and os.path.exists(os.path.join(b, n))):
            print("states_check: no " + n)
            fails += 1
        elif not filecmp.cmp(os.path.join(a, n), os.path.join(b, n), shallow=False):
            print("states_check: %s differs" % n)
            fails += 1


d, _ = run("wait 3\nhold A+2.B 2\npress START\npoke 7\nrepeat 2\nwait 1\nend\ndump d\nshot s\n")
for n in ["d.state", "d.frames", "d.empty", "s.ppm"]:
    if not os.path.exists(os.path.join(d, n)):
        print("states_check: no " + n)
        fails += 1
if os.path.exists(os.path.join(d, "d.none")):
    print("states_check: an optional region written")
    fails += 1
if os.path.getsize(os.path.join(d, "d.empty")) != 0:
    print("states_check: the empty region not empty")
    fails += 1
frames = int.from_bytes(open(os.path.join(d, "d.frames"), "rb").read(), "little")
if frames != 3 + 2 + 2 + 2 + 2:
    print("states_check: %d frames run, wanted 11" % frames)
    fails += 1

_, err = run("wait 1\nfrobnicate 2\n", expect=70)
if "s.txt:2: 'frobnicate' is not a step stub's script runner takes" not in err:
    print("states_check: unknown step message: " + err)
    fails += 1
_, err = run("hold Q 1\n", expect=70)
if "s.txt:1: buttons 'Q'" not in err:
    print("states_check: buttons message: " + err)
    fails += 1
_, err = run("repeat 2\nwait 1\n", expect=70)
if "s.txt:1: repeat without end" not in err:
    print("states_check: repeat message: " + err)
    fails += 1
_, err = run("poke\n", expect=70)
# the line as written, its newline too (the message has always quoted it so)
if "s.txt:1: 'poke\n'" not in err:
    print("states_check: own step message: " + err)
    fails += 1

script = "wait 5\nsave a\nhold A 3\ndump x\nload a.state\nhold B 3\ndump y\nload a.state\ndump z\n"
f1, _ = run(script)
f2, _ = run(script, {"STUB_SCRIPT_NOFORK": "1"})
same(f1, f2, ["x.state", "y.state", "z.state", "x.frames", "y.frames", "z.frames"])
same(f1, f1, ["z.state"])
zs = open(os.path.join(f1, "z.state"), "rb").read()
d0, _ = run("wait 5\ndump w\n")
if open(os.path.join(d0, "w.state"), "rb").read() != zs:
    print("states_check: a load is not the machine its save made")
    fails += 1

plays = tempfile.mkdtemp()
with open(os.path.join(plays, "p.play"), "w") as f:
    f.write("wait 4\nhold B 2\nsave elsewhere\n")
for env in ({}, {"STUB_SCRIPT_NOFORK": "1"}):
    e = dict(env, STUB_STATES=plays)
    d, _ = run("wait 1\nload elsewhere.state\ndump q\n", e)
    d1, _ = run("wait 4\nhold B 2\ndump q\n")
    same(d, d1, ["q.state"])
_, err = run("load nowhere.state\n", {"STUB_STATES": plays}, expect=70)
if "load nowhere: no play in" not in err:
    print("states_check: missing play message: " + err)
    fails += 1

print("states_check: " + ("ok" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
