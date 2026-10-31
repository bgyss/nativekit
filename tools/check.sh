#!/bin/sh
# The full check set Task 0 proved: unit, DSP, tools, host and renderer checks.
# Needs the null, macos and null-{metal,gl,vulkan} builds (mise run build) and,
# for the ROM checks, NSHVC_ASAR and NSHVC_ORACLE_CORE (mise run tools).
# Prints each check's last lines and exit code; exits non-zero if an
# expected-pass check failed or an expected-fatal check did not exit 70.
cd "$(dirname "$0")/.." || exit 1
unset LIBRARY_PATH
: "${NSHVC_ASAR:=$PWD/build/toolchains/asar/asar}"
: "${NSHVC_ORACLE_CORE:=$PWD/build/oracle-core/bsnes_libretro.dylib}"
export NSHVC_ASAR NSHVC_ORACLE_CORE
fails=0
mkdir -p build
log=build/check.last
r() { echo "### $*"; "$@" >"$log" 2>&1; rc=$?; tail -3 "$log" | cut -c1-240; echo "[exit $rc]"; [ $rc -eq 0 ] || fails=$((fails+1)); }
x() { echo "### $* (expect exit 70)"; "$@" >"$log" 2>&1; rc=$?; tail -3 "$log" | cut -c1-240; echo "[exit $rc]"; [ $rc -eq 70 ] || fails=$((fails+1)); }
r python3 tests/rom/ext_check.py
r python3 tests/dsp/fuzz.py 1 2 3 4
r python3 tests/dsp/keys.py
r build/null/tests/resample_check
r python3 tests/tools/profile_check.py
r build/null/tests/input
r python3 tests/unit/states.py
# pointer_macos moves a real window's pointer and fails now and then (about 1 run in 4,
# with or without nix) when the window manager's timing slips: retried up to 3 times.
if [ -x build/macos/tests/pointer_macos ]; then
  n=0
  while :; do
    n=$((n+1)); f=$fails
    r build/macos/tests/pointer_macos
    [ "$fails" -eq "$f" ] && break
    [ $n -ge 3 ] && break
    fails=$f
  done
fi
r build/null/tests/vram_direct
x build/null/tests/vram_direct --copy0
x build/null/tests/vram_direct --direct0
r build/null/tests/waits
r build/null/tests/waits --allowance
x build/null/tests/waits --offpoll
x build/null/tests/waits --held
x build/null/tests/waits --heldpoll
r build/null/tests/ipl
r build/null/tests/ipl --driver
r build/null/tests/ipl --profile --sweep
r build/null/tests/ipl --cpu-profile
r build/null/tests/chip
r build/null/tests/chip_irq
r build/null/tests/canvas_check
r build/null/tests/screen_check
r build/null/tests/present_check
for g in metal gl vulkan; do
  if [ -x build/null-$g/tests/overlay_check ]; then RR_SIZE=1100x800 r build/null-$g/tests/overlay_check; else echo "### overlay_check $g: not built"; fi
done
echo "$fails check(s) failed"
[ "$fails" -eq 0 ]
