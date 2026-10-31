#!/usr/bin/env python3
"""golden.py must catch a planted change: record, compare, then compare again
   with NSHVC_GOLDEN_PLANT, which flips one hashed byte."""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
G = os.path.join(HERE, "golden.py")
ONLY = "mode0,mode1"

def run(*a, env=None):
    return subprocess.run([sys.executable, G] + list(a), capture_output=True, text=True, env=env)

with tempfile.TemporaryDirectory() as d:
    out = os.path.join(d, "g")
    r = run("record", out, "--only", ONLY)
    assert r.returncode == 0, r.stderr
    r = run("compare", out, "--only", ONLY)
    assert r.returncode == 0, "unchanged run must compare equal: " + r.stdout + r.stderr
    env = dict(os.environ, NSHVC_GOLDEN_PLANT="1")
    r = run("compare", out, "--only", ONLY, env=env)
    assert r.returncode == 1, "a planted change must be caught"
    assert "differs" in r.stdout, r.stdout
print("golden_selftest: ok")
