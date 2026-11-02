# nativekit Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract nativeshvc's console-independent parts into `kit/` (nativekit) and rebuild nativeshvc on it as `consoles/snes/`, with every output byte-for-byte unchanged.

**Architecture:** A monorepo: `kit/` is compiled as CMake OBJECT libraries folded into the existing `nshvc` static library, so games keep linking `nshvc` alone. The kit owns `main`'s loop, the window, pacing, sound output, native input devices, scripts, states, saves and generic presentation; a console hands it one `nk_console` table of callbacks. Every step is held to a golden master recorded before any code moves, plus the bsnes reference comparison at stage ends.

**Tech Stack:** C11, Objective-C (Metal, AppKit, UIKit), CMake ≥ 3.16, Python 3 (tests and tools), asar, a bsnes v115 libretro core.

**Spec:** `docs/superpowers/specs/2026-10-30-nativekit-extraction-design.md`

## Global Constraints

- License: the kit and every moved file stay AGPL-3.0; `LICENSE`, `NOTICE` and every copyright line are kept.
- The SNES game contract (`include/nshvc/machine.h`, `spc.h`, `nshvc.h`, `cart.h`, `hw.h`) is unchanged byte for byte; nativeshvc stays at contract 2.10.
- Every output is byte-for-byte unchanged on every test cartridge and unit test (the golden master), after every task.
- `tests/rom/run.py` against the bsnes v115 reference gives 1,108 match, 42 multiplier/divider reads, 14 known, as `tests/rom/KNOWN`.
- `kit/` names no console: no match, case-insensitive, for `snes|nshvc|super nintendo|super famicom|ppu|spc700|spc_|65816|s-cpu|mode 7|mode7|bsnes` in any file under `kit/`, checked at configure time.
- Consoles depend on the kit; the kit never on a console.
- Environment settings keep their names for the SNES (`NSHVC_STATES`, `NSHVC_SCRIPT_NOFORK`, `NSHVC_SCRIPT_PLAN`, `NSHVC_SCRIPT_FRAMES`, `NSHVC_SCRIPT_SAVED`, `NSHVC_SCRIPT_RESUME`, `NSHVC_RRCHECK`, `NSHVC_RRCHECK_WHY`, `NSHVC_FPS`, `NSHVC_FPS_LOG`, `NSHVC_SCALE`, `NSHVC_COMPOSE`, `RR_SIZE`, `PORT_RESDIR`).
- Headless frame time on the test cartridges no more than 3% slower than the baseline.
- CMake options kept: `NSHVC_HOST` (`null`, `macos`, `windows`, `ios`, `none`), `NSHVC_RENDER` (`soft`, `metal`, `gl`, `vulkan`), `NSHVC_TESTS`, `NSHVC_EXAMPLES`; the library target a game links stays `nshvc`.
- nativekit's version starts at 0.1.0.
- Code style: match the surrounding code (C11, 4-space indent, `/* */` comments in the files' own plain voice, `static` file-local state, no new dependencies in the core).
- Every commit ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT
  ```

## Review Focus

1. **Fatal errors keep their exact text and status.** A game or test that hits a fatal error must still print `nativeshvc: <message>` and exit 70, including errors raised inside moved kit code (coroutines, scripts, saves). Pinned by the golden master's captured stderr and exit codes of the fatal-path unit runs (Task 1) and by `kit/tests/fatal_check.c` (Task 3).
2. **A game that links `nshvc` as before still builds.** An out-of-tree game doing `add_subdirectory(nativeshvc)` and `target_link_libraries(game PRIVATE nshvc)` must still compile and link, with `#include "nshvc/nshvc.h"` resolving. Pinned by `consoles/snes/tests/consumer/` (Task 2), rebuilt in every later task's gate.
3. **States without fork still rebuild the same machine.** `NSHVC_SCRIPT_NOFORK=1` (the Windows path) must make the same dumps as fork after the script moves into the kit. Pinned by the golden master's states runs (Task 1) and `kit/tests/states_check.py` with a stub console (Task 7).
4. **The battery save keeps its file and timing.** A windowed run writes `<save dir>/<save_name>.srm` when the save area changes (every 120 frames and at quit), never in a headless run, via a `.new` file renamed over the old. Pinned by `kit/tests/save_check.c` (Task 6).
5. **iOS still starts the game.** `NSHVC_MAIN` on iOS defines `nshvc_game_main`, which the iOS host must still call after the host moves into the kit. Pinned by the iOS compile check (Task 3) and the `nk_game_main` link in that build.

---

## File map (end state)

```
CMakeLists.txt                         top level: options, kit/, consoles/snes/
kit/CMakeLists.txt                     nk_* OBJECT libraries; the no-console-names check
kit/include/nk/
  nk.h            the console interface (nk_console, nk_region, nk_button, nk_battery, nk_main)
  fatal.h         nk_fatal (a console defines it)
  env.h           nk_env_prefix, nk_getenv, nk_setenv
  coro.h jump.h   moved from src/console
  host.h          moved from src/host
  resample.h      moved from src/dsp
  routines.h      nk_routine_find
  script.h        the script runner
  input.h         native devices, players' sources
  save.h          the battery save
  rr.h rr_size.h  generic presentation (rr_compose*, rr_mode7 leave for the SNES)
  rr_metal_ext.h rr_gl_ext.h rr_vulkan_ext.h rr_soft_ext.h   backend hooks for a console's GPU work
kit/src/coro/     coro.c coro_arm64.S coro_x86_64.S
kit/src/host/     posix/ win32/ null/ macos/ windows/ ios/
kit/src/dsp/      resample.c
kit/src/dispatch/ routines.c
kit/src/front/    env.c script.c input.c save.c app.c
kit/src/render/   soft/rr_soft.c metal/rr_metal.m gl/rr_gl.c gl/gl_ctx.c gl/gl_api.h
                  vulkan/rr_vulkan.c vulkan/rr_spv_kit.h vulkan/shaders/{quad.vert,copy.frag,sharp.frag,over.frag,build.sh}
kit/tools/nk_profile.py
kit/tests/        CMakeLists.txt fatal_check.c coro_check.c routines_check.c env_check.c
                  script_check.c stub_console.c states_check.py save_check.c profile_lib_check.py
kit/docs/         kit.md contract-discipline.md
kit/VERSION       0.1.0
consoles/snes/    everything nativeshvc had, minus what moved, plus:
  src/front/snes_console.c     the nk_console table and its callbacks
  src/front/snes_input.c       the SNES's controllers from the kit's devices (was input.c)
  src/front/snes_present.c     show_picture, show_mode7, mode7_prepare, shots, rrcheck hooks
  src/front/console.c          the public nshvc_* calls, overlays API, nshvc_main
  src/front/ios_main.c         nk_game_main -> nshvc_game_main (iOS only)
  src/render/snes_rr.h         rr_compose, rr_compose_woven, rr_compose_mode7, rr_mode7
  src/render/soft/snes_soft.c  src/render/metal/snes_metal.m  src/render/gl/snes_gl.c
  src/render/vulkan/snes_vulkan.c  src/render/vulkan/rr_spv_snes.h
  tests/golden.py  tests/consumer/
docs/superpowers/ specs and plans
```

---

### Task 0: Tooling and the baseline

**Files:**
- No source changes. Outputs live under `build/` (ignored by git); results go in the task report.

**Interfaces:**
- Produces: `NSHVC_ASAR` (path to asar), `NSHVC_ORACLE_CORE` (path to `build/oracle-core/bsnes_libretro.dylib`), builds `build/null` (headless, soft, tests and examples) and `build/macos` (macos, metal, tests and examples), and the baseline numbers later tasks compare against.

- [ ] **Step 1: Build asar from source**

```bash
mkdir -p build/tools && cd build/tools
git clone --depth 1 --branch v1.91 https://github.com/RPGHacker/asar.git
cmake -S asar/src -B asar/build -DCMAKE_BUILD_TYPE=Release && cmake --build asar/build -j
export NSHVC_ASAR="$PWD/asar/build/asar/bin/asar"
"$NSHVC_ASAR" --version
```
Expected: asar prints its version (1.91). If the tag is missing, use the newest 1.x tag and say which.

- [ ] **Step 2: Build the bsnes v115 libretro core with the two front-end changes**

```bash
cd build/tools
git clone --depth 1 --branch v115 https://github.com/bsnes-emu/bsnes.git
```
Make the two changes `tests/README.md` names, in `bsnes/bsnes/target-libretro/`, and nothing in the emulated console:
1. `program.cpp` `pollInputDevices`: pass the light guns' inputs through (Super Scope and Justifier ids return the libretro lightgun state) instead of returning nothing for them.
2. The libretro target's display gamma: 1.0 instead of 1.5.

Read `tests/oracle/oracle.c` first: it pins the core's options and shows which inputs it sends for each light gun, so the change to `pollInputDevices` is exactly what it expects.
```bash
make -C bsnes/bsnes target=libretro binary=library -j
mkdir -p ../oracle-core && cp bsnes/bsnes/out/bsnes_libretro.dylib ../oracle-core/
export NSHVC_ORACLE_CORE="$(cd ../oracle-core && pwd)/bsnes_libretro.dylib"
```
Expected: `build/oracle-core/bsnes_libretro.dylib` exists. Commit nothing; record the diff of the two changes in the task report (`git -C bsnes diff`).

- [ ] **Step 3: Install the optional toolchains (Homebrew)**

```bash
brew install mingw-w64 glslang vulkan-headers vulkan-loader molten-vk
```
Expected: `x86_64-w64-mingw32-gcc --version`, `glslangValidator --version` work. If the user's machine refuses an install, continue: the backends and hosts they serve are then compile-checked or skipped, and the report says which.

- [ ] **Step 4: Build every configuration that builds today**

```bash
cmake -S . -B build/null -DNSHVC_TESTS=ON -DNSHVC_EXAMPLES=ON && cmake --build build/null -j
cmake -S . -B build/macos -DNSHVC_HOST=macos -DNSHVC_RENDER=metal -DNSHVC_TESTS=ON -DNSHVC_EXAMPLES=ON && cmake --build build/macos -j
cmake -S . -B build/null-metal -DNSHVC_RENDER=metal -DNSHVC_TESTS=ON && cmake --build build/null-metal -j
cmake -S . -B build/null-gl -DNSHVC_RENDER=gl -DNSHVC_TESTS=ON && cmake --build build/null-gl -j
cmake -S . -B build/null-vulkan -DNSHVC_RENDER=vulkan -DNSHVC_TESTS=ON && cmake --build build/null-vulkan -j
cmake -S . -B build/x86 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DNSHVC_TESTS=ON && cmake --build build/x86 -j
cmake -S . -B build/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DNSHVC_HOST=ios -DNSHVC_EXAMPLES=ON && cmake --build build/ios -- -quiet CODE_SIGNING_ALLOWED=NO
cmake -S . -B build/win -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DNSHVC_HOST=windows -DNSHVC_RENDER=gl -DNSHVC_EXAMPLES=ON && cmake --build build/win -j
```
Expected: each builds with no warnings. Record, for each, "builds" or the exact failure. A configuration that fails before any change is out of the gate (say so); none of the later tasks may make a building one fail.

- [ ] **Step 5: Run the reference comparison**

```bash
python3 tests/rom/run.py --keep | tee build/baseline-run.txt
tail -1 build/baseline-run.txt
```
Expected last line: `0 of 1164 differ; 56 more differ as KNOWN lists; 0 listed in KNOWN now pass` (1,108 + 42 + 14). If it differs, STOP: report the output and do not start Task 1.

- [ ] **Step 6: Run every other check in `tests/README.md`**

```bash
python3 tests/rom/ext_check.py
python3 tests/dsp/fuzz.py 1 2 3 4
python3 tests/dsp/keys.py
build/null/tests/resample_check
python3 tests/tools/profile_check.py
build/null/tests/input
python3 tests/unit/states.py
build/macos/tests/pointer_macos
build/null/tests/vram_direct; build/null/tests/vram_direct --copy0; echo "exit $?"; build/null/tests/vram_direct --direct0; echo "exit $?"
build/null/tests/waits; build/null/tests/waits --allowance; build/null/tests/waits --offpoll; echo "exit $?"; build/null/tests/waits --held; echo "exit $?"; build/null/tests/waits --heldpoll; echo "exit $?"
build/null/tests/ipl; build/null/tests/ipl --driver; build/null/tests/ipl --profile --sweep; build/null/tests/ipl --cpu-profile
build/null/tests/chip; build/null/tests/chip_irq
build/null/tests/canvas_check; build/null/tests/screen_check; build/null/tests/present_check
for r in metal gl vulkan; do [ -x build/null-$r/tests/overlay_check ] && RR_SIZE=1100x800 build/null-$r/tests/overlay_check; done
```
Expected: each passes as `tests/README.md` describes (the `--copy0`, `--direct0`, `--offpoll`, `--held`, `--heldpoll` runs exit 70). Record each result; the pointer check needs a window and the user's screen.

- [ ] **Step 7: Record the timing baseline**

```bash
for i in 1 2 3; do /usr/bin/time -p python3 tests/rom/ext_check.py 2>&1 | grep real; done | tee build/baseline-time.txt
```
Expected: three `real` times; the smallest is the baseline (Task 1's golden tool re-measures with its own runs, which is the number later tasks use).

---

### Task 1: The golden master tool

**Files:**
- Modify: `tests/rom/run.py` (factor the script writer out of `run_one` into `write_script(t, script)`, no change in what it writes)
- Create: `tests/golden.py`
- Test: `tests/golden_selftest.py`

**Interfaces:**
- Consumes: `build/null/tests/{testcart,replay,input,states,waits,ipl,vram_direct,chip,chip_irq,canvas_check,screen_check,present_check,resample_check}`, the cpu logs kept by Task 0 Step 5 under `build/tests/rom/<name>/oracle/cpu.log`, `NSHVC_ASAR`.
- Produces: `python3 tests/golden.py record OUT` and `python3 tests/golden.py compare OUT` (exit 0 when every file's hash, stdout, stderr and exit status match; else exit 1 naming each difference), `python3 tests/golden.py time` (prints the min of 3 headless runs of the testcart set, seconds). Later tasks run `python3 tests/golden.py compare build/golden` (after Task 2 moves it, `consoles/snes/tests/golden.py`).

- [ ] **Step 1: Factor `write_script` out of `run.py`**

In `tests/rom/run.py`, move the block in `run_one` from `script = os.path.join(d, "script.txt")` through `f.write("dump end\n")` into:

```python
def write_script(t, script):
    """the controllers' steps, shots and the final dump, the same script on
       the reference and on nativeshvc"""
    with open(script, "w") as f:
        if t.inputs:
            # the controllers' steps before each frame, the same script on both
            for n in range(1, t.frames + 1):
                lines = t.inputs.get(n, ())
                for line in lines:
                    f.write(line + "\n")
                if not any(line.startswith("hold") for line in lines):
                    f.write("wait 1\n")
                if n in t.shots:
                    f.write("shot frame_%d\n" % n)
        else:
            at = 0
            for s in t.shots:
                f.write("wait %d\nshot frame_%d\n" % (s - at, s))
                at = s
            if t.frames > at:
                f.write("wait %d\n" % (t.frames - at))
        f.write("dump end\n")
```
and in `run_one`: `script = os.path.join(d, "script.txt")` then `write_script(t, script)`.

Run: `python3 tests/rom/run.py --only $(python3 -c "import sys; sys.path.insert(0,'tests/rom'); import cases; print(','.join(t.name for t in cases.all_tests()[:20]))")`
Expected: the same 20 lines as the first 20 of `build/baseline-run.txt`.

- [ ] **Step 2: Write the failing self-test**

`tests/golden_selftest.py`:
```python
#!/usr/bin/env python3
"""golden.py must catch a planted change: record, change one output byte in a
   copy of the testcart binary's run (via NSHVC_GOLDEN_PLANT), compare."""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
G = os.path.join(HERE, "golden.py")

def run(*a, env=None):
    return subprocess.run([sys.executable, G] + list(a), capture_output=True, text=True, env=env)

with tempfile.TemporaryDirectory() as d:
    out = os.path.join(d, "g")
    r = run("record", out, "--only", "cpu_mul,ppu_mode0_random")
    assert r.returncode == 0, r.stderr
    r = run("compare", out, "--only", "cpu_mul,ppu_mode0_random")
    assert r.returncode == 0, "unchanged run must compare equal: " + r.stdout + r.stderr
    env = dict(os.environ, NSHVC_GOLDEN_PLANT="1")
    r = run("compare", out, "--only", "cpu_mul,ppu_mode0_random", env=env)
    assert r.returncode == 1, "a planted change must be caught"
    assert "differs" in r.stdout, r.stdout
print("golden_selftest: ok")
```
Replace the two test names with two real names from `cases.all_tests()` (the first test and the first `ppu_` test): `python3 -c "import sys; sys.path.insert(0,'tests/rom'); import cases; print([t.name for t in cases.all_tests()][:5])"`.

Run: `python3 tests/golden_selftest.py`
Expected: FAIL (golden.py does not exist).

- [ ] **Step 3: Write `tests/golden.py`**

```python
#!/usr/bin/env python3
"""
golden.py: every output nativeshvc makes, recorded once and compared after
each step of a refactor (docs/superpowers/plans/2026-10-30-nativekit-extraction.md).

  golden.py record OUT [--only NAME,...]   run everything, write OUT/manifest.json
  golden.py compare OUT [--only NAME,...]  run again, compare with OUT/manifest.json
  golden.py time                           min of 3 runs of the testcart set, seconds

What it runs, each into its own folder, hashing every file written there and
the run's stdout, stderr and exit status:
  testcart  each test cartridge (tests/rom/cases.py) headless with its
            script, --sound and --counts
  replay    each test cartridge's reference accesses (the cpu.log run.py
            --keep left in build/tests/rom/NAME/oracle/) on nativeshvc
  scale     the first 40 test cartridges with NSHVC_SCALE=3 (presented shots)
  states    tests/unit/states.py's runs, with fork and NSHVC_SCRIPT_NOFORK=1
  unit      each unit test with each of its arguments (fatal ones included)
NSHVC_GOLDEN_PLANT=1 flips one byte of the first hashed file (the self-test).
"""
import concurrent.futures, hashlib, json, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "rom"))
import cases  # noqa: E402
import run as romrun  # noqa: E402
import snestest  # noqa: E402

BUILD = os.environ.get("NSHVC_TESTBUILD", os.path.join(ROOT, "build", "null", "tests"))
LOGS = os.path.join(ROOT, "build", "tests", "rom")
WORK = os.path.join(ROOT, "build", "golden-work")

UNIT = [
    ("input", []), ("states", []), ("waits", []), ("waits", ["--allowance"]), ("waits", ["--offpoll"]),
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
    """run cmd with d as its folder; {file: hash} plus the run's own streams"""
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run(cmd, cwd=d, capture_output=True, env=e, timeout=1800)
    got = {"exit": r.returncode, "stdout": hashlib.sha256(r.stdout).hexdigest(),
           "stderr": hashlib.sha256(r.stderr).hexdigest()}
    for base, _, files in os.walk(d):
        for f in sorted(files):
            p = os.path.join(base, f)
            got["file:" + os.path.relpath(p, d)] = digest(p)
    with open(os.path.join(d, "..", key.replace("/", "_") + ".stderr"), "wb") as f:
        f.write(r.stderr)
    return key, got


def jobs(only):
    tests = [t for t in cases.all_tests() if not only or t.name in only]
    js = []
    for t in tests:
        src = os.path.join(WORK, "rom", t.name)
        os.makedirs(src, exist_ok=True)
        rom = t.build(src)
        script = os.path.join(src, "script.txt")
        romrun.write_script(t, script)
        js.append(("testcart/" + t.name,
                   [os.path.join(BUILD, "testcart"), "--rom", rom, "--headless", "--script", script, "--out", ".",
                    "--sound", "sound.raw", "--counts", "counts.txt"], None))
        log = os.path.join(LOGS, t.name, "oracle", "cpu.log")
        if os.path.exists(log):
            js.append(("replay/" + t.name,
                       [os.path.join(BUILD, "replay"), "--rom", rom, "--log", log, "--headless", "--script", script,
                        "--out", "."], None))
    for t in tests[:40]:
        src = os.path.join(WORK, "rom", t.name)
        js.append(("scale/" + t.name,
                   [os.path.join(BUILD, "testcart"), "--rom", os.path.join(src, t.name + ".sfc"), "--headless",
                    "--script", os.path.join(src, "script.txt"), "--out", "."], {"NSHVC_SCALE": "3"}))
    if not only:
        js.append(("states/fork", [sys.executable, os.path.join(HERE, "unit", "states.py")], None))
        js.append(("states/nofork", [sys.executable, os.path.join(HERE, "unit", "states.py")],
                   {"NSHVC_SCRIPT_NOFORK": "1"}))
        for name, args in UNIT:
            js.append(("unit/" + name + "".join(args), [os.path.join(BUILD, name)] + args, None))
    return js


def run_all(only):
    out = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        fs = [ex.submit(run_into, k, c, os.path.join(WORK, "out", k), e) for k, c, e in jobs(only)]
        for f in concurrent.futures.as_completed(fs):
            k, got = f.result()
            out[k] = got
    if os.environ.get("NSHVC_GOLDEN_PLANT") and out:
        k = sorted(out)[0]
        f = sorted(x for x in out[k] if x.startswith("file:"))[0]
        out[k][f] = "0" + out[k][f][1:]
    return out


def main():
    a = sys.argv[1:]
    only = set(a[a.index("--only") + 1].split(",")) if "--only" in a else None
    if not snestest.ASAR:
        sys.exit("golden.py: set NSHVC_ASAR")
    if a and a[0] == "record":
        got = run_all(only)
        os.makedirs(a[1], exist_ok=True)
        with open(os.path.join(a[1], "manifest.json"), "w") as f:
            json.dump(got, f, indent=1, sort_keys=True)
        print("golden: recorded %d runs" % len(got))
    elif a and a[0] == "compare":
        with open(os.path.join(a[1], "manifest.json")) as f:
            want = json.load(f)
        if only:
            want = {k: v for k, v in want.items() if k.split("/", 1)[1] in only}
        got = run_all(only)
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
        best = None
        for _ in range(3):
            t0 = time.monotonic()
            run_all(None)
            dt = time.monotonic() - t0
            best = dt if best is None else min(best, dt)
        print("golden: %.1f s" % best)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
```
Before relying on it, check two assumptions against the code and fix the tool, not the code, if either is wrong:
1. The ROM a test builds is `<dir>/<name>.sfc` (read `snestest.py`'s `build`; use the path `t.build` returns in the `scale` jobs, kept in a dict from the first loop, if the name differs).
2. `tests/unit/states.py` runs from any working folder and writes only under its own temporary folders (read it; if it writes under `build/`, pass it an output folder argument it already takes, or hash its stdout only).

- [ ] **Step 4: Run the self-test**

Run: `python3 tests/golden_selftest.py`
Expected: `golden_selftest: ok`

- [ ] **Step 5: Record the golden master and the time**

```bash
python3 tests/golden.py record build/golden
python3 tests/golden.py compare build/golden
python3 tests/golden.py time | tee build/golden-time.txt
```
Expected: `golden: recorded N runs` (N about 2 × 1,164 + 40 + 2 + 21); the compare prints `golden: 0 of N runs differ`. If the compare of an unchanged tree differs, a run is not deterministic: find which (the printed key and file), and make the tool hash that output in a deterministic form or leave that one output out with a comment saying why, then record again. Never leave a whole run out.

- [ ] **Step 6: Commit**

```bash
git add tests/rom/run.py tests/golden.py tests/golden_selftest.py
git commit -m "Golden master: every output nativeshvc makes, recorded and compared

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 2: Restructure into `consoles/snes/`

**Files:**
- Move: `include/ src/ tests/ examples/ tools/ docs/{architecture.svg,contract.md,cost-profile.md,nativeshvc.md,snes-decomp-learnings.md}` and the current `CMakeLists.txt` into `consoles/snes/` (`git mv`). `README.md` is copied to `consoles/snes/README.md` (kept at the root too); `LICENSE`, `NOTICE`, `docs/superpowers/` stay at the root.
- Create: `CMakeLists.txt` (top level), `consoles/snes/tests/consumer/CMakeLists.txt`, `consoles/snes/tests/consumer/game.c`
- Modify: `consoles/snes/CMakeLists.txt` (no `project()` / `cmake_minimum_required` when included from the top), path constants in `consoles/snes/tests/golden.py`, `consoles/snes/tests/rom/run.py`, `consoles/snes/tests/rom/ext_check.py`, `consoles/snes/tools/nshvc-profile` that compute `ROOT` from their own location.

**Interfaces:**
- Produces: the build folders keep their names (`build/null`, ...) at the repository root; test paths become `consoles/snes/tests/...`; `ROOT` in the Python tools still means the repository root (so `build/...` paths keep working). The `nshvc` target is defined by `consoles/snes/CMakeLists.txt`.

- [ ] **Step 1: Write the failing consumer test**

`consoles/snes/tests/consumer/CMakeLists.txt`:
```cmake
# A game as games use nativeshvc: its own project, nativeshvc added as a
# subdirectory, the nshvc target linked.
cmake_minimum_required(VERSION 3.16)
project(consumer LANGUAGES C)
add_subdirectory(${NATIVESHVC} nativeshvc)
add_executable(consumer game.c)
target_link_libraries(consumer PRIVATE nshvc)
```
`consoles/snes/tests/consumer/game.c`:
```c
/* consumer: the smallest game that links nshvc from outside the tree */
#include "nshvc/nshvc.h"
#include "nshvc/machine.h"

static void abi_reset(void)
{
    for (;;)
        cpu_wai(0x008000);
}

static uint8_t rom[0x8000];
static const nshvc_routine routines[] = { { 0x008000, abi_reset, "Reset", 0 } };

NSHVC_MAIN(argc, argv)
{
    static nshvc_cart cart;
    rom[0x7FFC] = 0x00;
    rom[0x7FFD] = 0x80;
    cart.title = "Consumer";
    cart.map = NSHVC_LOROM;
    cart.rom = rom;
    cart.rom_size = sizeof rom;
    cart.routines = routines;
    cart.routine_count = 1;
    return nshvc_main(&cart, argc, argv);
}
```
Run (before the move, against the root): `cmake -S consoles/snes/tests/consumer -B build/consumer -DNATIVESHVC=$PWD && cmake --build build/consumer && build/consumer/consumer --headless --frames 5; echo "exit $?"`
Expected: builds and prints `exit 0` against today's root (it is a guard, not a red test: it must keep passing after the move, with `-DNATIVESHVC=$PWD` still the repository root).

- [ ] **Step 2: Move the tree**

```bash
mkdir -p consoles/snes/docs
git mv include src tests examples tools consoles/snes/
git mv docs/architecture.svg docs/contract.md docs/cost-profile.md docs/nativeshvc.md docs/snes-decomp-learnings.md consoles/snes/docs/
git mv CMakeLists.txt consoles/snes/CMakeLists.txt
cp README.md consoles/snes/README.md && git add consoles/snes/README.md
```

- [ ] **Step 3: The top-level CMake**

`CMakeLists.txt`:
```cmake
# The monorepo: nativekit (kit/), the console-independent parts, and each
# native console on it (consoles/). The SNES (consoles/snes) is nativeshvc:
#
#   cmake -B build -DNSHVC_HOST=macos -DNSHVC_RENDER=metal
#   cmake --build build
#
# builds the static library nshvc a game links with its C. Options as
# consoles/snes/CMakeLists.txt describes them.
cmake_minimum_required(VERSION 3.16)
project(nativeshvc VERSION 2.1.0 LANGUAGES C)
add_subdirectory(consoles/snes)
```
In `consoles/snes/CMakeLists.txt`, replace the `cmake_minimum_required(...)` and `project(nativeshvc ...)` lines with:
```cmake
if(NOT PROJECT_NAME)
  cmake_minimum_required(VERSION 3.16)
  project(nativeshvc VERSION 2.1.0 LANGUAGES C)
endif()
```
and replace every `${PROJECT_SOURCE_DIR}` and `${CMAKE_CURRENT_SOURCE_DIR}` that means nativeshvc's own folder (in it and in `consoles/snes/tests/CMakeLists.txt`, `consoles/snes/examples/CMakeLists.txt`) with `${NSHVC_DIR}`, set at the top of `consoles/snes/CMakeLists.txt`:
```cmake
set(NSHVC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
```
The consumer's `add_subdirectory(${NATIVESHVC} nativeshvc)` adds the root; the root's `project()` is the one that runs.

- [ ] **Step 4: Fix the Python tools' roots**

Each of these computes the repository root from its own path; it is now two levels deeper. For each, change only the `ROOT =` line:
- `consoles/snes/tests/golden.py`: `ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))`
- `consoles/snes/tests/rom/run.py`: `ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE))))`
- `consoles/snes/tools/nshvc-profile`: `ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))`
- every other `.py` under `consoles/snes/tests/` with a `ROOT =` line (find them: `grep -rn "^ROOT\|ROOT =" consoles/snes/tests consoles/snes/tools`), the same: one `os.path.dirname` more per level. Where a tool also joins `ROOT` with `tests/` or `tools/`, make it `consoles/snes/tests/`.
Also grep the docs and READMEs for `tests/`, `tools/`, `docs/` command lines (`grep -rn "python3 tests\|build/null/tests\|tools/nshvc-profile" consoles/snes/*.md consoles/snes/docs consoles/snes/tests/README.md`): in `consoles/snes/` docs, commands run from the repository root become `python3 consoles/snes/tests/...`. The root `README.md` keeps its text for now (Task 14 rewrites its top).

- [ ] **Step 5: Build and gate**

```bash
rm -rf build/null build/macos && cmake -S . -B build/null -DNSHVC_TESTS=ON -DNSHVC_EXAMPLES=ON && cmake --build build/null -j
cmake -S . -B build/macos -DNSHVC_HOST=macos -DNSHVC_RENDER=metal -DNSHVC_TESTS=ON -DNSHVC_EXAMPLES=ON && cmake --build build/macos -j
rm -rf build/consumer && cmake -S consoles/snes/tests/consumer -B build/consumer -DNATIVESHVC=$PWD && cmake --build build/consumer && build/consumer/consumer --headless --frames 5; echo "exit $?"
python3 consoles/snes/tests/golden.py compare build/golden
```
Expected: builds with no warnings; consumer `exit 0`; `golden: 0 of N runs differ`. Rebuild the other Task 0 configurations that built; each must still build.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Monorepo: nativeshvc as consoles/snes, a top-level CMake over it

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

**The gate every later task runs** (called "the gate" below):
```bash
cmake --build build/null -j && cmake --build build/macos -j
cmake --build build/consumer && build/consumer/consumer --headless --frames 5
python3 consoles/snes/tests/golden.py compare build/golden
```
plus a rebuild of every other configuration that built at Task 0 (each must still build with no warnings), plus the task's own checks. All of it must pass before the task's commit.

---

### Task 3: The kit skeleton, `nk_fatal`, and the stage-1 moves

**Files:**
- Create: `kit/CMakeLists.txt`, `kit/VERSION`, `kit/include/nk/fatal.h`, `kit/tests/CMakeLists.txt`, `kit/tests/fatal_check.c`, `kit/tests/coro_check.c`, `kit/tests/kit_names.cmake`, `consoles/snes/src/front/ios_main.c`
- Move: `consoles/snes/src/console/{coro.c,coro_arm64.S,coro_x86_64.S}` → `kit/src/coro/`; `consoles/snes/src/console/{coro.h,jump.h}` → `kit/include/nk/`; `consoles/snes/src/host/host.h` → `kit/include/nk/host.h`; `consoles/snes/src/host/{posix,win32,null,macos,windows,ios}` → `kit/src/host/`; `consoles/snes/src/dsp/resample.c` → `kit/src/dsp/`; `consoles/snes/src/dsp/resample.h` → `kit/include/nk/`
- Modify: `kit/src/coro/coro.c` (`snes_fatal` → `nk_fatal`, include), `kit/include/nk/coro.h` and `jump.h` (comments only: no console names), `kit/src/host/ios/host_ios.m` (`nshvc_game_main` → `nk_game_main`, comments), `consoles/snes/src/console/bus.c` (`snes_fatal` calls `nk_fatal`'s body: see Step 4), `CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `void nk_fatal(const char *fmt, ...)` declared in `kit/include/nk/fatal.h`; **defined by each console** (the SNES: in `bus.c`, printing `nativeshvc: ` then the message, exit 70). Kit tests define their own.
  - `int nk_game_main(int argc, char **argv)`: on iOS, the kit's host calls it; the SNES defines it in `ios_main.c` as `return nshvc_game_main(argc, argv);`.
  - CMake: `nk_coro`, `nk_resample`, `nk_host` OBJECT libraries; the variable `NK_INCLUDE` (`kit/include` and `kit/include/nk`) consoles add to their include paths; the kit owns the `NSHVC_HOST` / `NSHVC_RENDER` choice of host sources (Task 10 on adds renderers).

- [ ] **Step 1: Write the failing kit tests**

`kit/tests/fatal_check.c`:
```c
/* fatal_check: a console's nk_fatal is what the kit's own errors reach:
   a coroutine that returns is fatal, with the console's message and its
   status. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/coro.h"
#include "nk/fatal.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "stub: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static void returns(void *arg) { (void)arg; }

int main(void)
{
    Coro *self = coro_self(), *c = coro_new(returns, NULL, 64 * 1024);
    coro_switch(self, c);
    fprintf(stderr, "fatal_check: the coroutine's return was not fatal\n");
    return 1;
}
```
`kit/tests/coro_check.c`:
```c
/* coro_check: switching back and forth, each coroutine on its own stack,
   deep recursion on one. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/coro.h"
#include "nk/fatal.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}

static Coro *self, *a, *b;
static int trace[16], nt;

static int deep(int n) { return n == 0 ? 0 : 1 + deep(n - 1); }

static void fa(void *arg)
{
    (void)arg;
    for (;;) {
        trace[nt++] = 1;
        coro_switch(a, b);
    }
}

static void fb(void *arg)
{
    (void)arg;
    for (;;) {
        char local[256];
        local[0] = (char)deep(10000);
        trace[nt++] = 2 + (local[0] != (char)10000);
        coro_switch(b, self);
    }
}

int main(void)
{
    int i;
    self = coro_self();
    a = coro_new(fa, NULL, 1 << 20);
    b = coro_new(fb, NULL, 1 << 20);
    for (i = 0; i < 4; i++) {
        coro_switch(self, a);
        if (coro_current() != self) {
            fprintf(stderr, "coro_check: not back on the program's stack\n");
            return 1;
        }
    }
    for (i = 0; i < 8; i++)
        if (trace[i] != (i % 2 ? 2 : 1)) {
            fprintf(stderr, "coro_check: trace[%d] = %d\n", i, trace[i]);
            return 1;
        }
    printf("coro_check: ok\n");
    return 0;
}
```
`kit/tests/CMakeLists.txt`:
```cmake
# the kit's own checks, each against a stub console where it needs one
add_executable(kit_fatal_check fatal_check.c $<TARGET_OBJECTS:nk_coro>)
add_executable(kit_coro_check coro_check.c $<TARGET_OBJECTS:nk_coro>)
foreach(t kit_fatal_check kit_coro_check)
  target_include_directories(${t} PRIVATE ${NK_INCLUDE})
  set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
endforeach()
```
Run: `cmake --build build/null -j`
Expected: FAIL (no `kit/`, no `nk/coro.h`).

- [ ] **Step 2: Move the files**

```bash
mkdir -p kit/include/nk kit/src/coro kit/src/host kit/src/dsp kit/tests
git mv consoles/snes/src/console/coro.c consoles/snes/src/console/coro_arm64.S consoles/snes/src/console/coro_x86_64.S kit/src/coro/
git mv consoles/snes/src/console/coro.h consoles/snes/src/console/jump.h kit/include/nk/
git mv consoles/snes/src/host/host.h kit/include/nk/host.h
for h in posix win32 null macos windows ios; do git mv consoles/snes/src/host/$h kit/src/host/$h; done
git mv consoles/snes/src/dsp/resample.c kit/src/dsp/ && git mv consoles/snes/src/dsp/resample.h kit/include/nk/
echo 0.1.0 > kit/VERSION
```
`consoles/snes/src/host/` keeps the Windows resource files only if they were not under `windows/` (check `ls consoles/snes/src/host`; it should now be empty: remove the folder).

- [ ] **Step 3: `nk_fatal` and the edits that free the moved files of console names**

`kit/include/nk/fatal.h`:
```c
/*
 * fatal.h: the kit's fatal error. The console defines nk_fatal: its own
 * name before the message, and its exit status (70 for every console in
 * this repository: the contract's "a fatal error, never a guess").
 */
#ifndef NK_FATAL_H
#define NK_FATAL_H

#ifdef __GNUC__
__attribute__((format(printf, 1, 2), noreturn))
#endif
void nk_fatal(const char *fmt, ...);

#endif
```
In `kit/src/coro/coro.c`: replace `#include "snes.h"` with `#include "fatal.h"` and each `snes_fatal(` with `nk_fatal(`. Rewrite the comments in `coro.h` and `jump.h` that name the S-CPU, the SPC700, `spc_main` or a cartridge chip so they say the same thing without the console: for example coro.h's first paragraph becomes "coroutines, for processors that run beside the main one: each on a stack of its own, handing control back and forth with the program's own stack, one running at a time: a deterministic schedule, no threads." and jump.h's "(seen in spc_main on x86-64 macOS)" becomes "(seen in a coroutine's main loop on x86-64 macOS)". Keep every technical statement; change no code in them.

In `kit/src/host/ios/host_ios.m`: rename the declaration and the call `nshvc_game_main` → `nk_game_main`; reword the three comments (lines 6-7, 42, 177) to "the game's main (the console's main macro: on iOS the host keeps the process's main and runs the game's, nk_game_main, on a thread of its own)" and "What a touch means is the game's (the console's touch hook)".

In `kit/include/nk/resample.h` and `kit/src/dsp/resample.c`: no console names (checked in Step 5); if any comment says DSP's rate as 32,040, keep it only if it is phrased as an example; the code is unchanged.

`consoles/snes/src/front/ios_main.c`:
```c
/*
 * ios_main.c: the kit's iOS host runs the game's main as nk_game_main; a
 * game writes NSHVC_MAIN, which on iOS defines nshvc_game_main.
 */
int nshvc_game_main(int argc, char **argv);
int nk_game_main(int argc, char **argv);

int nk_game_main(int argc, char **argv) { return nshvc_game_main(argc, argv); }
```

- [ ] **Step 4: The SNES defines `nk_fatal`**

In `consoles/snes/src/console/bus.c`, keep `snes_fatal` exactly and add, after it:
```c
/* the kit's fatal errors (kit/include/nk/fatal.h): the console's own */
void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "nativeshvc: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}
```
(The kit's coroutines used to call `snes_fatal`, whose text is the same, so their messages are unchanged.)

- [ ] **Step 5: The kit's CMake and the no-console-names check**

`kit/tests/kit_names.cmake`:
```cmake
# Fails the configure when a file under kit/ names a console: the kit is
# console independent (docs/superpowers/specs/2026-10-30-nativekit-extraction-design.md).
file(GLOB_RECURSE _kit_files ${NK_DIR}/*)
set(_bad "")
foreach(f ${_kit_files})
  if(f MATCHES "/tests/kit_names\\.cmake$")
    continue()
  endif()
  file(READ ${f} _text)
  string(TOLOWER "${_text}" _low)
  foreach(word snes nshvc "super nintendo" "super famicom" ppu spc700 spc_ 65816 s-cpu "mode 7" mode7 bsnes)
    string(FIND "${_low}" "${word}" _at)
    if(NOT _at EQUAL -1)
      list(APPEND _bad "${f}: ${word}")
    endif()
  endforeach()
endforeach()
if(_bad)
  string(REPLACE ";" "\n  " _list "${_bad}")
  message(FATAL_ERROR "nativekit names a console:\n  ${_list}")
endif()
```
`kit/CMakeLists.txt`:
```cmake
# nativekit: the console-independent parts of a native console (kit/docs/kit.md).
# OBJECT libraries a console folds into its own library, so a game links the
# console's library alone. NSHVC_HOST and NSHVC_RENDER are read from the
# top-level CMake (their names stay nativeshvc's for its games).
set(NK_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(READ ${NK_DIR}/VERSION NK_VERSION)
string(STRIP "${NK_VERSION}" NK_VERSION)
include(${NK_DIR}/tests/kit_names.cmake)
set(NK_INCLUDE ${NK_DIR}/include ${NK_DIR}/include/nk PARENT_SCOPE)
set(NK_INCLUDE ${NK_DIR}/include ${NK_DIR}/include/nk)

if(NOT MSVC)
  set(NK_C_FLAGS -Wall -fno-strict-aliasing -ffp-contract=off)
endif()

# the coroutines and their switch (the architecture the build targets)
add_library(nk_coro OBJECT ${NK_DIR}/src/coro/coro.c)
if(NOT WIN32)
  set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
  if(CMAKE_OSX_ARCHITECTURES)
    set(_arch "${CMAKE_OSX_ARCHITECTURES}")
  endif()
  if(_arch MATCHES "arm64|aarch64|ARM64")
    target_sources(nk_coro PRIVATE ${NK_DIR}/src/coro/coro_arm64.S)
  elseif(_arch MATCHES "x86_64|AMD64|amd64")
    target_sources(nk_coro PRIVATE ${NK_DIR}/src/coro/coro_x86_64.S)
  else()
    message(FATAL_ERROR "nativekit: no coroutine switch for ${_arch} (one architecture a build)")
  endif()
endif()

add_library(nk_resample OBJECT ${NK_DIR}/src/dsp/resample.c)

# time, files and the host
add_library(nk_host OBJECT)
if(WIN32)
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/win32/host_win32.c)
elseif(NOT NSHVC_HOST STREQUAL "none")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/posix/host_posix.c)
endif()
if(NSHVC_HOST STREQUAL "null")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/null/host_null.c)
elseif(NSHVC_HOST STREQUAL "macos")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/macos/host_macos.m)
  set_source_files_properties(${NK_DIR}/src/host/macos/host_macos.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
elseif(NSHVC_HOST STREQUAL "ios")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/ios/host_ios.m)
  set_source_files_properties(${NK_DIR}/src/host/ios/host_ios.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
elseif(NSHVC_HOST STREQUAL "windows")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/windows/host_windows.c)
elseif(NOT NSHVC_HOST STREQUAL "none")
  message(FATAL_ERROR "nativekit: no host ${NSHVC_HOST}")
endif()

foreach(t nk_coro nk_resample nk_host)
  target_include_directories(${t} PRIVATE ${NK_INCLUDE})
  if(NK_C_FLAGS)
    target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:C>:${NK_C_FLAGS}>)
  endif()
endforeach()

if(NSHVC_TESTS)
  add_subdirectory(tests)
endif()
```
Copy the compile options exactly as `consoles/snes/CMakeLists.txt` applies them today (`-Wall -fno-strict-aliasing -ffp-contract=off` on every source, Objective-C included); if the current file applies them to all languages, apply `NK_C_FLAGS` to all languages here too, so the moved files compile with the same flags as before.

In the root `CMakeLists.txt`, before `add_subdirectory(consoles/snes)`: move the option declarations (`NSHVC_HOST`, the render default, `NSHVC_RENDER`, `NSHVC_TESTS`, `NSHVC_EXAMPLES`, `CMAKE_C_STANDARD 11`, `CMAKE_C_EXTENSIONS ON`, `enable_language(OBJC)` on Apple and `enable_language(ASM)` when not MSVC) from `consoles/snes/CMakeLists.txt` up here, then `add_subdirectory(kit)`.

In `consoles/snes/CMakeLists.txt`: remove the coroutine-switch block, the host blocks' `target_sources` lines and `src/console/coro.c`, `src/dsp/resample.c` from `add_library(nshvc ...)`; keep the host blocks' `target_link_libraries` and `target_compile_definitions` lines (the frameworks and `NSHVC_HOST_IOS=1`) under the same `if`s; add
```cmake
target_sources(nshvc PRIVATE $<TARGET_OBJECTS:nk_coro> $<TARGET_OBJECTS:nk_resample> $<TARGET_OBJECTS:nk_host>)
if(NSHVC_HOST STREQUAL "ios")
  target_sources(nshvc PRIVATE src/front/ios_main.c)
endif()
```
and add `${NK_INCLUDE}` to `target_include_directories(nshvc PRIVATE ...)`; remove `src/host` from that list. In `consoles/snes/tests/CMakeLists.txt`, replace each `${NSHVC_DIR}/src/host` include with `${NK_INCLUDE}` and each `${NSHVC_DIR}/src/dsp` used for `resample.h` likewise (keep `src/dsp` for `mixer.h`); `resample_check` adds `$<TARGET_OBJECTS:nk_resample>` if it compiled `resample.c` directly.

- [ ] **Step 6: Run the kit tests**

```bash
cmake -S . -B build/null -DNSHVC_TESTS=ON -DNSHVC_EXAMPLES=ON && cmake --build build/null -j
build/null/kit/tests/kit_coro_check
build/null/kit/tests/kit_fatal_check; echo "exit $?"
```
Expected: `coro_check: ok`; the fatal check prints `stub: a coroutine returned` and `exit 70`.

- [ ] **Step 7: Check that the name check fails when it must**

```bash
echo "/* snes */" > kit/include/nk/planted.h && cmake -S . -B build/null 2>&1 | grep -A2 "names a console"; rm kit/include/nk/planted.h
```
Expected: `nativekit names a console:` and `kit/include/nk/planted.h: snes`.

- [ ] **Step 8: Run the gate, the x86-64 build and the iOS build**

Run the gate. Then:
```bash
cmake --build build/x86 -j
NSHVC_TESTBUILD=$PWD/build/x86/tests python3 consoles/snes/tests/golden.py compare build/golden
cmake --build build/ios -- -quiet CODE_SIGNING_ALLOWED=NO
```
Expected: the gate passes; the x86-64 golden compare (under Rosetta) gives `0 of N runs differ`; the iOS build links (the link proves `nk_game_main` is found). If the iOS configuration did not build at Task 0, say so and skip it.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "nativekit: hosts, coroutines and the resampler moved into kit/

nk_fatal is the console's; the configure fails when kit/ names a console.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 4: The routine table lookup

**Files:**
- Create: `kit/include/nk/routines.h`, `kit/src/dispatch/routines.c`, `kit/tests/routines_check.c`
- Modify: `consoles/snes/src/console/dispatch.c:45-60` (`find`), `kit/CMakeLists.txt`, `kit/tests/CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Produces: `const void *nk_routine_find(const void *base, int count, size_t stride, uint32_t addr);` over a table sorted by a `uint32_t` first member, returning the entry or `NULL`. CMake: `nk_dispatch` OBJECT library.

- [ ] **Step 1: Write the failing test**

`kit/tests/routines_check.c`:
```c
/* routines_check: the lookup over sorted tables of several shapes, absent
   addresses before, between and past the entries. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "nk/fatal.h"
#include "nk/routines.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}

typedef struct { uint32_t addr; void (*fn)(void); const char *name; int x; } Wide;
typedef struct { uint32_t addr; } Narrow;

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "routines_check: %s\n", #c); fails++; } } while (0)

int main(void)
{
    static const Wide w[] = { { 0x008000, 0, "a", 0 }, { 0x008010, 0, "b", 0 }, { 0xC00000, 0, "c", 0 } };
    static const Narrow n[] = { { 1 }, { 2 }, { 3 }, { 5 }, { 8 } };
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x008010) == &w[1]);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x008000) == &w[0]);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0xC00000) == &w[2]);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x007FFF) == NULL);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0x008001) == NULL);
    CHECK(nk_routine_find(w, 3, sizeof w[0], 0xFFFFFF) == NULL);
    CHECK(nk_routine_find(n, 5, sizeof n[0], 5) == &n[3]);
    CHECK(nk_routine_find(n, 5, sizeof n[0], 4) == NULL);
    CHECK(nk_routine_find(n, 0, sizeof n[0], 1) == NULL);
    CHECK(nk_routine_find(NULL, 0, sizeof n[0], 1) == NULL);
    if (fails)
        return 1;
    printf("routines_check: ok\n");
    return 0;
}
```
Add to `kit/tests/CMakeLists.txt`:
```cmake
add_executable(kit_routines_check routines_check.c $<TARGET_OBJECTS:nk_dispatch>)
target_include_directories(kit_routines_check PRIVATE ${NK_INCLUDE})
set_target_properties(kit_routines_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
```
Run: `cmake --build build/null -j`
Expected: FAIL (`nk/routines.h` not found).

- [ ] **Step 2: Implement**

`kit/include/nk/routines.h`:
```c
/*
 * routines.h: the routines with C a console reaches by address (a vector,
 * a computed call or jump), in a table sorted by address. Each entry's
 * first member is its address, a uint32_t; the rest is the console's.
 */
#ifndef NK_ROUTINES_H
#define NK_ROUTINES_H

#include <stddef.h>
#include <stdint.h>

/* the entry of `count` entries `stride` bytes apart at `base` whose
   address is addr; NULL when none is */
const void *nk_routine_find(const void *base, int count, size_t stride, uint32_t addr);

#endif
```
`kit/src/dispatch/routines.c`:
```c
/*
 * routines.c: the routine table's lookup (routines.h): a binary search.
 */
#include <string.h>

#include "routines.h"

const void *nk_routine_find(const void *base, int count, size_t stride, uint32_t addr)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *e = (const char *)base + (size_t)mid * stride;
        uint32_t at;
        memcpy(&at, e, sizeof at);
        if (at == addr)
            return e;
        if (at < addr)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}
```
In `kit/CMakeLists.txt`, add `add_library(nk_dispatch OBJECT ${NK_DIR}/src/dispatch/routines.c)` and add `nk_dispatch` to the `foreach(t ...)` list.

- [ ] **Step 3: Run the test**

Run: `cmake --build build/null -j && build/null/kit/tests/kit_routines_check`
Expected: `routines_check: ok`

- [ ] **Step 4: The SNES uses it**

In `consoles/snes/src/console/dispatch.c`, add `#include <stddef.h>` and `#include "routines.h"`, and replace `find`'s body:
```c
/* the table's first member is its address (kit/include/nk/routines.h) */
_Static_assert(offsetof(nshvc_routine, addr) == 0, "nshvc_routine's address first");

static const nshvc_routine *find(uint32_t addr)
{
    return (const nshvc_routine *)nk_routine_find(snes_cart.routines, snes_cart.routine_count,
                                                  sizeof *snes_cart.routines, addr & 0xFFFFFF);
}
```
Leave `spc_find` (the SPC700's scan with `uses` selection) as it is. In `consoles/snes/CMakeLists.txt`, add `$<TARGET_OBJECTS:nk_dispatch>` to the kit objects line.

- [ ] **Step 5: Run the gate**

Expected: passes (the lookup's result is the same entry for every address; the golden master covers every routine reached by the test cartridges and the chip tests).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "nativekit: the routine table's lookup

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 5: The cost-profile fitter as a library

**Files:**
- Create: `kit/tools/nk_profile.py`, `kit/tests/profile_lib_check.py`
- Modify: `consoles/snes/tools/nshvc-profile` (import the library; keep its command line, messages and file formats)

**Interfaces:**
- Produces, in `kit/tools/nk_profile.py`:
  - `subset_ls(A, b, idx)`, `fit_exact(A, b, yy)`: moved verbatim.
  - `fit_weights(frames, measured, defaults, active_clocks)` → `(weights, rows)`: `fit_cpu` generalised: `frames` rows `(frame, counts[k], work, bus)`, `measured[k]` booleans, `defaults[k]` flat list (clocks), `active_clocks` (a frame counts when `work + bus >= active_clocks`).
  - `errors(rows, weights)`, `fmt_err(e)`: moved verbatim.
  - `read_jobs(path, tool)`, `fit_program(jobs, nops)`, `chip_errors(jobs, fixed, weights)`, `fit_chip(path, tool, min_jobs)`: moved, with `"nshvc-profile"` in messages replaced by `tool` and `CHIP_MIN_JOBS` by `min_jobs`.
  - The chip jobs file keeps its header word `nshvc-chip-jobs`: it is a file format the SNES's games already write, so the library takes the header word as a parameter: `read_jobs(path, tool, header="nk-chip-jobs")`, and the SNES passes `header="nshvc-chip-jobs"`. The default must not name a console.

- [ ] **Step 1: Record the tool's current output on synthetic input**

```bash
python3 consoles/snes/tests/tools/profile_check.py
```
Read `profile_check.py`: it builds synthetic frames and chip jobs and calls the tool's functions or its command line. Record its exact stdout to `build/profile-before.txt`:
```bash
python3 consoles/snes/tests/tools/profile_check.py > build/profile-before.txt 2>&1; echo "exit $?"
```
Expected: exit 0.

- [ ] **Step 2: Write the failing library test**

`kit/tests/profile_lib_check.py`:
```python
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
```
Run: `python3 kit/tests/profile_lib_check.py`
Expected: FAIL (`No module named 'nk_profile'`).

- [ ] **Step 3: Write the library**

`kit/tools/nk_profile.py` starts with:
```python
"""
nk_profile: the cost profile's fit, for any console's tool
(kit/docs/kit.md). A console's tool measures its processors on its reference
and names its own kinds of access; this library fits the weights by one
fixed method (non-negative least squares, exact search over which weights
are not zero), reports the error, and fits a cartridge chip's programs.
"""
import itertools, sys
```
Then move, verbatim, from `consoles/snes/tools/nshvc-profile`: `subset_ls`, `fit_exact`, `errors`, `fmt_err`, `fit_program`, `chip_errors`. Then:
```python
def fit_weights(frames, measured, defaults, active_clocks):
    """each kind's weight in clocks: non-negative least squares on each frame's
       error in clocks (a frame's time is the sum of its costs, so its slowest
       frames weigh most), over the frames with real work; a kind not measured
       keeps its default"""
    rows = [(f[1], f[2], f[3]) for f in frames if f[2] + f[3] >= active_clocks]
    kk = range(len(measured))
    A = [[0.0] * len(measured) for _ in kk]
    b = [0.0] * len(measured)
    yy = 0.0
    for n, g, bus in rows:
        default = sum(n[k] * defaults[k] for k in kk if not measured[k])
        y = g - default
        yy += y * y
        for i in kk:
            if not measured[i]:
                continue
            b[i] += n[i] * y
            for j in kk:
                if measured[j]:
                    A[i][j] += n[i] * n[j]
    idx = [k for k in kk if measured[k]]
    sub = fit_exact([[A[i][j] for j in idx] for i in idx], [b[i] for i in idx], yy)
    w = [0.0] * len(measured)
    for i, k in enumerate(idx):
        w[k] = sub[i]
    return [w[k] if measured[k] else defaults[k] for k in kk], rows


def read_jobs(path, tool, header="nk-chip-jobs"):
    jobs, head = {}, None
    for line in open(path):
        w = line.split()
        if not w:
            continue
        if w[0] == header:
            if int(w[1]) != 1:
                sys.exit("%s: %s: chip jobs format %s, this tool reads 1" % (tool, path, w[1]))
            ops = w[w.index("ops") + 1:w.index("speed")]
            head = (w[2], ops, float(w[w.index("speed") + 1]))
        elif w[0] == "job":
            if not head:
                sys.exit("%s: %s: no %s line first" % (tool, path, header))
            jobs.setdefault(int(w[1], 16), []).append((float(w[2]), [int(x) for x in w[3:3 + len(head[1])]]))
    if not head:
        sys.exit("%s: %s: not a chip jobs file" % (tool, path))
    return head, jobs


def fit_chip(path, tool, min_jobs, header="nk-chip-jobs"):
    (name, ops, speed), jobs = read_jobs(path, tool, header)
    total = sum(c for js in jobs.values() for c, _ in js) or 1.0
    every = [j for js in jobs.values() for j in js]
    mult = sum(c for c, _ in every) / max(1, sum(speed * sum(n) for _, n in every))
    default = (0.0, [mult * speed] * len(ops))
    rows = []
    for entry in sorted(jobs):
        js = jobs[entry]
        if len(js) >= min_jobs:
            fixed, weights = fit_program(js, len(ops))
            how = "fit"
        else:
            m = sum(c for c, _ in js) / max(1, sum(speed * sum(n) for _, n in js))
            fixed, weights, how = 0.0, [m * speed] * len(ops), "multiplier"
        rows.append((entry, fixed, weights, how, len(js), sum(c for c, _ in js) / total,
                     chip_errors(js, fixed, weights)))
    return name, ops, speed, default, rows
```
`fit_weights` with `defaults[k] = DEFAULT_TENTHS[k // 4][k % 4] / 10` performs exactly `fit_cpu`'s arithmetic in the same order; keep it so.

- [ ] **Step 4: Run the library test**

Run: `python3 kit/tests/profile_lib_check.py`
Expected: `profile_lib_check: ok`

- [ ] **Step 5: The SNES tool uses the library**

In `consoles/snes/tools/nshvc-profile`, after the imports:
```python
sys.path.insert(0, os.path.join(ROOT, "kit", "tools"))
from nk_profile import subset_ls, fit_exact, errors, fmt_err, fit_program, chip_errors  # noqa: E402
import nk_profile  # noqa: E402
```
Delete its own `subset_ls`, `fit_exact`, `errors`, `fmt_err`, `fit_program`, `chip_errors`, `read_jobs`, `fit_chip`, and replace `fit_cpu` and the two with:
```python
def fit_cpu(frames, measured):
    return nk_profile.fit_weights(frames, measured, [DEFAULT_TENTHS[k // 4][k % 4] / 10 for k in range(8)],
                                  ACTIVE * FRAME)


def read_jobs(path):
    return nk_profile.read_jobs(path, "nshvc-profile", "nshvc-chip-jobs")


def fit_chip(path):
    return nk_profile.fit_chip(path, "nshvc-profile", CHIP_MIN_JOBS, "nshvc-chip-jobs")
```
Everything else (`fit_spc`, `spc_errors`, the reference runs, `write_profile`, `write_c`, `measure`, `check`, `main`) stays.

- [ ] **Step 6: The tool's output is unchanged**

```bash
python3 consoles/snes/tests/tools/profile_check.py > build/profile-after.txt 2>&1; echo "exit $?"
diff build/profile-before.txt build/profile-after.txt && echo same
```
Expected: `exit 0` and `same`. Then run the gate.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "nativekit: the cost profile's fitter as a library over any console's kinds

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 6: The console interface, settings by prefix, and the battery save

**Files:**
- Create: `kit/include/nk/nk.h`, `kit/include/nk/env.h`, `kit/src/front/env.c`, `kit/include/nk/save.h`, `kit/src/front/save.c`, `kit/tests/env_check.c`, `kit/tests/save_check.c`
- Modify: `kit/CMakeLists.txt`, `kit/tests/CMakeLists.txt`

**Interfaces:**
- Produces (`kit/include/nk/env.h`):
  ```c
  void nk_env_prefix(const char *prefix);           /* "NSHVC": the console's settings' prefix */
  const char *nk_getenv(const char *name);           /* getenv("<prefix>_<name>") */
  void nk_setenv(const char *name, const char *value); /* setenv("<prefix>_<name>"); fatal when it cannot */
  ```
- Produces (`kit/include/nk/save.h`):
  ```c
  typedef struct {
      uint8_t *ram;          /* the battery-backed RAM as it stands now */
      size_t bytes;          /* its size */
      size_t from, size;     /* the save area within it (size 0: all of it) */
      const char *name;      /* the save's file name, less ".srm" */
  } nk_battery;
  /* the save loaded from dir/NAME.srm into the RAM; get() fills b (0: no save) */
  void nk_save_load(int (*get)(nk_battery *b), const char *dir);
  /* written when the save area changed (a new file, renamed over the old) */
  void nk_save_write(void);
  ```
- Produces (`kit/include/nk/nk.h`): the `nk_console` table used by Tasks 7–13 (below), and `int nk_main(const nk_console *c, const void *cart, int argc, char **argv);` (implemented in Task 9).
- CMake: `nk_front` OBJECT library (env, save, and in Task 7 the script runner). Task 8 adds `nk_input` and Task 9 `nk_app` as libraries of their own, so a kit test links only what it needs (`app.c` needs a host and a renderer, `input.c` a host's `host_pad_read`).

- [ ] **Step 1: Write the failing tests**

`kit/tests/env_check.c`:
```c
/* env_check: settings by the console's prefix */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/env.h"
#include "nk/fatal.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}

int main(void)
{
    nk_env_prefix("KITX");
    setenv("KITX_STATES", "/tmp/plays", 1);
    if (!nk_getenv("STATES") || strcmp(nk_getenv("STATES"), "/tmp/plays"))
        return fprintf(stderr, "env_check: KITX_STATES not read\n"), 1;
    if (nk_getenv("NOT_SET_ANYWHERE"))
        return fprintf(stderr, "env_check: an unset setting read\n"), 1;
    nk_setenv("SCRIPT_PLAN", "M:0-0");
    if (!getenv("KITX_SCRIPT_PLAN") || strcmp(getenv("KITX_SCRIPT_PLAN"), "M:0-0"))
        return fprintf(stderr, "env_check: KITX_SCRIPT_PLAN not set\n"), 1;
    printf("env_check: ok\n");
    return 0;
}
```
`kit/tests/save_check.c`:
```c
/* save_check: the battery save. Loaded from DIR/NAME.srm when there is
   one; written only when the save area changed, through NAME.srm.new
   renamed over NAME.srm; a change outside the save area writes nothing;
   a save area past the RAM is fatal. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nk/fatal.h"
#include "nk/save.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "stub: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static uint8_t ram[64];
static size_t from = 16, size = 16;

static int get(nk_battery *b)
{
    b->ram = ram;
    b->bytes = sizeof ram;
    b->from = from;
    b->size = size;
    b->name = "kit test";
    return 1;
}

static long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) ? -1 : (long)st.st_size;
}

int main(int argc, char **argv)
{
    char dir[] = "/tmp/nk_save_XXXXXX", path[256];
    FILE *f;
    uint8_t back[64];
    if (!mkdtemp(dir))
        return 1;
    snprintf(path, sizeof path, "%s/kit test.srm", dir);
    if (argc > 1 && !strcmp(argv[1], "--past")) {
        from = 60;
        nk_save_load(get, dir);
        return 0;
    }
    memset(ram, 0xAA, sizeof ram);
    f = fopen(path, "wb");
    for (int i = 0; i < 64; i++)
        fputc(i, f);
    fclose(f);
    nk_save_load(get, dir);
    if (ram[5] != 5 || ram[63] != 63)
        return fprintf(stderr, "save_check: not loaded\n"), 1;
    remove(path);
    ram[2] = 0x55;                      /* outside the save area */
    nk_save_write();
    if (file_size(path) != -1)
        return fprintf(stderr, "save_check: written for a change outside the area\n"), 1;
    ram[20] = 0x99;                     /* inside it */
    nk_save_write();
    if (file_size(path) != 64)
        return fprintf(stderr, "save_check: not written (%ld)\n", file_size(path)), 1;
    snprintf(path + strlen(path), sizeof path - strlen(path), ".new");
    if (file_size(path) != -1)
        return fprintf(stderr, "save_check: the .new file left\n"), 1;
    path[strlen(path) - 4] = 0;
    f = fopen(path, "rb");
    if (fread(back, 1, 64, f) != 64 || back[20] != 0x99 || back[2] != 0x55)
        return fprintf(stderr, "save_check: wrong bytes written\n"), 1;
    fclose(f);
    printf("save_check: ok\n");
    return 0;
}
```
Add both to `kit/tests/CMakeLists.txt` the same way as `kit_routines_check`, with `$<TARGET_OBJECTS:nk_front>`.

Run: `cmake --build build/null -j`
Expected: FAIL (headers missing).

- [ ] **Step 2: Implement `env`**

`kit/include/nk/env.h` as in Interfaces, with a header comment: "env.h: the console's settings, each named PREFIX_NAME (the SNES: NSHVC_STATES...), so a console's settings keep their names". `kit/src/front/env.c`:
```c
/*
 * env.c: the console's settings by its prefix (env.h).
 */
#include <stdio.h>
#include <stdlib.h>

#include "env.h"
#include "fatal.h"

static const char *s_prefix = "NK";

void nk_env_prefix(const char *prefix) { s_prefix = prefix; }

static const char *full(const char *name)
{
    static char k[4][128];
    static int i;
    i = (i + 1) & 3;
    snprintf(k[i], sizeof k[i], "%s_%s", s_prefix, name);
    return k[i];
}

const char *nk_getenv(const char *name) { return getenv(full(name)); }

void nk_setenv(const char *name, const char *value)
{
    const char *k = full(name);
#ifdef _WIN32
    if (_putenv_s(k, value))
#else
    if (setenv(k, value, 1))
#endif
        nk_fatal("cannot set %s", k);
}
```

- [ ] **Step 3: Implement `save`**

`kit/include/nk/save.h` as in Interfaces, header comment: "save.h: the cartridge's battery RAM kept as NAME.srm in the host's save folder, written when its save area changes. A headless run never calls these." `kit/src/front/save.c` holds `console.c`'s `save_load` and `save_write` (consoles/snes/src/front/console.c:242-296) with the cartridge's fields taken from `nk_battery`:
```c
/*
 * save.c: the battery save (save.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fatal.h"
#include "save.h"

static int (*s_get)(nk_battery *b);
static uint8_t *saved;
static size_t save_from, save_size;
static char save_path[1024];

void nk_save_load(int (*get)(nk_battery *b), const char *dir)
{
    FILE *f;
    nk_battery b;
    s_get = get;
    if (!get || !get(&b) || !b.bytes || !b.ram)
        return;
    save_from = b.size ? b.from : 0;
    save_size = b.size ? b.size : b.bytes;
    if (save_from + save_size > b.bytes)
        nk_fatal("the cartridge's save area ($%X + $%X) is past its RAM ($%X)", (unsigned)save_from,
                 (unsigned)save_size, (unsigned)b.bytes);
    saved = (uint8_t *)malloc(b.bytes);
    if (!saved)
        nk_fatal("no memory for the save");
    memcpy(saved, b.ram, b.bytes);
    snprintf(save_path, sizeof save_path, "%s/%s.srm", dir, b.name);
    f = fopen(save_path, "rb");
    if (!f)
        return;
    if (fread(b.ram, 1, b.bytes, f) == b.bytes)
        memcpy(saved, b.ram, b.bytes);
    fclose(f);
}

void nk_save_write(void)
{
    char tmp[1100];
    FILE *f;
    nk_battery b;
    if (!saved || !s_get(&b))
        return;
    if (!memcmp(saved + save_from, b.ram + save_from, save_size))
        return;
    memcpy(saved, b.ram, b.bytes);
    snprintf(tmp, sizeof tmp, "%s.new", save_path);
    f = fopen(tmp, "wb");
    if (!f)
        return;
    fwrite(saved, 1, b.bytes, f);
    fclose(f);
    remove(save_path);
    rename(tmp, save_path);
}
```
Compare it line by line with `console.c`'s two functions: the message's `%X`s took `uint32_t` there; the SNES's values all fit 32 bits, so the text printed is the same. The SNES's `get` (Task 9) returns 0 when the cartridge has no `battery` flag, when its RAM size is 0 or `snes_sram()` is NULL, so the conditions are the same.

- [ ] **Step 4: Write `nk.h`**

`kit/include/nk/nk.h`:
```c
/*
 * nk.h: a native console on nativekit. The console is its machine; the kit
 * is the application around it: main's loop, the window, the frame's pace,
 * the sound out, the players' devices, the headless script and its states,
 * the battery save and the picture's presentation (kit/docs/kit.md). A
 * console fills one nk_console and calls nk_main.
 */
#ifndef NK_NK_H
#define NK_NK_H

#include <stddef.h>
#include <stdint.h>

#include "save.h"

#ifdef __cplusplus
extern "C" {
#endif

/* a named memory: `dump NAME` writes each as DIR/NAME<ext>, in table order */
typedef struct {
    const char *ext;                    /* ".wram" */
    const void *(*data)(size_t *bytes); /* as it stands now; NULL with 0 bytes: an empty file */
    int optional;                       /* 1: no file when data gives NULL */
} nk_region;

/* a button's name in the script ("B", "START") and the console's bit */
typedef struct {
    const char *name;
    uint32_t bit;
} nk_button;

typedef struct nk_console {
    const char *name;                   /* "nativeshvc": the window's title fallback, messages */
    const char *env;                    /* "NSHVC": its settings' prefix (env.h) */
    const char *play_ext;               /* ".sfx": the plays in <env>_STATES that save states */

    /* the machine */
    void     (*power)(const void *cart);
    void     (*run_frame)(void);        /* to this frame's end */
    uint64_t (*frame_ns)(void);         /* the frame's length now, ns */
    void     (*app_name)(const void *cart, const char **dir, const char **title); /* the save folder, the title */

    /* the picture */
    void     (*window_size)(int *w, int *h);              /* the window's first size */
    double   (*aspect)(void);                              /* the picture's display aspect now */
    void     (*scaled)(int w, int h);                      /* optional: the picture's size on the target */
    void     (*present)(const int d[4]);                   /* the picture onto target rectangle d */
    /* a shot's pixels: w x h, every xs-th column and ys-th row written */
    const uint32_t *(*shot)(int *w, int *h, int *xs, int *ys);
    /* optional, NSHVC_RRCHECK: draw this frame onto d and fill want (tw x th)
       with its reference; 0 when the generic one applies (the picture
       presented, and drawn scaled on the CPU) */
    int      (*rrcheck)(const int d[4], uint32_t *want, int tw, int th);
    void     (*rrcheck_why)(int x, int y);                 /* optional: more about a pixel that differs */
    const char *rrcheck_own;            /* the report's words for frames rrcheck drew ("with ...") */
    const uint32_t *(*picture)(int *w, int *h);            /* the console's picture as it stands */
    void     (*headless_scale)(double sx, double sy);      /* optional: <env>_SCALE in a headless run */

    /* the sound */
    double   audio_hz;
    int      (*audio_take)(int16_t *lr, int max);
    int      (*audio_queued)(void);

    /* the controllers */
    const nk_button *buttons;
    int      button_count;
    const char *const *steps;           /* the script steps the console takes itself */
    int      step_count;
    int      (*script_step)(int argc, char w[][1024]);     /* one of them: 1 when taken */
    int      (*key)(int vk, int down);                     /* optional: 1 when the game took the key */
    void     (*touch)(int id, int phase, float x, float y);/* optional: the game's touch hook */
    /* the players' controllers from the frame's input: headless, the
       script's buttons (b: players 0 and 1); else the kit's devices (input.h) */
    void     (*input)(int headless, const uint32_t b[2]);
    void     (*frame)(void);            /* optional: the game's frame hook */

    /* memories */
    const nk_region *regions;
    int      region_count;
    int      (*battery)(nk_battery *b); /* optional: 0 no save */
    void     (*counts_hooks)(void (*rd)(uint32_t addr, uint8_t v), void (*wr)(uint32_t addr, uint8_t v));
    int      (*access_kind)(uint32_t addr);
    int      kind_count;

    void     (*at_exit)(void);          /* optional: its own checks' reports */
} nk_console;

/* power on, open what the arguments ask for and run until the window
   closes (or a headless run's end) */
int nk_main(const nk_console *c, const void *cart, int argc, char **argv);

/* the console nk_main runs (NULL before) */
const nk_console *nk_console_now(void);
/* <env>_SCALE (headless): window pixels a console pixel across and down, 0 when not set */
void nk_shot_scale(double *x, double *y);

#ifdef __cplusplus
}
#endif
#endif
```
The spec allows the fields to tighten; this is the tightened table: `app_name`, `shot`, `picture`, `rrcheck_why`, `headless_scale`, `steps`, `key`, `touch`, `input` and `frame` come from reading `console.c`, `script.c` and `input.c`; `player_state` and `kinds` from the spec are replaced by `input` (the console reads the kit's devices itself, Task 8) and `kind_count` (the kinds' names are the profile tool's, Task 5).

- [ ] **Step 5: CMake and run**

In `kit/CMakeLists.txt`: `add_library(nk_front OBJECT ${NK_DIR}/src/front/env.c ${NK_DIR}/src/front/save.c)` and add it to the `foreach`. Run:
```bash
cmake --build build/null -j && build/null/kit/tests/kit_env_check && build/null/kit/tests/kit_save_check && build/null/kit/tests/kit_save_check --past; echo "exit $?"
```
Expected: `env_check: ok`, `save_check: ok`, then `stub: the cartridge's save area ($3C + $10) is past its RAM ($40)` and `exit 70`. Then the gate (nothing in the SNES uses these yet: it must pass unchanged).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "nativekit: the console interface, settings by prefix, the battery save

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 7: The script runner and its states

**Files:**
- Move: `consoles/snes/src/front/script.c` → `kit/src/front/script.c`; `consoles/snes/src/front/script.h` → `kit/include/nk/script.h`
- Create: `kit/tests/stub_console.c`, `kit/tests/script_check.c`, `kit/tests/states_check.py`, `consoles/snes/src/front/snes_console.c` (the table, filled further in Tasks 8–9)
- Modify: `kit/src/front/script.c`, `kit/include/nk/script.h`, `consoles/snes/src/front/console.c` (calls), `kit/CMakeLists.txt`, `kit/tests/CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Consumes: `nk_console` fields `name`, `env`, `play_ext`, `buttons`, `button_count`, `steps`, `step_count`, `script_step`, `shot`, `regions`, `region_count`; `nk_getenv`, `nk_setenv`, `nk_fatal`.
- Produces (`kit/include/nk/script.h`):
  ```c
  void nk_script_console(const nk_console *c);           /* before nk_script_load */
  void nk_script_load(const char *path, const char *out_dir);
  void nk_script_args(int argc, char **argv);
  int  nk_script_replaying(void);
  int  nk_script_steps(void);
  void nk_script_takes(void);
  int  nk_script_frame(uint32_t *b0, uint32_t *b1);
  ```
- Produces (SNES): `extern const nk_console snes_console;` declared in `consoles/snes/src/front/snes_console.h`, defined in `snes_console.c`.

- [ ] **Step 1: Write the stub console and the failing tests**

`kit/tests/stub_console.c`: a console whose machine is a frame counter and a xorshift sequence, with two regions, buttons `A`(1) `B`(2) `START`(8), one own step `poke N` (adds N to the sequence's state), and a `shot` that returns a 4x2 picture made from the state. It defines `nk_fatal` (prefix `stub: `, exit 70), `stub_console` (an `nk_console` with `name = "stub"`, `env = "STUB"`, `play_ext = ".play"`), and a `main` that runs a headless script loop with the kit's script runner alone:
```c
/* stub_console: a console for the kit's checks: its machine a frame
   counter and a xorshift sequence the buttons stir. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/env.h"
#include "nk/fatal.h"
#include "nk/nk.h"
#include "nk/script.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "stub: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static uint32_t state = 1, frames;
static uint32_t pic[8];

static void stir(uint32_t b)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    state += b;
    frames++;
}

static const void *r_state(size_t *n) { *n = sizeof state; return &state; }
static const void *r_frames(size_t *n) { *n = sizeof frames; return &frames; }
static const void *r_none(size_t *n) { *n = 0; return NULL; }

static const uint32_t *shot(int *w, int *h, int *xs, int *ys)
{
    int i;
    for (i = 0; i < 8; i++)
        pic[i] = 0xFF000000u | (state >> i) * 2654435761u;
    *w = 4;
    *h = 2;
    *xs = *ys = 1;
    return pic;
}

static int step(int n, char w[][1024])
{
    if (!strcmp(w[0], "poke") && n == 2) {
        state += (uint32_t)atoi(w[1]);
        return 1;
    }
    return 0;
}

static const nk_region regions[] = {
    { ".state", r_state, 0 }, { ".frames", r_frames, 0 }, { ".none", r_none, 1 }, { ".empty", r_none, 0 },
};
static const nk_button buttons[] = { { "A", 1 }, { "B", 2 }, { "START", 8 } };
static const char *const steps[] = { "poke" };

const nk_console stub_console = {
    .name = "stub", .env = "STUB", .play_ext = ".play",
    .shot = shot,
    .buttons = buttons, .button_count = 3,
    .steps = steps, .step_count = 1, .script_step = step,
    .regions = regions, .region_count = 4,
};

int main(int argc, char **argv)
{
    const char *script = NULL, *out = ".";
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
    }
    nk_env_prefix(stub_console.env);
    nk_script_console(&stub_console);
    nk_script_args(argc, argv);
    if (!script)
        return 2;
    nk_script_load(script, out);
    {
        uint32_t b0, b1;
        nk_script_takes();
        nk_script_frame(&b0, &b1);
        for (;;) {
            stir(b0 | b1 << 16);
            if (!nk_script_frame(&b0, &b1))
                return 0;
        }
    }
}
```
`kit/tests/script_check.c` is not needed separately: the parser's checks run the stub with small scripts from `states_check.py`.

`kit/tests/states_check.py`:
```python
#!/usr/bin/env python3
"""states_check: the kit's script runner on the stub console. Steps and
   their errors (with line numbers, the console's own steps, unknown steps
   named with the console's name); dumps of every region (an optional one
   absent, an empty one written empty); save and load with fork and with
   STUB_SCRIPT_NOFORK=1, every dump the same both ways and the machine each
   load names; a load of a state the script never saved, rebuilt from the
   play in STUB_STATES that saves it."""
import filecmp, os, subprocess, sys, tempfile

STUB = sys.argv[1]
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
        if not filecmp.cmp(os.path.join(a, n), os.path.join(b, n), shallow=False):
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
if "s.txt:1: 'poke'" not in err:
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
```
Add to `kit/tests/CMakeLists.txt`:
```cmake
add_executable(kit_stub_console stub_console.c $<TARGET_OBJECTS:nk_front>)
target_include_directories(kit_stub_console PRIVATE ${NK_INCLUDE})
set_target_properties(kit_stub_console PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
```
Run: `cmake --build build/null -j`
Expected: FAIL (`nk/script.h` not found).

- [ ] **Step 2: Move the script runner and make it the kit's**

```bash
git mv consoles/snes/src/front/script.c kit/src/front/script.c
git mv consoles/snes/src/front/script.h kit/include/nk/script.h
```
Edit `kit/src/front/script.c` (each change keeps behaviour and text for the SNES):
1. Includes: drop `nshvc/nshvc.h`, `nshvc/cart.h`, `snes_internal.h`, `ppu.h`; add `"nk.h"`, `"env.h"`, `"fatal.h"`, `"script.h"`. Add `static const nk_console *con;` and `void nk_script_console(const nk_console *c) { con = c; }`.
2. `nshvc_fatal(` → `nk_fatal(` everywhere.
3. `Step.buttons` becomes `uint32_t buttons[2]`; `add_step`'s `b0`, `b1` and `parse_buttons`' outputs become `uint32_t`; `parse_buttons` looks names up in `con->buttons` (`con->button_count` entries) instead of its own table. Keep the `2.` prefix and the `+` split exactly.
4. `script_range`: replace `!strcmp(cmd, "plug") || ... || !strcmp(cmd, "gun")` with a loop over `con->steps`; replace the last message with `nk_fatal("%s:%d: '%s' is not a step %s's script runner takes", path, i + 1, cmd, con->name);`.
5. `shot`: replace its body's picture logic with the console's:
   ```c
   static void shot(const char *name)
   {
       int w, h, x, y, xs, ys;
       const uint32_t *px = con->shot(&w, &h, &xs, &ys);
       char path[1024];
       FILE *f;
       snprintf(path, sizeof path, "%s/%s.ppm", out, name);
       f = fopen(path, "wb");
       if (!f)
           nk_fatal("cannot write %s", path);
       fprintf(f, "P6\n%d %d\n255\n", w / xs, h / ys);
       for (y = 0; y < h / ys; y++)
           for (x = 0; x < w / xs; x++) {
               uint32_t c = px[(y * ys) * w + x * xs];
               uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
               fwrite(rgb, 1, 3, f);
           }
       fclose(f);
   }
   ```
   and keep the interlace and hires halving in the SNES (Step 4).
6. `chip_ram` and `dump`: replace with
   ```c
   static void dump(const char *name)
   {
       int i;
       for (i = 0; i < con->region_count; i++) {
           size_t n = 0;
           const void *p = con->regions[i].data(&n);
           if (!p && con->regions[i].optional)
               continue;
           write_file(name, con->regions[i].ext, p, p ? n : 0);
       }
   }
   ```
7. `input_step`: replace its body with
   ```c
   static void input_step(const Step *s)
   {
       char w[8][1024];
       int n = words(s->text, w, 8);
       if (!con->script_step || !con->script_step(n, w))
           nk_fatal("%s:%d: '%s'", cur->path, s->line, s->text);
   }
   ```
8. Settings: `getenv("NSHVC_STATES")` → `nk_getenv("STATES")`, and likewise `NSHVC_SCRIPT_NOFORK`, `NSHVC_SCRIPT_PLAN`, `NSHVC_SCRIPT_FRAMES`, `NSHVC_SCRIPT_SAVED`, `NSHVC_SCRIPT_RESUME`; `env_set("NSHVC_SCRIPT_...", v)` → `nk_setenv("SCRIPT_...", v)` and delete `env_set`. The two messages that name the setting: `"load %s: set NSHVC_STATES to the folder ..."` becomes `nk_fatal("load %s: set %s_STATES to the folder of the plays that save states", name, con->env)`, and `"NSHVC_STATES: cannot read %s"` becomes `nk_fatal("%s_STATES: cannot read %s", con->env, dir)`. `"NSHVC_SCRIPT_PLAN: '%s'"` likewise with `%s_SCRIPT_PLAN`.
9. Plays: `".sfx"` → `con->play_ext` in `find_play` (both the readdir and the `_findfirst` branches; the `_findfirst` pattern built as `"%s/*%s"` with `dir, con->play_ext`; the length test `k < 4 || strcmp(e->d_name + k - 4, ".sfx")` becomes a test against `strlen(con->play_ext)`), and in the comments.
10. Rename the public functions: `script_load` → `nk_script_load`, `script_args` → `nk_script_args`, `script_replaying` → `nk_script_replaying`, `script_steps` → `nk_script_steps`, `script_takes` → `nk_script_takes`, `script_frame` → `nk_script_frame` (its outputs `uint32_t *`). `"save and load need the program's arguments (script_args)"` becomes `"(nk_script_args)"`: this message is reached only by a program that never called it, which no SNES program does; say so in the commit message.
11. Comments: say "the console's processors' coroutines" for "the S-CPU's and the SPC700's coroutines", `<prefix>_STATES (*<play_ext>)` for `NSHVC_STATES (*.sfx)`, and so on, so the name check passes.

`kit/include/nk/script.h`: the declarations in Interfaces, its comment's step list kept (the generic steps), with "the console's own steps (the SNES: plug, pad, mouse, gun)" said without naming: "the console's own steps (nk_console.steps)". Remove `front_shot_picture` from it (Step 4 keeps it in the SNES).

Add `${NK_DIR}/src/front/script.c` to `nk_front`.

- [ ] **Step 3: Run the kit's check**

Run: `cmake --build build/null --target kit_stub_console && python3 kit/tests/states_check.py build/null/kit/tests/kit_stub_console`
Expected: `states_check: ok`. The SNES does not build yet (Step 4).

- [ ] **Step 4: The SNES's side of the script**

`consoles/snes/src/front/snes_console.h`:
```c
/*
 * snes_console.h: the SNES as nativekit's console (kit/include/nk/nk.h).
 */
#ifndef NSHVC_SNES_CONSOLE_H
#define NSHVC_SNES_CONSOLE_H

#include "nk.h"

extern const nk_console snes_console;

/* the picture a shot holds (snes_present.c): nshvc_picture's, or with
   NSHVC_SCALE the frame presented at that scale (*presented 1) */
const uint32_t *front_shot_picture(int *w, int *h, int *presented);

#endif
```
`consoles/snes/src/front/snes_console.c` (Tasks 8 and 9 add the rest of the table):
```c
/*
 * snes_console.c: the SNES as nativekit's console: its script steps and
 * buttons, its memories for dumps, its shots.
 */
#include <stdlib.h>
#include <string.h>

#include "nshvc/nshvc.h"
#include "nshvc/cart.h"
#include "snes_internal.h"
#include "ppu.h"
#include "snes_console.h"

/* ---- the script ---- */
static const nk_button buttons[] = {
    { "B", NSHVC_B }, { "Y", NSHVC_Y }, { "SELECT", NSHVC_SELECT }, { "START", NSHVC_START },
    { "UP", NSHVC_UP }, { "DOWN", NSHVC_DOWN }, { "LEFT", NSHVC_LEFT }, { "RIGHT", NSHVC_RIGHT },
    { "A", NSHVC_A }, { "X", NSHVC_X }, { "L", NSHVC_L }, { "R", NSHVC_R },
};
static const char *const steps[] = { "plug", "pad", "mouse", "gun" };

/* plug PORT DEVICE (joypad, none, mouse, multitap, superscope, justifier,
   justifiers); pad N BITS (joypad N's buttons from now, hex; 0 and 1 are
   hold's); mouse PORT DX DY BUTTONS (motion for its next report); gun G X
   Y BUTTONS (from now) */
static int script_step(int n, char w[][1024])
{
    static const char *const dev[] = { "joypad", "none", "mouse", "multitap", "superscope", "justifier",
                                       "justifiers" };
    int i;
    if (!strcmp(w[0], "plug") && n == 3) {
        for (i = 0; i < 7; i++)
            if (!strcmp(w[2], dev[i])) {
                nshvc_plug(atoi(w[1]), i);
                return 1;
            }
    } else if (!strcmp(w[0], "pad") && n == 3) {
        nshvc_set_joypad(atoi(w[1]), (uint16_t)strtoul(w[2], NULL, 16));
        return 1;
    } else if (!strcmp(w[0], "mouse") && n == 5) {
        nshvc_set_mouse(atoi(w[1]), atoi(w[2]), atoi(w[3]), atoi(w[4]));
        return 1;
    } else if (!strcmp(w[0], "gun") && n == 5) {
        nshvc_set_gun(atoi(w[1]), atoi(w[2]), atoi(w[3]), (unsigned)atoi(w[4]));
        return 1;
    }
    return 0;
}

/* A shot is the picture made the way the reference's are (tests/oracle):
   an interlaced picture whose two fields' rows are the same is its lines
   once, a hires one of the console's width whose pixel pairs are all the
   same 256 wide. A presented one (NSHVC_SCALE) is as presented. */
static const uint32_t *shot(int *w, int *h, int *xs, int *ys)
{
    int x, y, presented;
    const uint32_t *px = front_shot_picture(w, h, &presented);
    const PpuFrame *fr = ppu_frame();
    *xs = *ys = 1;
    if (!presented && fr->interlace && *h == 2 * fr->screen_height) {
        *ys = 2;
        for (y = 0; y + 1 < *h && *ys == 2; y += 2)
            if (memcmp(px + (size_t)y * (size_t)*w, px + (size_t)(y + 1) * (size_t)*w, (size_t)*w * sizeof *px))
                *ys = 1;
    }
    if (!presented && fr->width == PPU_WIDTH && *w == PPU_HIRES_WIDTH) {
        *xs = 2;
        for (y = 0; y < *h && *xs == 2; y++)
            for (x = 0; x < *w; x += 2)
                if (px[y * *w + x] != px[y * *w + x + 1]) {
                    *xs = 1;
                    break;
                }
    }
    return px;
}

/* ---- the memories a dump writes, in its order ---- */
static const void *r_wram(size_t *n) { *n = 0x20000; return snes_wram(); }
static const void *r_sram(size_t *n) { *n = nshvc_sram() ? snes_sram_size() : 0; return nshvc_sram(); }
static const void *r_chipram(size_t *n)
{
    uint32_t g = 0;
    *n = 0;
    if (!snes_cart.chip || !snes_cart.chip->ram)
        return NULL;
    {
        uint8_t *p = snes_cart.chip->ram(&g);
        *n = g;
        return p;
    }
}
static const void *r_vram(size_t *n) { *n = 0x10000 * PPU_VRAM_PAGES; return nshvc_vram(); }
static const void *r_cgram(size_t *n) { *n = 0x200; return nshvc_cgram(); }
static const void *r_oam(size_t *n) { *n = 544; return nshvc_oam(); }

static const nk_region regions[] = {
    { ".wram", r_wram, 0 }, { ".sram", r_sram, 0 }, { ".chipram", r_chipram, 1 },
    { ".vram", r_vram, 0 }, { ".cgram", r_cgram, 0 }, { ".oam", r_oam, 0 },
};

const nk_console snes_console = {
    .name = "nativeshvc", .env = "NSHVC", .play_ext = ".sfx",
    .shot = shot,
    .buttons = buttons, .button_count = sizeof buttons / sizeof buttons[0],
    .steps = steps, .step_count = 4, .script_step = script_step,
    .regions = regions, .region_count = sizeof regions / sizeof regions[0],
};
```
Check against the old `dump`: `.sram` was written with `nshvc_sram() ? n : 0` bytes and always written; `.chipram` only when the chip had RAM (old `if (gr)`: `chip->ram` returning NULL wrote nothing): `optional` gives both. The old `dump` wrote `.vram` with `nshvc_vram()`; keep that.

In `consoles/snes/src/front/console.c`: `#include "script.h"` stays (now the kit's), add `#include "env.h"` and `#include "snes_console.h"`; in `nshvc_main` before `script_args`: `nk_env_prefix(snes_console.env); nk_script_console(&snes_console);`; rename the calls (`nk_script_args`, `nk_script_load`, `nk_script_takes`, `nk_script_frame`, `nk_script_replaying`, `nk_script_steps`) and make `b0`, `b1` `uint32_t` where `nk_script_frame` fills them (casts to `uint16_t` where they reach `nshvc_set_joypad`, as before). In `consoles/snes/CMakeLists.txt`: remove `src/front/script.c`, add `src/front/snes_console.c` and `$<TARGET_OBJECTS:nk_front>`.

- [ ] **Step 5: Run the gate and the states checks**

```bash
cmake --build build/null -j && python3 kit/tests/states_check.py build/null/kit/tests/kit_stub_console
python3 consoles/snes/tests/unit/states.py && NSHVC_SCRIPT_NOFORK=1 python3 consoles/snes/tests/unit/states.py
```
Then the gate (it holds every shot, dump and both states runs). Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "nativekit: the script runner and its states, the console's steps and memories its own

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 8: Native input devices

**Files:**
- Create: `kit/include/nk/input.h`, `kit/src/front/input.c` (the device half of today's input.c), `kit/tests/input_check.c`
- Move: `consoles/snes/src/front/input.c` → `consoles/snes/src/front/snes_input.c`; `consoles/snes/src/front/input.h` → `consoles/snes/src/front/snes_input.h`
- Modify: `consoles/snes/src/front/snes_input.c`, `consoles/snes/src/front/console.c` (calls), `consoles/snes/tests/unit/input.c` (calls only, if it calls `input_*`), `consoles/snes/tests/CMakeLists.txt`, `kit/CMakeLists.txt`, `kit/tests/CMakeLists.txt`

**Interfaces:**
- Produces (`kit/include/nk/input.h`):
  ```c
  #define NK_PLAYERS 8
  enum { NK_INPUT_NONE, NK_INPUT_KEYBOARD, NK_INPUT_GAMEPAD, NK_INPUT_MOUSE, NK_INPUT_TOUCH, NK_INPUT_SCRIPT };
  enum { NK_PAD_GENERIC, NK_PAD_XBOX, NK_PAD_PLAYSTATION, NK_PAD_NINTENDO };
  typedef struct { int kind; int style; char name[64]; } nk_input_source;
  /* positional bits, host_pad's (host.h): 0 bottom 1 right 2 left 3 top 4 LB 5 RB 6 view 7 menu,
     12-15 d-pad up, right, down, left (the left stick past half sets these too) */
  unsigned nk_pad_bits(const host_pad *p);
  void nk_input_key(int vk, int down);
  int  nk_input_key_held(int vk);
  unsigned nk_input_keyboard_bits(void);       /* the keys held, as positional bits */
  const char *nk_input_key_name(unsigned bit_mask); /* the first key for a positional bit ("Z"), "" none */
  void nk_input_focus_lost(void);
  void nk_input_mouse(float x, float y, float dx, float dy, int buttons);
  void nk_input_touch(int id, int phase, float x, float y);
  typedef struct { float x, y, dx, dy; int buttons, seen; } nk_mouse;
  const nk_mouse *nk_input_mouse_now(void);
  void nk_input_mouse_frame_done(void);         /* dx, dy and seen cleared */
  int  nk_input_touches(float x[2], float y[2]);
  /* the controllers read for this frame (now/on given: a check's own) */
  void nk_input_pads(const host_pad *now, const int *on);
  const host_pad *nk_input_pad(int k);         /* controller k, NULL when not connected */
  void nk_input_use(int player, int kind, const host_pad *pad);
  void nk_input_script(void);                  /* a headless run: the script drives every player */
  void nk_input_report(void (*hook)(int player, const nk_input_source *src));
  const nk_input_source *nk_input_source_of(int player);
  int  nk_input_point(const int *at, float scale, int width, int height, int left, int top, float x, float y,
                      int *col, int *row);       /* today's input_point */
  ```
- Consumes: `host_pad`, `host_pad_read` (`host.h`).
- The SNES keeps `nshvc_player_source`, `nshvc_player_label` and its controllers' mapping in `snes_input.c`; `nshvc_input_source` is the same layout as `nk_input_source` (static asserts).

- [ ] **Step 1: Write the failing test**

`kit/tests/input_check.c`:
```c
/* input_check: the kit's devices. Keys to positional bits and back to
   their names; a controller's stick past half on the d-pad's bits; a new
   press on a controller makes it its player's source, reported once; the
   mouse's motion summed over a frame then cleared; two touches at most,
   one ended taken out. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/fatal.h"
#include "nk/host.h"
#include "nk/input.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(70);
}
int host_pad_read(int index, host_pad *p) { (void)index; memset(p, 0, sizeof *p); return 0; }

static int fails, reports, last_player, last_kind;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "input_check: %s\n", #c); fails++; } } while (0)
static void hook(int player, const nk_input_source *s) { reports++; last_player = player; last_kind = s->kind; }

int main(void)
{
    host_pad now[NK_PLAYERS];
    int on[NK_PLAYERS] = { 0 };
    float tx[2], ty[2];
    memset(now, 0, sizeof now);
    nk_input_key('Z', 1);
    nk_input_key(0x26, 1);
    CHECK(nk_input_keyboard_bits() == (1u << 0 | 1u << 12));
    CHECK(!strcmp(nk_input_key_name(1u << 0), "Z"));
    CHECK(!strcmp(nk_input_key_name(1u << 6), "Right Shift"));
    nk_input_report(hook);
    CHECK(reports == 1 && last_player == 0 && last_kind == NK_INPUT_KEYBOARD);
    nk_input_focus_lost();
    CHECK(nk_input_keyboard_bits() == 0 && !nk_input_key_held('Z'));

    now[1].x = 0.75f;
    now[1].buttons = 1u << 1;
    now[1].style = HOST_PAD_PLAYSTATION;
    on[1] = 1;
    CHECK(nk_pad_bits(&now[1]) == (1u << 1 | 1u << 13));
    nk_input_pads(now, on);
    nk_input_report(hook);
    CHECK(reports == 2 && last_player == 1 && last_kind == NK_INPUT_GAMEPAD);
    nk_input_pads(now, on);                 /* held, not pressed again */
    nk_input_report(hook);
    CHECK(reports == 2);
    CHECK(nk_input_pad(1) && nk_input_pad(1)->style == HOST_PAD_PLAYSTATION && !nk_input_pad(2));

    nk_input_mouse(10, 20, 3, 4, 1);
    nk_input_mouse(11, 21, 2, -1, 1);
    CHECK(nk_input_mouse_now()->dx == 5 && nk_input_mouse_now()->dy == 3 && nk_input_mouse_now()->seen);
    nk_input_mouse_frame_done();
    CHECK(nk_input_mouse_now()->dx == 0 && !nk_input_mouse_now()->seen && nk_input_mouse_now()->x == 11);

    nk_input_touch(7, 0, 1, 1);
    nk_input_touch(8, 0, 2, 2);
    nk_input_touch(9, 0, 3, 3);              /* a third: not kept */
    CHECK(nk_input_touches(tx, ty) == 2);
    nk_input_touch(7, 2, 0, 0);
    CHECK(nk_input_touches(tx, ty) == 1 && tx[0] == 2);
    if (fails)
        return 1;
    printf("input_check: ok\n");
    return 0;
}
```
Add it to `kit/tests/CMakeLists.txt` with `$<TARGET_OBJECTS:nk_input>` (it defines its own `host_pad_read`, so it does not link a host).

Run: `cmake --build build/null -j`
Expected: FAIL (`nk/input.h` not found).

- [ ] **Step 2: Split `input.c`**

```bash
git mv consoles/snes/src/front/input.c consoles/snes/src/front/snes_input.c
git mv consoles/snes/src/front/input.h consoles/snes/src/front/snes_input.h
```
Create `kit/src/front/input.c` with, from `snes_input.c`, the device half, renamed and freed of SNES types:
- `source[]`, `source_changed[]`, `pads[]`, `pad_on[]` and `use` (→ `nk_input_use`): `nshvc_input_source` → `nk_input_source`, `NSHVC_PAD_*` → `NK_PAD_*`, `NSHVC_INPUT_*` → `NK_INPUT_*`; the same names ("Keyboard", "Mouse", "Touch screen", "Script").
- The keyboard: a table of the same keys in the same order, each with a positional bit instead of the SNES's bit:
  ```c
  static const struct { int vk; unsigned bit; const char *name; } k_map[] = {
      { 0x26, 1u << 12, "Up" }, { 0x28, 1u << 14, "Down" }, { 0x25, 1u << 15, "Left" },
      { 0x27, 1u << 13, "Right" }, { 'Z', 1u << 0, "Z" }, { 'X', 1u << 1, "X" }, { 'A', 1u << 2, "A" },
      { 'S', 1u << 3, "S" }, { 'Q', 1u << 4, "Q" }, { 'W', 1u << 5, "W" }, { 0x0D, 1u << 7, "Return" },
      { 0x10, 1u << 6, "Right Shift" }, { 0x09, 1u << 6, "Tab" },
  };
  ```
  (the SNES's bits map to these exactly as `pad_joypad` maps a controller's: B bottom 0, A right 1, Y left 2, X top 3, L 4, R 5, SELECT view 6, START menu 7, d-pad 12-15). `nk_input_key` keeps a held set of virtual keys (a 256-entry array) and the positional bits held; on a press of a key in the table it calls `nk_input_use(0, NK_INPUT_KEYBOARD, NULL)`. `nk_input_focus_lost` clears both.
- The mouse and touches: today's code, renamed (`nk_input_mouse`, `nk_input_touch`), plus `nk_input_mouse_now`, `nk_input_mouse_frame_done`, `nk_input_touches`.
- `nk_pad_bits`:
  ```c
  unsigned nk_pad_bits(const host_pad *p)
  {
      unsigned b = p->buttons & 0xFFu;
      if ((p->buttons & 1u << 12) || p->y < -0.5f) b |= 1u << 12;
      if ((p->buttons & 1u << 13) || p->x > 0.5f) b |= 1u << 13;
      if ((p->buttons & 1u << 14) || p->y > 0.5f) b |= 1u << 14;
      if ((p->buttons & 1u << 15) || p->x < -0.5f) b |= 1u << 15;
      return b;
  }
  ```
  Bits 8-11 (sticks' clicks, triggers) are left out exactly as `pad_joypad` leaves them out of the joypad; the source test keeps using the raw `buttons` for those.
- `nk_input_pads(now, on)`: today's loop's first half:
  ```c
  void nk_input_pads(const host_pad *now, const int *on)
  {
      host_pad read[NK_PLAYERS];
      int got[NK_PLAYERS], k;
      if (!now) {
          for (k = 0; k < NK_PLAYERS; k++)
              got[k] = host_pad_read(k, &read[k]);
          now = read;
          on = got;
      }
      for (k = 0; k < NK_PLAYERS; k++) {
          unsigned was = pad_on[k] ? nk_pad_bits(&pads[k]) : 0, was_b = pad_on[k] ? pads[k].buttons : 0;
          pad_on[k] = on[k];
          if (on[k])
              pads[k] = now[k];
          if (pad_on[k] && ((nk_pad_bits(&pads[k]) & ~was) || (pads[k].buttons & ~was_b)))
              nk_input_use(k, NK_INPUT_GAMEPAD, &pads[k]);
      }
  }
  ```
- `input_script` → `nk_input_script`, `input_report` → `nk_input_report`, `input_point` → `nk_input_point` (renamed parameters `col`, `row`), `nk_input_source_of`, `nk_input_pad`, `nk_input_key_name` (the first entry of `k_map` whose bit is the mask), `nk_input_key_held`, `nk_input_keyboard_bits`.

`consoles/snes/src/front/snes_input.c` keeps the SNES half, reading the kit's devices:
- Delete what moved. `pad_joypad(p)` becomes a map from positional bits:
  ```c
  /* positional bits (kit/include/nk/input.h) as the joypad's */
  static uint16_t joypad_of(unsigned b)
  {
      uint16_t j = 0;
      if (b & 1u << 0) j |= NSHVC_B;
      if (b & 1u << 1) j |= NSHVC_A;
      if (b & 1u << 2) j |= NSHVC_Y;
      if (b & 1u << 3) j |= NSHVC_X;
      if (b & 1u << 4) j |= NSHVC_L;
      if (b & 1u << 5) j |= NSHVC_R;
      if (b & 1u << 6) j |= NSHVC_SELECT;
      if (b & 1u << 7) j |= NSHVC_START;
      if (b & 1u << 12) j |= NSHVC_UP;
      if (b & 1u << 13) j |= NSHVC_RIGHT;
      if (b & 1u << 14) j |= NSHVC_DOWN;
      if (b & 1u << 15) j |= NSHVC_LEFT;
      return j;
  }
  ```
  `pad_bit_of` and `pad_default_label` stay (they are about the SNES's buttons and the families' names); `player_port` and `gun_aim` stay.
- `input_frame_pads(now, on, game_buttons)` becomes `snes_input_frame(const host_pad *now, const int *on, const uint16_t *game_buttons)`: `nk_input_pads(now, on)` first, then `joy[k] = nk_input_pad(k) ? joypad_of(nk_pad_bits(nk_input_pad(k))) : 0`, `joy[0] |= joypad_of(nk_input_keyboard_bits())`, then today's port loop unchanged except: `mouse_x`/`mouse_y`/`mouse_dx`/`mouse_dy`/`mouse_buttons`/`mouse_seen` read from `nk_input_mouse_now()`, `touches`/`touch[g]` from `nk_input_touches`, `key_turbo` → `nk_input_key_held('T')`, `key_pause` → `nk_input_key_held('P')`, `key_start` → `nk_input_key_held(0x0D)`, `use(...)` → `nk_input_use(...)` with `NK_INPUT_*`; at its end `nk_input_mouse_frame_done()`. `input_frame(game_buttons)` becomes `snes_input_frame(NULL, NULL, game_buttons)`.
- The SNES's key handling for T and P, which today marks the keyboard as player 0's source: `int snes_input_key(int vk, int down) { if (down && (vk == 'T' || vk == 'P')) nk_input_use(0, NK_INPUT_KEYBOARD, NULL); return 0; }`, called by the console before the kit's `nk_input_key` (Task 9 wires it into `nk_console.key` after the game's hook).
- `nshvc_player_source` and `nshvc_player_label`: as today, `source[player]` → `*nk_input_source_of(player)` copied into `nshvc_input_source` with
  ```c
  _Static_assert(sizeof(nshvc_input_source) == sizeof(nk_input_source), "the same source");
  _Static_assert(offsetof(nshvc_input_source, name) == offsetof(nk_input_source, name), "the same source");
  _Static_assert(NSHVC_INPUT_SCRIPT == NK_INPUT_SCRIPT && NSHVC_PAD_NINTENDO == NK_PAD_NINTENDO, "the same kinds");
  ```
  `pads[player].label[bit]` → `nk_input_pad(player)->label[bit]` (with the `pad_on` test as `nk_input_pad(player) != NULL`); the keyboard's label: `for (i...) if (k_map[i].m == control)` becomes `nk_input_key_name(positional bit of control)` using `pad_bit_of(control)`: `{ int bit = pad_bit_of((uint16_t)control); return bit >= 0 ? nk_input_key_name(1u << bit) : ""; }` for player 0.
- `input_report(hook)` callers: `nk_input_report` takes `void (*)(int, const nk_input_source *)`; the SNES wraps the game's hook: `static void report(int p, const nk_input_source *s) { if (hooks_input) hooks_input(p, (const nshvc_input_source *)s); }` — with the static asserts above the cast is sound. Put `snes_input_report(void (*hook)(int, const nshvc_input_source *))` in `snes_input.c` doing that.
- `snes_input.h`: declare `snes_input_frame`, `snes_input_key`, `snes_input_report`, `console_picture_at` (as today).
- `consoles/snes/src/front/console.c`: `input_key` → `snes_input_key(vk, down); nk_input_key(vk, down);`, `input_mouse` → `nk_input_mouse`, `input_touch` → `nk_input_touch`, `input_focus_lost` → `nk_input_focus_lost`, `input_frame(game_buttons)` → `snes_input_frame(NULL, NULL, game_buttons)`, `input_script()` → `nk_input_script()`, `input_report(hooks.input)` → `snes_input_report(hooks.input)`, `input_point(...)` → `nk_input_point(...)`.
- `consoles/snes/tests/unit/input.c`: if it calls `input_frame_pads`, `input_key`, `input_mouse`, `input_touch` or `input_point`, rename the calls as above (`snes_input_frame`, `snes_input_key` + `nk_input_key`, ...), changing nothing it checks; add `${NK_INCLUDE}` to its include path in `consoles/snes/tests/CMakeLists.txt`.

In `kit/CMakeLists.txt`: `add_library(nk_input OBJECT ${NK_DIR}/src/front/input.c)`, added to the `foreach`; `kit_input_check` links `$<TARGET_OBJECTS:nk_input>`; `consoles/snes/CMakeLists.txt` adds `$<TARGET_OBJECTS:nk_input>`.

- [ ] **Step 3: Run the checks**

```bash
cmake --build build/null -j && build/null/kit/tests/kit_input_check && build/null/tests/input
cmake --build build/macos -j && build/macos/tests/pointer_macos
```
Expected: `input_check: ok`; the SNES's input check passes as before; `pointer_macos` passes (needs a window: if the session cannot show one, say so and ask the user to run it). Then the gate.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "nativekit: the players' native devices; the SNES's controllers read them

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 9: The application loop, `nk_main`

**Files:**
- Create: `kit/src/front/app.c`, `consoles/snes/src/front/snes_present.c`
- Modify: `consoles/snes/src/front/console.c` (it keeps the public `nshvc_*` calls, the overlays' API and `nshvc_main`; everything else moves), `consoles/snes/src/front/snes_console.c` (the rest of the table), `kit/include/nk/nk.h` (overlay calls), `kit/CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Consumes: every `nk_console` field; `nk_script_*`, `nk_input_*`, `nk_save_*`, `nk_env_*`, `rr_*` (`rr_open`, `rr_target`, `rr_begin`, `rr_image`, `rr_overlay`, `rr_present`, `rr_read`, `rr_name`, `rr_headless_size`, `rr_ref_image`), `resample_*`, `host_*`.
- Produces, added to `kit/include/nk/nk.h`:
  ```c
  #define NK_OVERLAYS 16
  int  nk_overlay_image(const uint32_t *argb, int w, int h);   /* -1 when full */
  void nk_overlay_place(int id, float cx, float cy, float scale, float angle_rad, float alpha, int visible);
  void nk_window_size(float *w, float *h);   /* points; a headless run's RR_SIZE */
  int  nk_headless(void);
  long nk_frame_no(void);
  /* where the last frame's picture was drawn on the target, and the target's width */
  void nk_shown_at(int d[4], int *tw);
  ```
  `NSHVC_OVERLAYS` in `nshvc.h` must equal `NK_OVERLAYS` (static assert in `console.c`).
- `nshvc_main(cart, argc, argv)` becomes `return nk_main(&snes_console, cart, argc, argv);` plus what is the SNES's own (Step 3).

- [ ] **Step 1: Write `app.c` from `console.c`**

Move these from `consoles/snes/src/front/console.c` into `kit/src/front/app.c`, verbatim except for the substitutions listed for each:
- `headless`, `max_frames`, `frame_no`, `sound_f`, `counts_f`, `counts`, `pending_kind`, `pending_n`, `count_read`, `count_write`, `counts_frame`: `NSHVC_KINDS` → `c->kind_count` (with `counts` sized `[2][16]` and a fatal `"a console with more than 16 kinds of access"` when `kind_count > 16`), `nshvc_access_kind` → `c->access_kind`. The `--counts` header comment keeps its format and says "the console's kinds of access".
- The overlays (`Overlay`, `overlays`, `noverlays`, `nshvc_overlay_image` → `nk_overlay_image`, `nshvc_overlay_place` → `nk_overlay_place`, `draw_overlays`): `NSHVC_OVERLAYS` → `NK_OVERLAYS`, `nshvc_window_size` → `nk_window_size`, `nshvc_fatal` → `nk_fatal`.
- `nshvc_window_size` → `nk_window_size` (the same body).
- The sound: `OUT_RATE`, `s_rs`, `audio_cb`, `sound_drain`: `NSHVC_DSP_RATE` → `c->audio_hz`, `snes_audio_take` → `c->audio_take`, `snes_audio_queued` → `c->audio_queued`, `script_replaying` → `nk_script_replaying`.
- The fps meter (`fps_*`, `fps_font`, `fps_draw`, `fps_frame`): `getenv("NSHVC_FPS")` → `nk_getenv("FPS")`, `"NSHVC_FPS_LOG"` → `nk_getenv("FPS_LOG")` and its message `"%s_FPS_LOG: cannot write %s", c->env, lf`.
- `shown_at`, `shown_tw` and `show`:
  ```c
  static void show(void)
  {
      int tw, th, w, h, d[4];
      double aspect;
      rr_target(&tw, &th);
      aspect = c->aspect();
      w = (int)(th * aspect + 0.5);
      h = th;
      if (w > tw) {
          w = tw;
          h = (int)(tw / aspect + 0.5);
      }
      d[0] = (tw - w) / 2;
      d[1] = (th - h) / 2;
      d[2] = d[0] + w;
      d[3] = d[1] + h;
      memcpy(shown_at, d, sizeof shown_at);
      shown_tw = tw;
      if (c->scaled)
          c->scaled(w, h);
      rr_begin(0xFF000000u);
      c->present(d);
      draw_overlays(tw, th);
      if (fps_on > 0)
          fps_draw();
      rr_present();
  }
  ```
  with `void nk_shown_at(int d[4], int *tw) { memcpy(d, shown_at, sizeof shown_at); *tw = shown_tw; }`.
- `rr_frames`, `rr_bad`, `rr_pixels`, `rr_first`, `rr_m7_frames` → `rr_frames`, `rr_bad`, `rr_pixels`, `rr_first`, `rr_own_frames`; `rr_report` prints `"rrcheck: %s, %ld frames (%ld with Mode 7 or a canvas presented), ..."` today — the kit cannot name Mode 7, so the kit prints `"rrcheck: %s, %ld frames (%ld %s), %ld differ (first at frame %ld), %ld pixels\n"` with the console's phrase from `nk_console.rrcheck_own` ("with Mode 7 or a canvas presented" for the SNES), so the SNES's line is byte-for-byte today's. `rr_check`:
  ```c
  static void rr_check(void)
  {
      static uint32_t *got, *want;
      int tw, th, pw, ph, i, d[4], own;
      const uint32_t *px = c->picture(&pw, &ph);
      long bad = 0;
      if (rr_frames < 0) {
          if (!rr_open()) {
              fprintf(stderr, "rrcheck: the renderer cannot open\n");
              exit(2);
          }
          atexit(rr_report);
          rr_frames = 0;
      }
      rr_target(&tw, &th);
      if (!got) {
          got = (uint32_t *)malloc((size_t)tw * (size_t)th * 4);
          want = (uint32_t *)malloc((size_t)tw * (size_t)th * 4);
      }
      d[0] = 0;
      d[1] = 0;
      d[2] = tw;
      d[3] = th;
      if (c->scaled)
          c->scaled(tw, th);
      rr_begin(0xFF000000u);
      for (i = 0; i < tw * th; i++)
          want[i] = 0xFF000000u;
      own = c->rrcheck && c->rrcheck(d, want, tw, th);
      if (own) {
          rr_own_frames++;
      } else {
          c->present(d);
          rr_ref_image(want, tw, th, px, pw, ph, d);
      }
      rr_present();
      rr_read(got);
      for (i = 0; i < tw * th; i++) {
          int sh, worst = 0;
          for (sh = 0; sh < 24; sh += 8) {
              int a = (int)((got[i] >> sh) & 0xFF) - (int)((want[i] >> sh) & 0xFF);
              if (a < 0)
                  a = -a;
              if (a > worst)
                  worst = a;
          }
          if (worst > 1) {
              if (rr_first < 0 && bad < 8 && nk_getenv("RRCHECK_WHY")) {
                  int X = i % tw, Y = i / tw;
                  fprintf(stderr, "rrcheck: frame %ld at (%d, %d): got %08x want %08x", frame_no, X, Y, got[i], want[i]);
                  if (own && c->rrcheck_why)
                      c->rrcheck_why(X, Y);
                  fprintf(stderr, "\n");
              }
              bad++;
          }
      }
      rr_frames++;
      if (bad) {
          rr_bad++;
          rr_pixels += bad;
          if (rr_first < 0)
              rr_first = frame_no;
      }
  }
  ```
  Compare with today's: the non-Mode 7 branch called `show_picture(d)`, which is what the SNES's `present` does for a frame with no Mode 7 or canvas (Step 2 keeps `present` as `show`'s choice: `show_mode7`, else `mode7_prepare` + `rr_mode7`, else `show_picture`; for a frame that is not Mode 7, both are `show_picture`). Today the Mode 7 branch is taken when `ppu_frame_mode7(ppu_frame()) && ph == ppu_frame()->screen_height`; the SNES's `rrcheck` takes it under the same test and returns 0 otherwise.
- `next_due`, `quit`, `frame`:
  ```c
  static void quit(void)
  {
      nk_save_write();
      host_shutdown();
      exit(0);
  }

  static void frame(void)
  {
      uint32_t b[2] = { 0, 0 };
      frame_no++;
      if (headless) {
          static int rc = -1;
          int more;
          if (counts_f)
              counts_frame(frame_no);
          if (rc < 0)
              rc = nk_getenv("RRCHECK") != NULL;
          if (rc)
              rr_check();
          if (sound_f)
              sound_drain();
          more = nk_script_frame(&b[0], &b[1]);
          c->input(1, b);
          if (c->frame)
              c->frame();
          if ((max_frames >= 0 && frame_no >= max_frames) || (!more && nk_script_steps() && max_frames < 0))
              exit(0);
          return;
      }
      /* the windowed half: today's, with these substitutions */
  }
  ```
  The windowed half is today's verbatim with: the fps settings as above; `HOST_EV_KEY`: `if (!(c->key && c->key(e.vk, e.down))) nk_input_key(e.vk, e.down);`; `HOST_EV_MOUSE` → `nk_input_mouse`; `HOST_EV_TOUCH` → `nk_input_touch(...)` then `if (c->touch) c->touch(e.id, e.phase, e.x, e.y);`; `input_focus_lost` → `nk_input_focus_lost`; `input_frame(game_buttons); input_report(hooks.input);` → `c->input(0, NULL);`; `save_write()` → `nk_save_write()`; `hooks.frame` → `c->frame`; `snes_pal() ? 19997194ull : 16639267ull` → `c->frame_ns()`.
  Today the headless half sets each joypad to `b0 | game_buttons[0]`, calls `input_script()` and `input_report(hooks.input)`: that becomes the SNES's `input(1, b)` (Step 2), in the same order.
- `shot_scale`: moves to the kit as `void nk_shot_scale(double *x, double *y)` (declared in `nk.h` since Task 6), today's body with `getenv("NSHVC_SCALE")` → `nk_getenv("SCALE")`; `nk_main` calls it where today's `nshvc_main` called `shot_scale`, and the SNES's `front_shot_picture` calls it.
- `nshvc_main` → `nk_main`:
  ```c
  static const nk_console *c;
  const nk_console *nk_console_now(void) { return c; }
  int nk_headless(void) { return headless; }
  long nk_frame_no(void) { return frame_no; }

  int nk_main(const nk_console *con, const void *cart, int argc, char **argv)
  {
      const char *script = NULL, *out_dir = ".", *dir, *title;
      int i, ww, wh;
      c = con;
      nk_env_prefix(c->env);
      nk_script_console(c);
      if (c->at_exit)
          atexit(c->at_exit);
      for (i = 1; i < argc; i++) {
          /* today's argument loop, its messages' "nativeshvc: " as "%s: ", c->name */
      }
      c->app_name(cart, &dir, &title);
      host_set_app_name(dir, title);
      host_init(argc, argv);
      nk_script_args(argc, argv);
      if (script)
          nk_script_load(script, out_dir);
      c->power(cart);
      if (headless && c->headless_scale) {
          double sx, sy;
          nk_shot_scale(&sx, &sy);
          if (sx > 0)
              c->headless_scale(sx, sy);
      }
      if (counts_f)
          c->counts_hooks(count_read, count_write);
      if (!headless && c->battery)
          nk_save_load(c->battery, host_save_dir());
      if (headless && script) {
          uint32_t b0, b1;
          nk_script_takes();
          nk_script_frame(&b0, &b1);
      }
      if (!headless) {
          host_thread_interactive();
          c->window_size(&ww, &wh);
          host_window_open(ww, wh, title);
          if (!rr_open()) {
              host_message_box("The renderer could not start on this computer.", title);
              return 1;
          }
          s_rs = resample_new(c->audio_hz, OUT_RATE);
          host_audio_open(OUT_RATE, audio_cb, NULL);
      }
      for (;;) {
          c->run_frame();
          frame();
      }
  }
  ```
  Check each line against today's `nshvc_main`: `atexit(ppucheck_report)` is now `c->at_exit`; `host_set_app_name(cart->save_name ? cart->save_name : cart->title, cart->title ? cart->title : "nativeshvc")` is `c->app_name`; `ppu_set_canvas_scale((int)ceil(sx > sy ? sx : sy))` is `c->headless_scale`; `nshvc_set_read_hook(count_read); nshvc_set_write_hook(count_write);` is `c->counts_hooks`; `save_load()` returned at once when headless, so `!headless &&` keeps it; `host_window_open(PPU_WIDTH, 224, snes_cart.title)` is `c->window_size` and `title` (`snes_cart.title` is the cartridge's title, which `app_name` gives: make the SNES's `app_name` return `cart->title` there and today's fallback for the folder only, so the window title is byte-for-byte `snes_cart.title`; if `snes_cart.title` can be NULL, give the SNES's `window_size`/`app_name` today's exact behaviour and say what it is in a comment); the message box's caption is `snes_cart.title` today: use the same `title`. `resample_new(NSHVC_DSP_RATE, OUT_RATE)`: `NSHVC_DSP_RATE`'s type and value must be what `c->audio_hz` gives (32040); `resample_new`'s parameter type decides whether `audio_hz` is an `int` or a `double` in `nk.h`: make it the same type as `resample_new`'s first parameter.

- [ ] **Step 2: The SNES's presentation and the rest of its table**

`consoles/snes/src/front/snes_present.c` takes from `console.c`, verbatim: `canvas_scale`, `show_picture`, `m7_cols`, `m7_rows`, `mode7_prepare`, `show_mode7`, `front_shot_picture` (calling `nk_shot_scale`), `console_picture_at` (with `shown_at`/`shown_tw` from `nk_shown_at`, `nshvc_window_size` from `nk_window_size`, `input_point` → `nk_input_point`), and adds:
```c
/* the picture onto d: Mode 7 or a canvas by the GPU's compositor, else by
   its record on the CPU's tables, else the picture (snes_console.present) */
void snes_present(const int d[4])
{
    RrMode7 m;
    RrM7Samples smp;
    int w = d[2] - d[0], h = d[3] - d[1];
    if (show_mode7(w, h, d))
        ;
    else if (mode7_prepare(w, h, &m, &smp))
        rr_mode7(&m, d, &smp);
    else
        show_picture(d);
}

/* the window's picture shape: the console's 256 x 224 at a television's
   4:3, a larger screen at the same shape of pixel */
double snes_aspect(void)
{
    const PpuFrame *f = ppu_frame();
    return 4.0 / 3.0 * ((double)f->width / PPU_WIDTH) * (224.0 / (double)f->screen_height);
}

void snes_scaled(int w, int h) { canvas_scale(w, h); }

/* NSHVC_RRCHECK on a frame with Mode 7 or a canvas: the compositor's
   records where the renderer has one, against rr_ref_mode7 */
static RrMode7 rc_m;
static RrM7Samples rc_smp;
static int rc_recorded;

int snes_rrcheck(const int d[4], uint32_t *want, int tw, int th)
{
    int pw, ph, gpu;
    ppu_picture(&pw, &ph);
    rc_recorded = 0;
    if (!(ppu_frame_mode7(ppu_frame()) && ph == ppu_frame()->screen_height))
        return 0;
    rc_recorded = 1;
    gpu = show_mode7(tw, th, d);
    if (!mode7_prepare(tw, th, &rc_m, &rc_smp))
        nshvc_fatal("rrcheck: no record for the frame");
    if (!gpu)
        rr_mode7(&rc_m, d, &rc_smp);
    rr_ref_mode7(want, tw, th, &rc_m, d, &rc_smp);
    return 1;
}

/* NSHVC_RRCHECK_WHY: the record of the picture pixel under (X, Y) */
void snes_rrcheck_why(int X, int Y)
{
    /* today's block from rr_check, verbatim: from
       "int tx = smp.cols[3 * X], ty = smp.rows[4 * Y], ..." to the end of
       its for loops, with m and smp as rc_m and rc_smp */
}
```
Fill `snes_rrcheck_why` by moving the `if (recorded) { ... }` body from today's `rr_check` exactly (it is code, not a placeholder: copy those lines, renaming `m`→`rc_m`, `smp`→`rc_smp`). Today's `rr_check` returned the Mode 7 branch's want from `rr_ref_mode7` with the target already set to `want = 0xFF000000` first: the kit fills `want` before calling `rrcheck`, so it is the same.

`snes_console.c`, the rest of the table:
```c
static void power(const void *cart) { nshvc_power((const nshvc_cart *)cart); }
static uint64_t frame_ns(void) { return snes_pal() ? 19997194ull : 16639267ull; }
static void app_name(const void *cart, const char **dir, const char **title)
{
    const nshvc_cart *k = (const nshvc_cart *)cart;
    *dir = k->save_name ? k->save_name : k->title;
    *title = k->title ? k->title : "nativeshvc";
}
static void window_size(int *w, int *h) { *w = PPU_WIDTH; *h = 224; }
static void headless_scale(double sx, double sy) { ppu_set_canvas_scale((int)ceil(sx > sy ? sx : sy)); }
static int battery(nk_battery *b)
{
    uint32_t n = snes_sram_size();
    if (!snes_cart.battery || !n || !snes_sram())
        return 0;
    b->ram = snes_sram();
    b->bytes = n;
    b->from = snes_cart.save_size ? snes_cart.save_from : 0;
    b->size = snes_cart.save_size;
    b->name = snes_cart.save_name;
    return 1;
}
static void counts_hooks(void (*rd)(uint32_t, uint8_t), void (*wr)(uint32_t, uint8_t))
{
    nshvc_set_read_hook(rd);
    nshvc_set_write_hook(wr);
}
```
plus `input`, `key`, `touch`, `frame`, `at_exit` from `console.c`'s state (`hooks`, `game_buttons`, `ppucheck_report`), which stay in `console.c` with these functions exported to the table:
```c
/* console.c */
void snes_input(int headless, const uint32_t b[2])
{
    if (headless) {
        nshvc_set_joypad(0, (uint16_t)(b[0] | game_buttons[0]));
        nshvc_set_joypad(1, (uint16_t)(b[1] | game_buttons[1]));
        nk_input_script();
    } else {
        snes_input_frame(NULL, NULL, game_buttons);
    }
    snes_input_report(hooks.input);
}
int snes_key(int vk, int down)
{
    if (hooks.key && hooks.key(vk, down))
        return 1;
    snes_input_key(vk, down);
    return 0;
}
void snes_touch(int id, int phase, float x, float y)
{
    if (hooks.touch) {
        nshvc_touch t;
        t.id = id;
        t.phase = phase;
        t.x = x;
        t.y = y;
        hooks.touch(&t, 1);
    }
}
void snes_frame_hook(void) { if (hooks.frame) hooks.frame(); }
```
Today the windowed half called `input_frame(game_buttons)` then `input_report(hooks.input)` — `snes_input(0, NULL)` does both in that order; the headless half set the joypads, `input_script()`, then `input_report(hooks.input)` — the same. Today's key event: `if (!(hooks.key && hooks.key(e.vk, e.down))) input_key(e.vk, e.down);` and `input_key` both marked T/P and set keys: `snes_key` marks T/P only when the game did not take the key, then the kit's `nk_input_key` runs: the same.

The table:
```c
const nk_console snes_console = {
    .name = "nativeshvc", .env = "NSHVC", .play_ext = ".sfx",
    .power = power, .run_frame = nshvc_run_frame, .frame_ns = frame_ns, .app_name = app_name,
    .window_size = window_size, .aspect = snes_aspect, .scaled = snes_scaled, .present = snes_present,
    .shot = shot, .rrcheck = snes_rrcheck, .rrcheck_why = snes_rrcheck_why,
    .rrcheck_own = "with Mode 7 or a canvas presented",
    .picture = nshvc_picture, .headless_scale = headless_scale,
    .audio_hz = NSHVC_DSP_RATE, .audio_take = nshvc_audio_take, .audio_queued = nshvc_audio_queued,
    .buttons = buttons, .button_count = sizeof buttons / sizeof buttons[0],
    .steps = steps, .step_count = 4, .script_step = script_step,
    .key = snes_key, .touch = snes_touch, .input = snes_input, .frame = snes_frame_hook,
    .regions = regions, .region_count = sizeof regions / sizeof regions[0],
    .battery = battery, .counts_hooks = counts_hooks, .access_kind = nshvc_access_kind,
    .kind_count = NSHVC_KINDS, .at_exit = snes_at_exit,
};
```
`snes_at_exit` is today's `ppucheck_report`, renamed and kept in `snes_present.c`. `nshvc_picture`'s signature is `const uint32_t *(int *, int *)`, matching `picture`; `nshvc_audio_take`, `nshvc_audio_queued`, `nshvc_access_kind` match their fields (check each signature in `nshvc.h`; if one differs, wrap it in a two-line static function rather than casting).

`console.c` keeps: the public `nshvc_*` calls at its top; `nshvc_fatal`; `hooks`, `nshvc_set_hooks`; `nshvc_save_dir`; `nshvc_safe_area`; `nshvc_window_size` → `{ nk_window_size(w, h); }`; `game_buttons`, `nshvc_game_buttons`; `nshvc_overlay_image` → `return nk_overlay_image(argb, w, h);`, `nshvc_overlay_place` → `nk_overlay_place(...)` with `_Static_assert(NSHVC_OVERLAYS == NK_OVERLAYS, "the same overlays");`; `snes_input`, `snes_key`, `snes_touch`, `snes_frame_hook`; and
```c
int nshvc_main(const nshvc_cart *cart, int argc, char **argv) { return nk_main(&snes_console, cart, argc, argv); }
```
Today `nshvc_main` printed `nativeshvc: cannot write %s` for `--counts` and `--sound` failures and returned 2: the kit's loop prints `"%s: cannot write %s\n", c->name, argv[i]` and returns 2: the same text.

CMake: `add_library(nk_app OBJECT ${NK_DIR}/src/front/app.c)` in `kit/CMakeLists.txt` (added to the `foreach`), `$<TARGET_OBJECTS:nk_app>` and `src/front/snes_present.c` added to `nshvc`. `app.c` needs `rr.h` and `rr_size.h`: they are still under `consoles/snes/src/render/` until Task 10, so for this task `nk_app` gets `${NSHVC_DIR}/src/render` on its include path **temporarily**, with a comment `# until kit/include/nk/rr.h (the renderer split)`; Task 10 removes it. The name check reads files, not include paths, so it still passes; `app.c` itself names no console.

- [ ] **Step 3: Run the checks**

```bash
cmake --build build/null -j && python3 consoles/snes/tests/golden.py compare build/golden
for r in metal gl vulkan; do [ -d build/null-$r ] && cmake --build build/null-$r -j && NSHVC_RRCHECK=1 RR_SIZE=1000x750 NSHVC_TESTBUILD=$PWD/build/null-$r/tests python3 consoles/snes/tests/rom/ext_check.py; done
build/macos/examples/hello &  sleep 5; kill %1
python3 consoles/snes/tests/golden.py time
```
Expected: the golden compare `0 of N runs differ`; each renderer's rrcheck line (in its runs' stderr) reports `0 differ`; hello opens a window showing blue (ask the user to confirm it if the session cannot see the screen); the time is within 3% of `build/golden-time.txt`. Then the gate.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "nativekit: the application loop; nshvc_main is nk_main with the SNES's table

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 10: The software renderer split

**Files:**
- Move: `consoles/snes/src/render/rr.h` → `kit/include/nk/rr.h`; `consoles/snes/src/render/rr_size.h` → `kit/include/nk/rr_size.h`; `consoles/snes/src/render/soft/rr_soft.c` → `kit/src/render/soft/rr_soft.c`
- Create: `consoles/snes/src/render/snes_rr.h`, `kit/include/nk/rr_soft_ext.h`, `consoles/snes/src/render/soft/snes_soft.c`
- Modify: `kit/include/nk/rr.h` (the four SNES calls leave), `kit/src/render/soft/rr_soft.c`, every file that called them (`#include "snes_rr.h"`), `kit/CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Produces (`kit/include/nk/rr.h`): `rr_open`, `rr_close`, `rr_name`, `rr_target`, `rr_begin`, `rr_image`, `rr_overlay`, `RR_OVERLAYS`, `rr_present`, `rr_read`, and a hook a console's GPU work registers at start-up:
  ```c
  /* a console's own GPU work, made ready when the backend opens (its
     shaders compiled): 0 when it cannot run, and the backend does not open */
  void rr_set_open_hook(int (*fn)(void));
  ```
- Produces (`consoles/snes/src/render/snes_rr.h`): `rr_compose`, `rr_compose_woven`, `rr_mode7`, `rr_compose_mode7` with today's declarations and comments (moved from `rr.h`).
- Produces (`kit/include/nk/rr_soft_ext.h`): `uint32_t *rr_soft_target(int *w, int *h);` (the target's pixels, for a console's own drawing).
- CMake: `nk_render` OBJECT library (the backend chosen by `NSHVC_RENDER`).

- [ ] **Step 1: Split**

```bash
mkdir -p kit/src/render/soft
git mv consoles/snes/src/render/rr.h kit/include/nk/rr.h
git mv consoles/snes/src/render/rr_size.h kit/include/nk/rr_size.h
git mv consoles/snes/src/render/soft/rr_soft.c kit/src/render/soft/rr_soft.c
```
In `kit/include/nk/rr.h`: move the declarations and comments of `rr_compose`, `rr_compose_woven`, `rr_mode7`, `rr_compose_mode7` (and the `struct RrMode7; struct RrM7Samples;` forward declarations) into a new `consoles/snes/src/render/snes_rr.h` (guard `NSHVC_SNES_RR_H`, `#include "rr.h"`); add `rr_set_open_hook`; reword the remaining comments that named `ppu.c` or `nshvc_overlay_place` ("For the game's controls on a touch screen (the console's overlays)"). In `rr_size.h`, nothing names a console (check: `grep -in "snes\|ppu" kit/include/nk/rr_size.h`); the 256 x 224 default stays as a number.

In `kit/src/render/soft/rr_soft.c`: remove `rr_compose`, `rr_compose_woven`, `rr_compose_mode7`, `rr_mode7` and `#include "rr_mode7.h"`; add
```c
static int (*s_open_hook)(void);
void rr_set_open_hook(int (*fn)(void)) { s_open_hook = fn; }
uint32_t *rr_soft_target(int *w, int *h)
{
    *w = s_w;
    *h = s_h;
    return s_px;
}
```
and at `rr_open`'s end: `return s_px != NULL && (!s_open_hook || s_open_hook());`.

`consoles/snes/src/render/soft/snes_soft.c`:
```c
/*
 * snes_soft.c: the SNES's picture on the software backend: no compositor
 * (the caller draws the picture), Mode 7 and the canvas by rr_mode7.h's
 * reference drawing onto the target.
 */
#include "snes_rr.h"
#include "rr_mode7.h"
#include "rr_soft_ext.h"

int rr_compose(const uint32_t *desc, size_t words, int w, int h, const int d[4])
{
    (void)desc;
    (void)words;
    (void)w;
    (void)h;
    (void)d;
    return 0;   /* no compositor: the caller draws the picture */
}

int rr_compose_woven(const uint32_t *desc, size_t words, int ww, int wh, uint32_t base0, uint32_t base1,
                     const int d[4])
{
    (void)desc;
    (void)words;
    (void)ww;
    (void)wh;
    (void)base0;
    (void)base1;
    (void)d;
    return 0;   /* no compositor */
}

int rr_compose_mode7(const uint32_t *desc, size_t words, const struct RrMode7 *m, const int d[4],
                     const struct RrM7Samples *s)
{
    (void)desc;
    (void)words;
    (void)m;
    (void)d;
    (void)s;
    return 0;   /* no compositor: the caller makes the records and picture */
}

void rr_mode7(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s)
{
    int w, h;
    uint32_t *px = rr_soft_target(&w, &h);
    rr_ref_mode7(px, w, h, m, d, s);
}
```
`kit/include/nk/rr_soft_ext.h`: the one declaration with a header comment ("the software backend's target, for a console's own drawing onto it").

Add `#include "snes_rr.h"` to every file under `consoles/snes/` that calls one of the four (`grep -rln "rr_compose\|rr_mode7(" consoles/snes/src consoles/snes/tests`). Remove the temporary `${NSHVC_DIR}/src/render` include from `nk_app` (Task 9).

CMake, `kit/CMakeLists.txt`:
```cmake
add_library(nk_render OBJECT)
if(NSHVC_RENDER STREQUAL "soft")
  target_sources(nk_render PRIVATE ${NK_DIR}/src/render/soft/rr_soft.c)
endif()
```
(Tasks 11–13 add the GPU backends here, each removing its lines from `consoles/snes/CMakeLists.txt`.) `consoles/snes/CMakeLists.txt`: in the `soft` branch, replace `src/render/soft/rr_soft.c` with `src/render/soft/snes_soft.c`; add `$<TARGET_OBJECTS:nk_render>`.

- [ ] **Step 2: Run the checks**

```bash
cmake --build build/null -j
python3 consoles/snes/tests/golden.py compare build/golden
NSHVC_RRCHECK=1 RR_SIZE=1067x800 build/null/tests/present_check --headless --frames 60 2>&1 | grep rrcheck
NSHVC_RRCHECK=1 RR_SIZE=1067x800 build/null/tests/canvas_check --headless --frames 120 2>&1 | grep rrcheck
NSHVC_RRCHECK=1 RR_SIZE=1067x800 build/null/tests/screen_check --headless --frames 20 2>&1 | grep rrcheck
RR_SIZE=1100x800 build/null/tests/overlay_check
```
Expected: golden `0 differ`; each rrcheck line `0 differ`; overlay_check passes. Then the gate.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "nativekit: generic presentation on the software backend; the SNES's Mode 7 drawing its own

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 11: The Metal backend split

**Files:**
- Move: `consoles/snes/src/render/metal/rr_metal.m` → `kit/src/render/metal/rr_metal.m`
- Create: `kit/include/nk/rr_metal_ext.h`, `consoles/snes/src/render/metal/snes_metal.m`
- Modify: `kit/src/render/metal/rr_metal.m`, `kit/CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Produces (`kit/include/nk/rr_metal_ext.h`, Objective-C):
  ```objc
  #import <Metal/Metal.h>
  id<MTLDevice> rr_metal_device(void);
  id<MTLCommandQueue> rr_metal_queue(void);
  int rr_metal_frame(void);                         /* this frame in flight's index, 0-2 */
  int rr_metal_encoding(void);                      /* 1 between rr_begin and rr_present */
  /* the target's render pass ended for a compute pass; rr_metal_compute_end
     ends it and goes on with the target's pass (its contents kept) */
  id<MTLComputeCommandEncoder> rr_metal_compute_begin(void);
  void rr_metal_compute_end(id<MTLComputeCommandEncoder> ce);
  id<MTLRenderCommandEncoder> rr_metal_encoder(void);
  /* a quad over target rectangle d in this frame's vertex buffer: its offset, -1 when full */
  long rr_metal_quad(const int d[4]);
  id<MTLBuffer> rr_metal_vertices(void);
  id<MTLTexture> rr_metal_picture_texture(int w, int h);   /* this frame's picture texture */
  void rr_metal_draw_picture(id<MTLTexture> t, int w, int h, const int d[4]);   /* one to one or sharp bilinear */
  /* a pipeline of the shared vertex function (bvs) and fragment function fs from lib */
  id<MTLRenderPipelineState> rr_metal_pipe(NSString *fs, id<MTLLibrary> lib, int blend);
  extern NSString *const rr_metal_vertex_src;     /* BO and bvs, for a console's library */
  void rr_metal_wait_idle(void);                    /* every frame in flight done */
  ```

- [ ] **Step 1: Split**

```bash
mkdir -p kit/src/render/metal && git mv consoles/snes/src/render/metal/rr_metal.m kit/src/render/metal/rr_metal.m
```
In `kit/src/render/metal/rr_metal.m` keep: the device, queue, offscreen target, `make_pipe_blend`/`make_pipe`, `targets`, `rr_open`, `rr_close`, `rr_name`, `rr_target`, `quad`, `rr_begin`, `pic_tex`, `rr_image`, `draw_pic`, `rr_overlay`, `rr_present`, `rr_read`, and in `k_src` the `BO` struct, `bvs`, `bfs`, `SU`, `sfs`, `ofs`. Move out to `snes_metal.m`: `#include "rr_compose_src.h"`, `k_compose_pre`, `k_compose_post`, the Mode 7 shader text in `k_src` (from the comment `/* a frame with Mode 7 presented (rr_mode7.h, the same arithmetic) */` through `m7fs`'s end), `s_m7`, `s_cp`, `s_cpr`, `s_cpbuf`, `s_recbuf`, `s_picbuf`, `s_m7buf`, `s_plane`, `s_plane_gen`, `s_planes`, `rr_compose`, `rr_compose_woven`, `compose`, `m7_draw`, `rr_mode7`, `rr_compose_mode7`, `table_buf`. Make `make_pipe_blend` the exported `rr_metal_pipe`; put `BO` and `bvs`'s two lines in `NSString *const rr_metal_vertex_src` and build `k_src` from it plus the generic fragment functions; implement the accessors over the existing statics (`rr_metal_compute_begin` is the four lines compose ran: `[s_enc endEncoding]; s_enc = nil; return [s_cmd computeCommandEncoder];`; `rr_metal_compute_end` is `[ce endEncoding]` then the `MTLRenderPassDescriptor` with `MTLLoadActionLoad` and `s_enc = [s_cmd renderCommandEncoderWithDescriptor:rp]`; `rr_metal_wait_idle` is the empty command buffer committed and waited on, as `m7_draw` does today). Add `rr_set_open_hook` and call the hook at `rr_open`'s end inside the autorelease pool, after `targets(w, h)`: `return !s_open_hook || s_open_hook();` (today `rr_open` returned 0 when the compositor did not compile: the SNES's hook keeps that).

In `consoles/snes/src/render/metal/snes_metal.m`: the moved code, with every `s_dev` → `rr_metal_device()`, `s_q` → `rr_metal_queue()`, `s_pi` → `rr_metal_frame()`, `s_enc` uses → `rr_metal_encoder()` (and the `!s_enc` tests → `!rr_metal_encoding()`), the end-and-reopen sequences → `rr_metal_compute_begin`/`rr_metal_compute_end`, `quad(d)` → `rr_metal_quad(d)`, `s_vb[s_pi]` → `rr_metal_vertices()`, `pic_tex` → `rr_metal_picture_texture`, `draw_pic` → `rr_metal_draw_picture`, `make_pipe(@"m7fs", lib)` → `rr_metal_pipe(@"m7fs", lib, 0)`. Its library source: `[NSString stringWithFormat:@"%@%s", rr_metal_vertex_src, k_m7_src]` where `k_m7_src` is the moved Mode 7 text with `#include <metal_stdlib>` and `using namespace metal;` before it (the generic library has them at its top; the SNES's needs its own). Its open hook compiles that library (`s_m7`) and the compositor (`s_cp`, `s_cpr`) as `rr_open` did, printing the same messages (`"rr: metal shader: %s\n"`, `"rr: metal compositor: %s\n"`), and returns 0 on failure; register it with `rr_set_open_hook` from `snes_console.c`'s `power`, or, simpler and earlier, from a constructor in `snes_metal.m`: `__attribute__((constructor)) static void snes_metal_init(void) { rr_set_open_hook(snes_metal_open); }`. The weak `host_metal_layer` definitions stay in the kit's file.

CMake: move the `metal` branch (`target_sources`, the `-fobjc-arc` property, the frameworks) to `nk_render` in `kit/CMakeLists.txt`; in `consoles/snes/CMakeLists.txt`'s `metal` branch, add `src/render/metal/snes_metal.m` with `-fobjc-arc` and keep the generated `rr_compose_src.h` include directory on `nshvc`.

- [ ] **Step 2: Run the checks**

```bash
cmake --build build/null-metal -j && cmake --build build/macos -j
NSHVC_TESTBUILD=$PWD/build/null-metal/tests python3 consoles/snes/tests/golden.py compare build/golden
for t in present_check canvas_check screen_check; do NSHVC_RRCHECK=1 RR_SIZE=1067x800 build/null-metal/tests/$t --headless --frames 60 2>&1 | grep rrcheck; done
RR_SIZE=1100x800 build/null-metal/tests/overlay_check
NSHVC_RRCHECK=1 RR_SIZE=1000x750 NSHVC_TESTBUILD=$PWD/build/null-metal/tests python3 consoles/snes/tests/rom/ext_check.py
build/macos/examples/hello & sleep 5; kill %1
```
Expected: every rrcheck line `0 differ` (the GPU's compositor drawing the 1,164 cartridges' frames is exercised by ext_check's runs with NSHVC_RRCHECK); overlay_check passes; hello shows its window. Run the rrcheck over all the test cartridges at two window sizes as `consoles/snes/tests/README.md`'s renderer row says (`NSHVC_RRCHECK=1 RR_SIZE=1000x750 build/null-metal/tests/testcart --rom ROM --headless --frames 4` for each ROM under `build/golden-work/rom/`, then at `RR_SIZE=2560x2240`), and the x86-64 build under Rosetta for one size. Then the gate.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "nativekit: generic presentation on Metal; the SNES's compositor and Mode 7 its own

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 12: The OpenGL backend split

**Files:**
- Move: `consoles/snes/src/render/gl/{rr_gl.c,gl_ctx.c,gl_api.h}` → `kit/src/render/gl/`; `consoles/snes/src/render/gl/rr_m7_glsl.h` stays with the SNES
- Create: `kit/include/nk/rr_gl_ext.h`, `consoles/snes/src/render/gl/snes_gl.c`
- Modify: `kit/src/render/gl/rr_gl.c`, `kit/CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Produces (`kit/include/nk/rr_gl_ext.h`):
  ```c
  #include "gl_api.h"
  GLuint rr_gl_program(const char *vs, const char *fs);   /* compiled and linked; 0 on failure (message printed) */
  extern const char *const rr_gl_quad_vs;                 /* the backend's vertex shader, for a console's programs */
  int  rr_gl_encoding(void);
  void rr_gl_draw(GLuint tex, int w, int h, const int d[4]);  /* a picture texture over d */
  GLuint rr_gl_picture_texture(int w, int h);
  void rr_gl_target_bind(void);                            /* the offscreen target as the draw framebuffer */
  void rr_gl_target_size(int *w, int *h);
  ```
  (Read `rr_gl.c` first and make the hook set exactly what `compose`, `rr_compose_mode7` and `m7_draw` use from the generic part: the program helper, the quad's vertex shader and buffer, the target framebuffer, the picture texture and `draw`. Name each accessor `rr_gl_<what>` and list them in the header with a one-line comment each.)

- [ ] **Step 1: Split**

As Task 11, for `rr_gl.c`: the kit keeps `shader`, `program` (exported as `rr_gl_program`), `targets`, `rr_open`, `rr_close`, `rr_name`, `rr_target`, `rr_begin`, `draw` (exported as `rr_gl_draw`), `rr_image`, `rr_overlay`, `rr_present`, `rr_read`, the copy/sharp/overlay programs; the SNES's `snes_gl.c` takes `compose`, `rr_compose`, `rr_compose_woven`, `rr_compose_mode7`, `rr_mode7`, `m7_draw`, the compositor's program (built from `k_compose_body`), the Mode 7 program (from `rr_m7_glsl.h`'s `k_m7`) and their buffers and textures, compiled in its open hook (constructor-registered, as Task 11). `gl_ctx.c` and `gl_api.h` name no console; move them whole. CMake: the `gl` branch's sources and libraries to `nk_render`; `consoles/snes/CMakeLists.txt` adds `src/render/gl/snes_gl.c` in its `gl` branch.

- [ ] **Step 2: Run the checks**

If `build/null-gl` built at Task 0:
```bash
cmake --build build/null-gl -j
for t in present_check canvas_check screen_check; do NSHVC_RRCHECK=1 RR_SIZE=1067x800 build/null-gl/tests/$t --headless --frames 60 2>&1 | grep rrcheck; done
RR_SIZE=1100x800 build/null-gl/tests/overlay_check
```
Expected: each `0 differ` — if the GL context opens headless on this Mac; if `rr_open` fails here as it did before this task (check Task 0's notes), the GL backend is compile-checked only: say so. The Windows cross-build (`build/win`, GL) must build with no warnings. Then the gate.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "nativekit: generic presentation on OpenGL; the SNES's compositor and Mode 7 its own

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 13: The Vulkan backend split

**Files:**
- Move: `consoles/snes/src/render/vulkan/rr_vulkan.c` → `kit/src/render/vulkan/rr_vulkan.c`; `consoles/snes/src/render/vulkan/shaders/{quad.vert,copy.frag,sharp.frag,over.frag}` → `kit/src/render/vulkan/shaders/`
- Create: `kit/include/nk/rr_vulkan_ext.h`, `kit/src/render/vulkan/rr_spv_kit.h`, `kit/src/render/vulkan/shaders/build.sh`, `consoles/snes/src/render/vulkan/snes_vulkan.c`, `consoles/snes/src/render/vulkan/rr_spv_snes.h`
- Modify: `consoles/snes/src/render/vulkan/shaders/build.sh` (its own shaders only), remove `consoles/snes/src/render/vulkan/rr_spv.h` (split into the two headers), `kit/CMakeLists.txt`, `consoles/snes/CMakeLists.txt`

**Interfaces:**
- Produces (`kit/include/nk/rr_vulkan_ext.h`): the device, physical device's memory properties, the frame's command buffer, the render pass's end and resumption around a compute dispatch, the target image and its view, the picture image, the staging ring (`ring_take`), `barrier`, `image`, `image_fmt`, `image_destroy`, `make_buffer`, `mem_type`, `vk_check`, the quad's draw, and the open hook — each as `rr_vk_<name>`, exactly the set `snes_vulkan.c`'s moved functions use (read `rr_vulkan.c` and list them as Task 12 does).
- `rr_spv_kit.h`: the arrays `k_quad_vert`, `k_copy_frag`, `k_sharp_frag`, `k_over_frag`, copied unchanged from today's `rr_spv.h`. `rr_spv_snes.h`: `k_m7_frag`, `k_compose_comp`, `k_compose_rec_comp`, unchanged.

- [ ] **Step 1: Split the SPIR-V without recompiling it**

```bash
cd consoles/snes/src/render/vulkan
python3 - <<'EOF'
import re
src = open("rr_spv.h").read()
blocks = re.findall(r"static const uint32_t (k_\w+)\[\] = \{.*?\};\n", src, re.S)
bodies = re.findall(r"(static const uint32_t k_\w+\[\] = \{.*?\};\n)", src, re.S)
by = dict(zip(blocks, bodies))
kit = ["k_quad_vert", "k_copy_frag", "k_sharp_frag", "k_over_frag"]
snes = ["k_m7_frag", "k_compose_comp", "k_compose_rec_comp"]
assert sorted(by) == sorted(kit + snes), sorted(by)
open("../../../../../kit/src/render/vulkan/rr_spv_kit.h", "w").write(
    "/* rr_spv_kit.h: SPIR-V of shaders/quad.vert, copy.frag, sharp.frag and over.frag,\n"
    " * written by shaders/build.sh: do not edit. */\n" + "".join(by[k] for k in kit))
open("rr_spv_snes.h", "w").write(
    "/* rr_spv_snes.h: SPIR-V of shaders/m7.frag, compose.comp and compose_rec.comp,\n"
    " * written by shaders/build.sh: do not edit. */\n" + "".join(by[k] for k in snes))
EOF
cd -
```
Then check that the words are the same: `cat kit/src/render/vulkan/rr_spv_kit.h consoles/snes/src/render/vulkan/rr_spv_snes.h | grep -o "0x[0-9a-f]*" | sha256sum` equals `grep -o "0x[0-9a-f]*" consoles/snes/src/render/vulkan/rr_spv.h | sha256sum` with the arrays in the same order (reorder the `cat` to the original's order if the sums differ only by order). `git rm consoles/snes/src/render/vulkan/rr_spv.h`.

- [ ] **Step 2: Split the shader builds**

`kit/src/render/vulkan/shaders/build.sh`: the four generic shaders' `glslangValidator` lines and the header written as `../rr_spv_kit.h`. `consoles/snes/src/render/vulkan/shaders/build.sh`: its three, written as `../rr_spv_snes.h`, plus the OpenGL Mode 7 header as today (`../../gl/rr_m7_glsl.h`). If `glslangValidator` is installed (Task 0 Step 3), run both and check the headers come out byte-identical to Step 1's: `git diff --exit-code kit/src/render/vulkan/rr_spv_kit.h consoles/snes/src/render/vulkan/rr_spv_snes.h consoles/snes/src/render/gl/rr_m7_glsl.h`. If the compiler's version gives other words than the checked-in ones, keep the checked-in words (they are what was tested) and say which version differs.

- [ ] **Step 3: Split `rr_vulkan.c`**

As Tasks 11–12: the kit keeps the instance, device, swapchain, target, picture image, ring, the copy/sharp/overlay pipelines and descriptor sets, `rr_open` (calling the open hook at its end), `rr_begin`, `rr_image`, `rr_overlay`, `rr_present`, `rr_read`; `snes_vulkan.c` takes `s_m7*`, `s_plane*`, `s_c*`, `s_cr*`, `s_cpr`, `s_cimg*`, the compositor and Mode 7 pipelines' creation (in its open hook), `compose`, `rr_compose`, `rr_compose_woven`, `rr_compose_mode7`, `rr_mode7`, `m7_draw`, including `rr_spv_snes.h`. The kit's file includes `rr_spv_kit.h`.

- [ ] **Step 4: Run the checks**

If `build/null-vulkan` built at Task 0 (with MoltenVK):
```bash
cmake --build build/null-vulkan -j
for t in present_check canvas_check screen_check; do NSHVC_RRCHECK=1 RR_SIZE=1067x800 build/null-vulkan/tests/$t --headless --frames 60 2>&1 | grep rrcheck; done
RR_SIZE=1100x800 build/null-vulkan/tests/overlay_check
```
Expected: each `0 differ`. Without Vulkan here, compile-check only and say so. Then the gate.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "nativekit: generic presentation on Vulkan; the SNES's compositor and Mode 7 its own

The SPIR-V is split by shader, its words unchanged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```

---

### Task 14: The kit's documents and the final verification

**Files:**
- Create: `kit/docs/kit.md`, `kit/docs/contract-discipline.md`
- Modify: `README.md` (root: a section at the top on the monorepo), `consoles/snes/README.md` (relative links), `consoles/snes/tests/README.md` (paths), `kit/tests/CMakeLists.txt` (a `kit-check` target running every kit check)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: A target that runs every kit check**

In `kit/tests/CMakeLists.txt`:
```cmake
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(kit-check
  COMMAND $<TARGET_FILE:kit_coro_check>
  COMMAND $<TARGET_FILE:kit_routines_check>
  COMMAND $<TARGET_FILE:kit_env_check>
  COMMAND $<TARGET_FILE:kit_save_check>
  COMMAND $<TARGET_FILE:kit_input_check>
  COMMAND ${Python3_EXECUTABLE} ${NK_DIR}/tests/states_check.py $<TARGET_FILE:kit_stub_console>
  COMMAND ${Python3_EXECUTABLE} ${NK_DIR}/tests/profile_lib_check.py
  DEPENDS kit_coro_check kit_routines_check kit_env_check kit_save_check kit_input_check kit_stub_console)
```
Run: `cmake --build build/null --target kit-check`
Expected: every check prints its `ok`.

- [ ] **Step 2: Write the documents**

`kit/docs/kit.md`: what nativekit is (the console-independent half of a native console), its parts and their headers (one paragraph each: the loop and `nk_console`, scripts and states, input devices, the save, presentation and the backend hooks, coroutines, routines, the profile fitter), how a console uses it (the SNES's `snes_console.c` as the worked example, with its table), the build (OBJECT libraries folded into the console's library; `NSHVC_HOST`, `NSHVC_RENDER`), the settings by prefix, the rule that the kit names no console and the check that holds it. `kit/docs/contract-discipline.md`: the versioning rules (major breaks, minor adds, patch fixes, as nativeshvc's `docs/contract.md` states them, generalised), nativekit's own version (0.1.0, internal until a second console uses it, 1.0 when the Mega Drive builds on it unchanged), and the standards every console in the repository keeps: behaviour exact against a pinned reference, a fatal error rather than a guess, extensions off until asked for, the same run on every host, and checks shown able to fail before they are relied on. Write both in the plain, exact voice of `consoles/snes/docs/contract.md`; neither names a console except as "a console" (the name check reads `kit/docs` too).

Root `README.md`: above its first line, a short section "This repository" saying it holds nativekit (`kit/`) and the consoles built on it, nativeshvc (`consoles/snes/`, whose README follows unchanged), with the build command; the rest of the root README stays nativeshvc's, its relative links rewritten to `consoles/snes/...` (`docs/` → `consoles/snes/docs/`, `include/` → `consoles/snes/include/`, `examples/` → `consoles/snes/examples/`, `src/` → `consoles/snes/src/` or `kit/src/` for what moved, `tests/` → `consoles/snes/tests/`). In `consoles/snes/README.md`, the links relative to its own folder work as they are except those to moved files (`src/front/script.h` → `../../kit/include/nk/script.h`, `src/front/input.c` → `src/front/snes_input.c`, `src/render/compose_body.h` stays). Check every link: `grep -on "](\([^)#]*\)" README.md consoles/snes/README.md consoles/snes/docs/*.md kit/docs/*.md` and test each path exists.

- [ ] **Step 3: The final verification**

```bash
for b in build/null build/macos build/null-metal build/null-gl build/null-vulkan build/x86 build/win; do [ -d $b ] && cmake --build $b -j; done
cmake --build build/ios -- -quiet CODE_SIGNING_ALLOWED=NO
cmake --build build/null --target kit-check
python3 consoles/snes/tests/golden.py compare build/golden
NSHVC_TESTBUILD=$PWD/build/x86/tests python3 consoles/snes/tests/golden.py compare build/golden
python3 consoles/snes/tests/golden.py time
python3 consoles/snes/tests/rom/run.py | tail -1
python3 consoles/snes/tests/rom/ext_check.py
python3 consoles/snes/tests/dsp/fuzz.py 1 2 3 4 && python3 consoles/snes/tests/dsp/keys.py
python3 consoles/snes/tests/tools/profile_check.py
python3 consoles/snes/tests/unit/states.py
git diff --stat $(git merge-base HEAD main) -- consoles/snes/include
```
Expected: every configuration that built at Task 0 builds with no warnings; `kit-check` all ok; golden `0 of N runs differ` (arm64 and x86-64); time within 3% of `build/golden-time.txt`; `run.py`'s last line `0 of 1164 differ; 56 more differ as KNOWN lists; 0 listed in KNOWN now pass`; every other check passes as at Task 0; the last command prints nothing (the contract's headers unchanged). Also run `build/macos/tests/pointer_macos` and hello in a window. Write the report: for each check, ran and passed, or compile-checked only, or could not run here and why.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "nativekit: its documents, a kit-check target, the README for the monorepo

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT"
```
