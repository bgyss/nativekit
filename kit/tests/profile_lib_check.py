#!/usr/bin/env python3
"""profile_lib_check: the fitter on kinds no console has: three kinds with
   known weights give them back; a kind not measured keeps its default; a
   chip's program with known fixed part and weights gives them back."""
import os, random, sys, tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import nk_profile as p  # noqa: E402

rnd = random.Random(7)
true = [2.5, 0.0, 7.0]
frames = []
for f in range(400):
    n = [rnd.randint(1000, 9000) for _ in true]
    bus = sum(n) * 6
    work = sum(w * c for w, c in zip(true, n))
    frames.append((f, n, work, bus))
w, rows = p.fit_weights(frames, [True, True, True], [0.0, 0.0, 0.0], 1000)
assert all(abs(a - b) < 1e-6 for a, b in zip(w, true)), w
w, rows = p.fit_weights(frames, [True, False, True], [0.0, 3.0, 0.0], 1000)
assert w[1] == 3.0, w

with tempfile.NamedTemporaryFile("w", suffix=".jobs", delete=False) as f:
    f.write("nk-chip-jobs 1 TESTCHIP ops LOAD STORE speed 2\n")
    for j in range(40):
        a, b = rnd.randint(10, 500), rnd.randint(10, 500)
        f.write("job 1234 %d %d %d\n" % (100 + 3 * a + 5 * b, a, b))
    path = f.name
name, ops, speed, default, prog = p.fit_chip(path, "kit-check", 20)
assert name == "TESTCHIP" and ops == ["LOAD", "STORE"], (name, ops)
entry, fixed, weights, how = prog[0][:4]
assert entry == 0x1234 and how == "fit", prog[0]
assert abs(fixed - 100) < 1e-6 and abs(weights[0] - 3) < 1e-6 and abs(weights[1] - 5) < 1e-6, prog[0]
os.unlink(path)
print("profile_lib_check: ok")
