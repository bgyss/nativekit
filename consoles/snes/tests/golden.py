#!/usr/bin/env python3
"""
golden.py: every output nativeshvc makes, recorded once and compared after
each step of a refactor (docs/superpowers/plans/2026-10-30-nativekit-extraction.md).

  golden.py record OUT [--only NAME,...]   run everything, write OUT/manifest.json
  golden.py compare OUT [--only NAME,...]  run again, compare with OUT/manifest.json
  golden.py time                           the testcart set timed 3 times (the runs only), seconds

What it runs, each into its own folder, hashing every file written there and
the run's stdout, stderr and exit status:
  testcart  each test cartridge (tests/rom/cases.py) headless with its
            script, --sound and --counts
  replay    each test cartridge's reference accesses (the cpu.log run.py
            --keep left in build/tests/rom/NAME/oracle/) on nativeshvc
  scale     the first 40 test cartridges with NSHVC_SCALE=3 (presented shots)
  states    tests/unit/states.py's runs, with fork and NSHVC_SCRIPT_NOFORK=1
  unit      each unit test with each of its arguments (fatal ones included),
            overlay_check at two sizes
  dsp       tests/dsp/keys.py's sound driver on nativeshvc, its SPC700 log
            through key_events and mix_replay
record without --only stops if a test has no reference accesses to replay.
NSHVC_GOLDEN_PLANT=1 flips one byte of the first hashed file (the self-test).
"""
import concurrent.futures, hashlib, json, os, resource, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(HERE, "rom"))
sys.path.insert(0, os.path.join(HERE, "dsp"))
import cases  # noqa: E402
import run as romrun  # noqa: E402
import snestest  # noqa: E402
import keys  # noqa: E402

BUILD = os.environ.get("NSHVC_TESTBUILD", os.path.join(ROOT, "build", "null", "tests"))
LOGS = os.path.join(ROOT, "build", "tests", "rom")
WORK = os.path.join(ROOT, "build", "golden-work")

UNIT = [
    ("input", []), ("waits", []), ("waits", ["--allowance"]), ("waits", ["--offpoll"]),
    ("waits", ["--held"]), ("waits", ["--heldpoll"]), ("ipl", []), ("ipl", ["--driver"]),
    ("ipl", ["--profile"]), ("ipl", ["--cpu-profile"]), ("ipl", ["--sweep"]),
    ("vram_direct", []), ("vram_direct", ["--copy0"]), ("vram_direct", ["--direct0"]),
    ("chip", []), ("chip_irq", []), ("canvas_check", []), ("screen_check", []),
    ("present_check", []), ("resample_check", []),
]


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def run_into(key, cmd, d, env=None):
    """run cmd (or a list of commands, one after another) with d as its
       folder; {file: hash} plus the run's own streams"""
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    e = dict(os.environ)
    e.update(env or {})
    cmds = cmd if isinstance(cmd[0], list) else [cmd]
    rs = [subprocess.run(c, cwd=d, capture_output=True, env=e, timeout=1800) for c in cmds]
    out = b"".join(x.stdout for x in rs)
    err = b"".join(x.stderr for x in rs)
    # present_check prints how long a frame took ("time: ..."), which is the
    # machine's, not the code's: that one line is left out of its hash
    hashed = b"".join(x for x in out.splitlines(True) if not x.startswith(b"time:"))
    got = {"exit": rs[0].returncode if len(rs) == 1 else [x.returncode for x in rs],
           "stdout": hashlib.sha256(hashed).hexdigest(), "stderr": hashlib.sha256(err).hexdigest()}
    for base, _, files in os.walk(d):
        for f in sorted(files):
            p = os.path.join(base, f)
            got["file:" + os.path.relpath(p, d)] = digest(p)
    # the streams, kept beside the folder for finding what differs
    with open(d + ".stdout", "wb") as f:
        f.write(out)
    with open(d + ".stderr", "wb") as f:
        f.write(err)
    return key, got


def missing_logs(only):
    """the tests with no reference accesses to replay (run.py --keep makes them)"""
    return [t.name for t in cases.all_tests() if (not only or t.name in only)
            and not os.path.exists(os.path.join(LOGS, t.name, "oracle", "cpu.log"))]


def jobs(only):
    """build every ROM and script, and list the runs: (key, command, env)"""
    tests = [t for t in cases.all_tests() if not only or t.name in only]
    js, roms = [], {}
    for t in tests:
        src = os.path.join(WORK, "rom", t.name)
        os.makedirs(src, exist_ok=True)
        roms[t.name] = rom = t.build(src)
        script = os.path.join(src, "script.txt")
        romrun.write_script(t, script)
        js.append(("testcart/" + t.name,
                   [os.path.join(BUILD, "testcart"), "--rom", rom, "--headless", "--script", script, "--out", ".",
                    "--sound", "sound.raw", "--counts", "counts.txt"], None))
        log = os.path.join(LOGS, t.name, "oracle", "cpu.log")
        if os.path.exists(log):  # record stops without them
            js.append(("replay/" + t.name,
                       [os.path.join(BUILD, "replay"), "--rom", rom, "--log", log, "--headless", "--script", script,
                        "--out", "."], None))
    for t in tests[:40]:
        src = os.path.join(WORK, "rom", t.name)
        js.append(("scale/" + t.name,
                   [os.path.join(BUILD, "testcart"), "--rom", roms[t.name], "--headless",
                    "--script", os.path.join(src, "script.txt"), "--out", "."], {"NSHVC_SCALE": "3"}))
    if not only:
        states = [sys.executable, os.path.join(HERE, "unit", "states.py"), os.path.dirname(BUILD)]
        js.append(("states/fork", states, None))
        js.append(("states/nofork", states, {"NSHVC_SCRIPT_NOFORK": "1"}))
        # the sound driver of tests/dsp/keys.py on nativeshvc: its SPC700 log
        # and sound, then the mixer given the log (key_events, mix_replay)
        src = os.path.join(WORK, "keys")
        os.makedirs(src, exist_ok=True)
        rom = keys.cartridge(src).build(src)
        script = os.path.join(src, "script.txt")
        with open(script, "w") as f:
            f.write("wait %d\n" % keys.FRAMES)
        cart = [os.path.join(BUILD, "testcart"), "--rom", rom, "--headless", "--script", script, "--out", ".",
                "--sound", "sound.raw"]
        js.append(("dsp/keys", [cart, [os.path.join(BUILD, "key_events"), "nat.spclog"],
                                [os.path.join(BUILD, "mix_replay"), "nat.spclog", "sound.raw"]],
                   {"NSHVC_SPCLOG": "nat.spclog"}))
        # every renderer's overlays against the reference drawing: the null
        # build's, at its own size and at the README's
        js.append(("unit/overlay_check", [os.path.join(BUILD, "overlay_check")], None))
        js.append(("unit/overlay_check_big", [os.path.join(BUILD, "overlay_check")], {"RR_SIZE": "1100x800"}))
        for name, args in UNIT:
            js.append(("unit/" + name + "".join(args), [os.path.join(BUILD, name)] + args, None))
    return js


def run_jobs(js):
    out = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        fs = [ex.submit(run_into, k, c, os.path.join(WORK, "out", k), e) for k, c, e in js]
        for f in concurrent.futures.as_completed(fs):
            k, got = f.result()
            out[k] = got
    if os.environ.get("NSHVC_GOLDEN_PLANT"):
        for k in sorted(out):
            fs = sorted(x for x in out[k] if x.startswith("file:"))
            if fs:
                h = out[k][fs[0]]
                out[k][fs[0]] = ("1" if h[0] == "0" else "0") + h[1:]
                break
    return out


def main():
    a = sys.argv[1:]
    only = set(a[a.index("--only") + 1].split(",")) if "--only" in a else None
    if not snestest.ASAR:
        sys.exit("golden.py: set NSHVC_ASAR")
    if a and a[0] == "record":
        if not only:
            lost = missing_logs(None)
            if lost:
                sys.exit("golden.py: no reference accesses (run tests/rom/run.py --keep) for %d tests: %s"
                         % (len(lost), ", ".join(lost[:10]) + (", ..." if len(lost) > 10 else "")))
        got = run_jobs(jobs(only))
        os.makedirs(a[1], exist_ok=True)
        with open(os.path.join(a[1], "manifest.json"), "w") as f:
            json.dump(got, f, indent=1, sort_keys=True)
        print("golden: recorded %d runs" % len(got))
    elif a and a[0] == "compare":
        with open(os.path.join(a[1], "manifest.json")) as f:
            want = json.load(f)
        if not any(k.startswith("replay/") for k in want):
            sys.exit("golden.py: %s has no replay runs (recorded without the reference accesses?)" % a[1])
        if only:
            want = {k: v for k, v in want.items() if k.split("/", 1)[1] in only}
        got = run_jobs(jobs(only))
        bad = 0
        for k in sorted(set(want) | set(got)):
            w, g = want.get(k), got.get(k)
            if w != g:
                bad += 1
                if w is None or g is None:
                    print("%s differs: %s" % (k, "new run" if w is None else "run missing"))
                    continue
                for x in sorted(set(w) | set(g)):
                    if w.get(x) != g.get(x):
                        print("%s differs: %s" % (k, x))
        print("golden: %d of %d runs differ" % (bad, len(set(want) | set(got))))
        sys.exit(1 if bad else 0)
    elif a and a[0] == "time":
        # the ROMs and scripts first; then only the runs are timed: the wall
        # clock, and the CPU the runs' processes used (less moved by the
        # machine's other work), 3 times
        js = [j for j in jobs(None) if j[0].startswith("testcart/")]
        walls, cpus = [], []
        for _ in range(3):
            c0 = resource.getrusage(resource.RUSAGE_CHILDREN)
            t0 = time.monotonic()
            run_jobs(js)
            walls.append(time.monotonic() - t0)
            c1 = resource.getrusage(resource.RUSAGE_CHILDREN)
            cpus.append(c1.ru_utime + c1.ru_stime - c0.ru_utime - c0.ru_stime)
        print("golden: wall %s s, cpu %s s" % (" ".join("%.1f" % x for x in walls), " ".join("%.1f" % x for x in cpus)))
        print("golden: %.1f s wall, %.1f s cpu (min of 3)" % (min(walls), min(cpus)))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
