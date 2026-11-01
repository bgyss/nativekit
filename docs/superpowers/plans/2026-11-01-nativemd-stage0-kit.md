# nativemd Stage 0: Kit Changes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make nativekit ready for a second console (nativemd) without changing one byte of the SNES's output.

**Architecture:** Eight changes to `kit/` and the top-level build, each moving an SNES assumption out of the kit and into the console's `nk_console` table or a console-neutral option. The SNES supplies today's values, so its golden master and reference comparison stay identical.

**Tech Stack:** C11, Objective-C, CMake ≥ 3.16, Python 3, mise, nix.

**Spec:** `docs/superpowers/specs/2026-11-01-nativemd-design.md` (Section 2: kit stage 0).

## Global Constraints

- The SNES is byte-for-byte unchanged: `mise run golden:compare` prints `golden: 0 of 2393 runs differ` after every task.
- `mise run test:rom` prints `0 of 1164 differ; 56 more differ as KNOWN lists; 0 listed in KNOWN now pass` at the end (Task 9).
- Headless CPU time (`python3 consoles/snes/tests/golden.py time`, children CPU, min of 3) ≤ 23.07 s.
- The SNES game contract (`consoles/snes/include/nshvc/*.h`) is unchanged byte for byte.
- `kit/` names no console (the configure-time name check, `kit/tests/kit_names.cmake`).
- The kit never depends on a console.
- CMake options `NSHVC_HOST`, `NSHVC_RENDER`, `NSHVC_TESTS`, `NSHVC_EXAMPLES` keep working; a game doing `add_subdirectory(<repo root>)` and linking `nshvc` keeps building (the consumer test).
- Code style: match the surrounding code (C11, 4-space indent, plain-voice `/* */` comments, `static` file-local state).
- Run everything inside `nix develop` (or `nix develop -c ...`); use the mise tasks.
- Every commit ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01ECRkuVyG1Q4qk8GheaShpT
  ```

## The gate (every task)

1. `nix develop -c mise run build` — all 9 builds (null, macos, null-metal, null-gl, null-vulkan, x86, ios, win, none) exit 0 with 0 warnings in their logs.
2. `nix develop -c mise run test:consumer` — exit 0.
3. `nix develop -c mise run golden:compare` — `golden: 0 of 2393 runs differ`.
4. `nix develop -c mise run test` — `0 check(s) failed`; `nix develop -c mise run test:kit` — ok.
5. Tasks 4–6 and 9: `nix develop -c mise exec -- python3 consoles/snes/tests/golden.py time` ≤ 23.07 s CPU.

## Review Focus

1. **A script with a bad player prefix fails as before.** `1.START`, `3.START` and `9.B` on the SNES (2 players) must still fail to parse with the same message as today; only `2.` is accepted. Pinned by a new case in `kit/tests/states_check.py` run against the stub console with `players = 2` (Task 5), and by the stub's 4-player run accepting `4.` and rejecting `5.`.
2. **A headless run with no console size set fails clearly, not at 0x0.** A program that calls `rr_open()` without `rr_set_headless_size` and without `RR_SIZE` must stop with `nk_fatal("no headless size ...")`, not open a 0-pixel target. Pinned in Task 4 by `kit/tests/render_check.c`.
3. **Keys the console's map doesn't list do nothing.** A key absent from the console's table sets no bit and does not make the keyboard player 0's source. Pinned by `kit/tests/input_check.c` (Task 6).
4. **The battery save keeps its timing.** Windowed: written at frame 120, 240 … and at quit, via `.new` renamed over the old; headless: never. Pinned by `kit/tests/loop_check.c` (Task 8).
5. **Old build commands still work.** `cmake -S . -B b -DNSHVC_HOST=macos -DNSHVC_RENDER=metal` configures the same targets as `-DNK_HOST=macos -DNK_RENDER=metal`. Pinned by `tools/check.sh` gaining a configure-only comparison of the two (Task 3).

---

## File map

```
CMakeLists.txt                      NK_* options, NSHVC_* aliases, NK_CONSOLES
kit/CMakeLists.txt                  NK_INCLUDE = kit/include only; private include of kit/include/nk; nk_render common source
kit/include/nk/gl_api.h             moved from kit/src/render/gl/gl_api.h
kit/include/nk/nk.h                 players, keys/key_count, kind_names, NK_KINDS_MAX, nk_quit; input hook takes b[NK_PLAYERS]
kit/include/nk/input.h              nk_key, nk_input_keymap
kit/include/nk/rr.h                 rr_set_headless_size
kit/include/nk/rr_size.h            rr_headless_size reads the size the console set
kit/include/nk/script.h             nk_script_frame(uint32_t b[NK_PLAYERS])
kit/src/render/rr_common.c          the headless size's storage (new)
kit/src/front/{app.c,input.c,script.c}
kit/tests/{kit_names.cmake,input_check.c,render_check.c,loop_check.c,stub_console.c,states_check.py,CMakeLists.txt}
consoles/snes/src/console/bus.c     one vfatal
consoles/snes/src/front/{console.c,snes_console.c,snes_console.h,snes_input.c}
consoles/snes/** (19 files)         #include "nk/<header>.h"
consoles/snes/tests/unit/overlay_check.c   rr_set_headless_size(256, 224)
mise.toml, tools/check.sh           NK_* flags; the alias check
kit/docs/kit.md, README.md          the new fields and options
```

---

### Task 1: One fatal function in the SNES; the kit's name check learns the Mega Drive

**Files:**
- Modify: `consoles/snes/src/console/bus.c:154-175`, `consoles/snes/src/front/console.c:60-69`
- Modify: `kit/tests/kit_names.cmake` (the banned list)

**Interfaces:**
- Produces: `void snes_vfatal(const char *fmt, va_list ap)` in `bus.c`, declared in the header that declares `snes_fatal` today (find it with `grep -rn "snes_fatal" consoles/snes/src --include=*.h`). `snes_fatal`, `nk_fatal` and `nshvc_fatal` each become a three-line wrapper.

- [ ] **Step 1: Confirm the gate is green before any change**

Run the gate (items 1–4). Expected: all pass. If not, stop and report.

- [ ] **Step 2: Write the shared function and the wrappers**

In `bus.c`, replace the bodies of `snes_fatal` and `nk_fatal`:
```c
/* every fatal error: "nativeshvc: " and the message, then exit 70 */
void snes_vfatal(const char *fmt, va_list ap)
{
    fprintf(stderr, "nativeshvc: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(70);
}

void snes_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    snes_vfatal(fmt, ap);
}

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    snes_vfatal(fmt, ap);
}
```
In `console.c`, `nshvc_fatal` becomes the same two-line wrapper over `snes_vfatal`. Declare `snes_vfatal` beside `snes_fatal`'s declaration, with `#include <stdarg.h>` there if it isn't already. Keep any `noreturn` attribute the existing declarations carry, on all three wrappers and on `snes_vfatal`.

- [ ] **Step 3: Add the Mega Drive's names to the name check**

In `kit/tests/kit_names.cmake`, extend the banned list with `nativemd`, `nmd_`, `mega drive`, `megadrive`, `genesis`, `ym2612`, `sn76489`, `68000` and `blastem`. Use `nmd_` rather than a bare `nmd`, so words that merely contain the letters can't match; check `grep -rniE 'nmd' kit` first, and if a bare `nmd` matches nothing in `kit/`, use `nmd` instead. Then plant one: `echo '/* genesis */' > kit/src/planted.c`, configure `build/null`, and expect the configure to fail naming `kit/src/planted.c`. Remove the file (`git rm` is unnecessary: it was never added; move it to the scratchpad, since `rm -rf` is blocked) and reconfigure. Expected: passes.

- [ ] **Step 4: Run the gate**

Expected: all pass, and the fatal-path runs in the golden master (`waits --held` and the others) still print `nativeshvc: ...` and exit 70.

- [ ] **Step 5: Commit**

```bash
git add consoles/snes/src kit/tests/kit_names.cmake
git commit -m "nativekit: one fatal function in the SNES; the name check knows the Mega Drive"
```

---

### Task 2: Consoles include kit headers as `nk/...`

**Files:**
- Move: `kit/src/render/gl/gl_api.h` → `kit/include/nk/gl_api.h` (`git mv`)
- Modify: `kit/CMakeLists.txt` (NK_INCLUDE, the kit targets' private include, the GL branch's public include)
- Modify: every file under `consoles/` that includes a kit header unprefixed (19 files; list them with
  `grep -rlE '#\s*(include|import) "(nk|fatal|env|coro|jump|host|resample|routines|script|input|save|rr|rr_size|rr_soft_ext|rr_metal_ext|rr_gl_ext|rr_vulkan_ext|gl_api)\.h"' consoles`)
- Modify: `consoles/snes/CMakeLists.txt`, `consoles/snes/tests/CMakeLists.txt` (they use `${NK_INCLUDE}` as before; nothing else needs `kit/include/nk`)

**Interfaces:**
- Produces: `NK_INCLUDE` is `${NK_DIR}/include` alone. Kit headers are reached as `"nk/<name>.h"`. Kit headers including each other keep their quoted same-folder form (`#include "save.h"` in `nk.h`), which resolves beside the including file.
- Produces: `NK_PRIVATE_INCLUDE` (`${NK_DIR}/include/nk`), used only by kit targets and kit tests.

- [ ] **Step 1: Make the failing build**

In `kit/CMakeLists.txt`:
```cmake
set(NK_INCLUDE ${NK_DIR}/include)
set(NK_INCLUDE ${NK_INCLUDE} PARENT_SCOPE)
set(NK_PRIVATE_INCLUDE ${NK_DIR}/include/nk)
```
Change the loop that gives kit targets `${NK_INCLUDE}` (around line 121) to `PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE}`. In `kit/tests/CMakeLists.txt`, give every target `${NK_INCLUDE} ${NK_PRIVATE_INCLUDE}`. Remove the GL branch's `target_include_directories(nk_render PUBLIC ${NK_DIR}/src/render/gl)` and its comment line. Then `git mv kit/src/render/gl/gl_api.h kit/include/nk/gl_api.h`.

Run `nix develop -c mise run build:null`. Expected: FAIL, with SNES sources not finding `host.h`, `rr.h` and so on. This is the RED step: it shows the flat path is gone.

- [ ] **Step 2: Prefix the console's includes**

For each file the grep lists, change `#include "X.h"` (or `#import`) to `#include "nk/X.h"`, for exactly the 18 kit header names in the grep. Do not touch includes of the console's own headers, such as `snes_rr.h`, `snes_input.h`, `nshvc/...` and `ppu.h`. Run the grep again. Expected: no output.

In the kit, `rr_gl.c`, `gl_ctx.c` and `rr_gl_ext.h` include `gl_api.h`; with the private include they still find it. Check with `grep -rn gl_api.h kit`.

- [ ] **Step 3: Run the gate**

Expected: all pass, all 9 builds included. The GL and win builds are the ones that exercise `gl_api.h`.

- [ ] **Step 4: Commit**

```bash
git add -A kit consoles
git commit -m "nativekit: consoles include the kit's headers as nk/...; gl_api.h with them"
```

---

### Task 3: Console-neutral build options

**Files:**
- Modify: `CMakeLists.txt` (top level)
- Modify: `mise.toml` (the build tasks use `-DNK_*`)
- Modify: `tools/check.sh` (the alias check)
- Modify: `kit/CMakeLists.txt` header comment

**Interfaces:**
- Produces:
  - Cache options `NK_HOST`, `NK_RENDER`, `NK_TESTS` and `NK_EXAMPLES`, plus `NK_CONSOLES`, a list defaulting to `snes`. nativemd's stage 1 adds `md`.
  - `NSHVC_HOST`, `NSHVC_RENDER`, `NSHVC_TESTS` and `NSHVC_EXAMPLES` are still read. When one is given, it sets the matching `NK_*`, so old commands work.
  - The SNES's own CMake still sees `NSHVC_*` set, from `NK_*`.

- [ ] **Step 1: Write the alias check, failing**

Add to `tools/check.sh` (near the other checks, using its `r` helper or the same pass/fail counting):
```sh
# the old option names configure the same as the kit's
alias_check() {
    a=$(mktemp -d) b=$(mktemp -d)
    cmake -S . -B "$a" -DNSHVC_HOST=null -DNSHVC_RENDER=soft -DNSHVC_TESTS=ON >/dev/null &&
    cmake -S . -B "$b" -DNK_HOST=null -DNK_RENDER=soft -DNK_TESTS=ON >/dev/null || return 1
    for v in NK_HOST NK_RENDER NK_TESTS; do
        [ "$(grep "^$v:" "$a/CMakeCache.txt")" = "$(grep "^$v:" "$b/CMakeCache.txt")" ] || return 1
    done
    cmake --build "$a" --target help | sort > "$a/targets"
    cmake --build "$b" --target help | sort > "$b/targets"
    cmp -s "$a/targets" "$b/targets"
}
```
Call it like the other checks. Run `sh tools/check.sh` (or only this check). Expected: FAIL, because `NK_HOST` is not a cache option yet and the `-DNK_*` configure gives a different cache.

- [ ] **Step 2: The top-level options**

Replace the option block in `CMakeLists.txt` (from `set(NSHVC_HOST ...` to `add_subdirectory(consoles/snes)`) with:
```cmake
# the options: NK_HOST (null, macos, windows, ios, none), NK_RENDER (soft,
# metal, gl, vulkan), NK_TESTS, NK_EXAMPLES and NK_CONSOLES (the consoles
# built, each in consoles/<name>). nativeshvc's own names (NSHVC_HOST,
# NSHVC_RENDER, NSHVC_TESTS, NSHVC_EXAMPLES) still set them, for its games.
foreach(_o HOST RENDER TESTS EXAMPLES)
  if(DEFINED NSHVC_${_o} AND NOT DEFINED NK_${_o})
    set(NK_${_o} "${NSHVC_${_o}}" CACHE STRING "" FORCE)
  endif()
endforeach()
set(NK_HOST "null" CACHE STRING "the host: null, macos, windows, ios, none")
if(APPLE AND NOT NK_HOST STREQUAL "null")
  set(_render_default metal)
elseif(WIN32)
  set(_render_default gl)
else()
  set(_render_default soft)
endif()
set(NK_RENDER "${_render_default}" CACHE STRING "the renderer: soft, metal, gl, vulkan")
option(NK_TESTS "build the tests" OFF)
option(NK_EXAMPLES "build the examples" OFF)
set(NK_CONSOLES "snes" CACHE STRING "the consoles to build (consoles/<name>), ';' between them")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# nativeshvc reads its own names
set(NSHVC_HOST ${NK_HOST})
set(NSHVC_RENDER ${NK_RENDER})
set(NSHVC_TESTS ${NK_TESTS})
set(NSHVC_EXAMPLES ${NK_EXAMPLES})
add_subdirectory(kit)
foreach(_c ${NK_CONSOLES})
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/consoles/${_c}/CMakeLists.txt)
    message(FATAL_ERROR "nativekit: no console ${_c} (consoles/${_c})")
  endif()
  add_subdirectory(consoles/${_c})
endforeach()
```
Look at how `consoles/snes/CMakeLists.txt` reads `NSHVC_EXAMPLES` and `NSHVC_TESTS`. If they are `option()`s there, the normal variables set above shadow them correctly under policy CMP0077 (`cmake_minimum_required(3.16)` sets it NEW). Confirm with the alias check.

Also update the header comment of `CMakeLists.txt` and the comment in `kit/CMakeLists.txt`: the options are `NK_*`, with `NSHVC_*` as aliases.

- [ ] **Step 3: mise uses the kit's names**

In `mise.toml`, change each build task's `-DNSHVC_HOST`/`-DNSHVC_RENDER`/`-DNSHVC_TESTS`/`-DNSHVC_EXAMPLES` to `-DNK_*`. The existing `build/<config>` caches hold `NSHVC_*` entries, which still work as aliases, so nothing else changes. Run `nix develop -c mise run build`. Expected: all 9 pass.

- [ ] **Step 4: Run the gate**

The alias check now passes, and the consumer test still passes. That test runs `add_subdirectory(<root>)` with no options, so it gets the defaults.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt kit/CMakeLists.txt mise.toml tools/check.sh
git commit -m "nativekit: console-neutral build options; nativeshvc's names as aliases"
```

---

### Task 4: The headless size comes from the console

**Files:**
- Create: `kit/src/render/rr_common.c`
- Create: `kit/tests/render_check.c`
- Modify: `kit/include/nk/rr.h`, `kit/include/nk/rr_size.h:1-24`
- Modify: `kit/CMakeLists.txt` (`rr_common.c` in every `nk_render` branch; render_check)
- Modify: `kit/src/front/app.c` (it sets the size from `c->window_size` before `rr_open`, and in `nk_window_size`)
- Modify: `consoles/snes/tests/unit/overlay_check.c` (it sets 256×224 itself)
- Modify: `kit/tests/CMakeLists.txt`, `tools/check.sh` or the kit-check target (it runs render_check)

**Interfaces:**
- Produces, in `rr.h`:
  ```c
  /* the target's size in a run with no window, before rr_open: the
     console's picture (RR_SIZE=WxH overrides it) */
  void rr_set_headless_size(int w, int h);
  ```
- Produces: `rr_headless_size(int *w, int *h)` keeps its signature. It returns `RR_SIZE` if set, else the size set, else `nk_fatal("no headless size: rr_set_headless_size before rr_open, or RR_SIZE=WxH")`.

- [ ] **Step 1: Write the failing test**

`kit/tests/render_check.c`:
```c
/* render_check: the headless target's size. RR_SIZE first, else the size
   the console set; neither: a fatal error, never a 0 x 0 target. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/fatal.h"
#include "nk/rr.h"
#include "nk/rr_size.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

int main(int argc, char **argv)
{
    int w = 0, h = 0;
    if (argc > 1 && !strcmp(argv[1], "--unset")) {
        rr_headless_size(&w, &h);           /* fatal */
        return 0;
    }
    rr_set_headless_size(320, 240);
    rr_headless_size(&w, &h);
    if (getenv("RR_SIZE") ? !(w == 1100 && h == 800) : !(w == 320 && h == 240)) {
        fprintf(stderr, "render_check: %d x %d\n", w, h);
        return 1;
    }
    printf("render_check: ok\n");
    return 0;
}
```
In `kit/tests/CMakeLists.txt`:
```cmake
add_executable(kit_render_check render_check.c ${NK_DIR}/src/render/rr_common.c)
target_include_directories(kit_render_check PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE})
set_target_properties(kit_render_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
```
Add three runs to `kit-check`:
- `$<TARGET_FILE:kit_render_check>`
- `${CMAKE_COMMAND} -E env RR_SIZE=1100x800 $<TARGET_FILE:kit_render_check>`
- the `--unset` run through `expect_fatal.cmake`, the way `kit_save_check --past` is run.

Add `kit_render_check` to `DEPENDS`. Run `nix develop -c mise run test:kit`. Expected: FAIL to build, because `rr_common.c` and `rr_set_headless_size` don't exist yet.

- [ ] **Step 2: Implement**

`kit/src/render/rr_common.c`:
```c
/* rr_common.c: what every backend shares that needs a home: the headless
   target's size the console set (rr_size.h). */
#include "fatal.h"
#include "rr.h"
#include "rr_size.h"

static int s_w, s_h;

void rr_set_headless_size(int w, int h)
{
    s_w = w;
    s_h = h;
}

void rr_headless_default(int *w, int *h)
{
    if (s_w < 1 || s_h < 1)
        nk_fatal("no headless size: rr_set_headless_size before rr_open, or RR_SIZE=WxH");
    *w = s_w;
    *h = s_h;
}
```
In `rr_size.h`:
- change the header comment's "(RR_SIZE=WxH, else 256 x 224)" to "(RR_SIZE=WxH, else the size the console set: rr_set_headless_size)";
- declare `void rr_headless_default(int *w, int *h);` above `rr_headless_size`;
- replace `*w = 256; *h = 224;` with `rr_headless_default(w, h);`, moved after the `RR_SIZE` test so that `RR_SIZE` alone needs no size set:
```c
static inline void rr_headless_size(int *w, int *h)
{
    const char *e = getenv("RR_SIZE");
    int a, b;
    if (e && sscanf(e, "%dx%d", &a, &b) == 2 && a > 0 && b > 0 && a <= 16384 && b <= 16384) {
        *w = a;
        *h = b;
        return;
    }
    rr_headless_default(w, h);
}
```
`rr_size.h` is included by SNES test programs that never open a backend: canvas_check, screen_check and present_check use only its sharp-bilinear functions. They link `nshvc`, which carries `nk_render` and so `rr_common.c`, so the declaration resolves. Confirm with the build.

In `kit/CMakeLists.txt`, add `${NK_DIR}/src/render/rr_common.c` to `nk_render` in all four branches. In `app.c`, call `int cw, ch; c->window_size(&cw, &ch); rr_set_headless_size(cw, ch);` once in `nk_main`, after `c` is set and power has run, before the first `rr_open` (both call sites, lines ~387 and ~612) and before any `nk_window_size` call. Then add `rr_set_headless_size(256, 224);` to `consoles/snes/tests/unit/overlay_check.c` before its `rr_open()`. This is SNES-side, so it may name the SNES size.

- [ ] **Step 3: Run the tests**

Run `nix develop -c mise run test:kit`. Expected: render_check prints ok in both size runs, and the `--unset` run exits 70 with the message. Then run the gate, perf included. overlay_check and overlay_check_big must be unchanged in the golden master.

- [ ] **Step 4: Commit**

```bash
git add kit consoles/snes/tests/unit/overlay_check.c
git commit -m "nativekit: the headless target's size is the console's"
```

---

### Task 5: More than two players in scripts

**Files:**
- Modify: `kit/include/nk/nk.h` (`players`; the `input` hook's `b`)
- Modify: `kit/include/nk/script.h` (`nk_script_frame`), `kit/src/front/script.c` (Step's buttons, `add_step`, `parse_buttons`, `nk_script_frame`)
- Modify: `kit/src/front/app.c:464-478, 606` (the buttons array)
- Modify: `consoles/snes/src/front/snes_console.c` (`.players = 2`), `consoles/snes/src/front/console.c:105` and `snes_console.h:26` (`snes_input` signature)
- Modify: `kit/tests/stub_console.c` (`.players` from `STUB_PLAYERS`, default 2), `kit/tests/states_check.py` (the prefix cases)

**Interfaces:**
- Produces, in `nk.h`:
  - `int players;`, inside `/* the controllers */`: the players a script drives, from 1 to `NK_PLAYERS`. Put it just before `buttons`.
  - The `input` hook becomes `void (*input)(int headless, const uint32_t *b);`, where `b` holds `players` entries.
- Move `#define NK_PLAYERS 8` from `input.h` into `nk.h`, and have `input.h` include `nk.h`. `nk.h` includes `save.h` only, so there is no cycle. `nk.h` must never include `input.h`; Task 6 keeps this rule too.
- Produces, in `script.h`: `int nk_script_frame(uint32_t b[NK_PLAYERS]);`. It fills all `NK_PLAYERS` entries, with 0 beyond `players`.
- Script syntax:
  - A button with no prefix is player 1's.
  - `N.` before a button, for N from 2 to `players`, makes it player N's.
  - Any other prefix (`1.`, `0.`, or N above `players`) is not a button, so the step fails to parse exactly as an unknown button does today.

- [ ] **Step 1: Write the failing test**

In `kit/tests/states_check.py`, add cases that run the stub console with a script that uses prefixes. Use the same harness the file already uses to run `kit_stub_console` on a script, and read how it builds scripts and environments.
- With the default (2 players): `2.A` runs; `1.A`, `3.A` and `9.A` each fail with the same message as an unknown button. Assert the exact stderr text, taken from today's failure for `XYZ` (read it from `script.c`'s parse error, `"'%s' is not ..."` or similar).
- With `STUB_PLAYERS=4`: `4.A` runs and `5.A` fails.
- Have the stub dump its players' buttons each frame as a region, so the check can assert that `4.A` reached player 4. Add `region` `.players` returning the last `b` it got.

Run `nix develop -c mise run test:kit`. Expected: FAIL, because `STUB_PLAYERS` is ignored and `4.A` fails.

- [ ] **Step 2: Implement in the kit**

In `script.c`:
- `Step.buttons` becomes `uint32_t buttons[NK_PLAYERS];`.
- `add_step` takes `const uint32_t *b` (NULL meaning all 0) instead of `b0, b1`. Update its callers (lines ~196–202 and `add_text`).
- `parse_buttons(const char *t, uint32_t b[NK_PLAYERS])`:
```c
/* the console's buttons (nk_console.buttons) by name, '+' between them;
   N. before one for player N, 2 to the console's players (2.START) */
static int parse_buttons(const char *t, uint32_t b[NK_PLAYERS])
{
    const nk_button *k = con->buttons;
    int nk = con->button_count;
    char buf[256], *p, *next;
    memset(b, 0, sizeof(uint32_t) * NK_PLAYERS);
    snprintf(buf, sizeof buf, "%s", t);
    for (p = buf; p; p = next) {
        int pad = 0;
        int i;
        next = strchr(p, '+');
        if (next)
            *next++ = 0;
        if (p[0] >= '2' && p[0] <= '9' && p[1] == '.' && p[0] - '0' <= con->players) {
            pad = p[0] - '1';
            p += 2;
        }
        for (i = 0; i < nk; i++)
            if (!strcmp(p, k[i].name))
                break;
        if (i == nk)
            return 0;
        b[pad] |= k[i].bit;
    }
    return 1;
}
```
  With `players = 2`, this accepts exactly what today's code does: only `2.`. Any other `N.` falls through to the name lookup and fails there, the same path as before.
- `nk_script_frame(uint32_t b[NK_PLAYERS])` copies `s->buttons` into `b` with `memcpy`.

In `app.c`:
- `uint32_t b[2] = { 0, 0 };` becomes `uint32_t b[NK_PLAYERS] = { 0 };`.
- `nk_script_frame(&b[0], &b[1])` becomes `nk_script_frame(b)`.
- At line ~606, `uint32_t b0, b1; ... nk_script_frame(&b0, &b1);` becomes `uint32_t b[NK_PLAYERS]; ... nk_script_frame(b);`.
- In `nk_main`, after `c` is set: if `c->players < 1 || c->players > NK_PLAYERS`, call `nk_fatal("the console's %d players, not 1 to %d", c->players, NK_PLAYERS)`.

Fix the `script.h` header comment to describe `N.`.

- [ ] **Step 3: The SNES and the stub**

- In `snes_console.c`, add `.players = 2,`.
- Change `snes_input`'s signature to `(int headless, const uint32_t *b)` in `snes_console.h` and `console.c`. The body still reads `b[0]` and `b[1]`.
- The stub reads `STUB_PLAYERS` (`nk_getenv("PLAYERS")` with its env prefix) into `.players`. Since `.players` is a `const` table field, make the stub's table non-const, or read the setting in `main` before calling `nk_script_console`. Follow how the stub already reads its settings.
- The stub's `nk_script_frame` calls at lines 93 and 96 move to the array form.

- [ ] **Step 4: Run the tests**

Run `nix develop -c mise run test:kit`. Expected: states_check passes, including the new prefix cases. Then run the gate, perf included. `kit/src/front/script.c` is still free of console names.

- [ ] **Step 5: Commit**

```bash
git add kit consoles/snes/src/front
git commit -m "nativekit: scripts for the console's players, not two"
```

---

### Task 6: The keyboard map comes from the console

**Files:**
- Modify: `kit/include/nk/input.h` (`nk_key`, `nk_input_keymap`; the header comment's table becomes generic)
- Modify: `kit/src/front/input.c:58-103` (k_map leaves; the map is set)
- Modify: `kit/include/nk/nk.h` (`keys`, `key_count`), `kit/src/front/app.c` (it hands the map to the input layer in `nk_main`)
- Modify: `consoles/snes/src/front/snes_console.c` (today's table, as data), `kit/tests/input_check.c` (its own table), `kit/tests/stub_console.c` (a small table)

**Interfaces:**
- Produces, in `nk.h` (beside `nk_button`, so `nk.h` needs no `input.h`):
  ```c
  /* a key: the host's virtual key, the positional bit it sets and its name
     on screen ("Z", "Return") */
  typedef struct { int vk; uint32_t bit; const char *name; } nk_key;
  ```
  and in `input.h` (which includes `nk.h` since Task 5):
  ```c
  /* the keyboard's keys (the console's: nk_console.keys); none before */
  void nk_input_keymap(const nk_key *keys, int n);
  ```
  `nk_input_keyboard_bits` and `nk_input_key_name` widen to `uint32_t`, so positional bits can go past 16 (a 6-button pad's second row).
- Produces, in `nk.h`: `const nk_key *keys; int key_count;`, beside `buttons`.
- `nk_main` calls `nk_input_keymap(c->keys, c->key_count)` before the first frame.

- [ ] **Step 1: Write the failing test**

In `kit/tests/input_check.c`, before the first `nk_input_key`, set its own table:
```c
static const nk_key keys[] = {
    { 0x26, 1u << 12, "Up" }, { 'Z', 1u << 0, "Z" }, { 0x10, 1u << 6, "Right Shift" },
    { 0x09, 1u << 6, "Tab" }, { 'C', 1u << 20, "C" },
};
...
    nk_input_keymap(keys, (int)(sizeof keys / sizeof keys[0]));
```
Keep the existing checks. Add:
- `CHECK(!strcmp(nk_input_key_name(1u << 20), "C"));`
- pressing `'C'` gives bit 20;
- pressing `'Q'`, which is not in this table, changes no bits and makes no report: `reports` stays as it was.

Run `nix develop -c mise run test:kit`. Expected: FAIL to build, because `nk_key` and `nk_input_keymap` don't exist.

- [ ] **Step 2: Implement**

In `input.c`:
- replace the `k_map` array with `static const nk_key *k_map; static int k_n;`;
- add `void nk_input_keymap(const nk_key *keys, int n) { k_map = keys; k_n = n; }`;
- loop to `k_n`;
- widen `keys` and the return types to `uint32_t`.

In `input.h`, rewrite the header comment's table:
- the controller column stays;
- the keyboard column goes;
- add "the keyboard's keys are the console's (nk_console.keys)".

In `nk.h`, add `nk_key` and the fields with a comment; do not include `input.h` there. In `app.c`'s `nk_main`, call `nk_input_keymap(c->keys, c->key_count);` next to the other set-up.

- [ ] **Step 3: The SNES's table**

In `snes_console.c`, add today's table, in today's order. Order matters, because `nk_input_key_name` returns the first match, so "Right Shift" comes before "Tab":
```c
/* the keyboard: arrows, Z X A S, Q W, Return, Right Shift and Tab, as
   positional bits (snes_input.c maps them to the joypad) */
static const nk_key keys[] = {
    { 0x26, 1u << 12, "Up" }, { 0x28, 1u << 14, "Down" }, { 0x25, 1u << 15, "Left" },
    { 0x27, 1u << 13, "Right" }, { 'Z', 1u << 0, "Z" }, { 'X', 1u << 1, "X" }, { 'A', 1u << 2, "A" },
    { 'S', 1u << 3, "S" }, { 'Q', 1u << 4, "Q" }, { 'W', 1u << 5, "W" }, { 0x0D, 1u << 7, "Return" },
    { 0x10, 1u << 6, "Right Shift" }, { 0x09, 1u << 6, "Tab" },
};
```
and `.keys = keys, .key_count = (int)(sizeof keys / sizeof keys[0]),`.

Check `snes_input.c`'s callers of `nk_input_keyboard_bits` and `nk_input_key_name` against the widened types: `joypad_of` takes `unsigned` today; `uint32_t` is the same width on every target. Give the stub console a two-key table.

- [ ] **Step 4: Run the tests**

Run `test:kit` and expect `input_check: ok`. Then run the gate, perf included. Also run `consoles/snes/tests/unit/input` (in `mise run test`); it pins the SNES's key names and joypad bits. Expected: unchanged.

- [ ] **Step 5: Commit**

```bash
git add kit consoles/snes/src/front
git commit -m "nativekit: the keyboard's keys are the console's"
```

---

### Task 7: Named access kinds, and the cap as a constant

**Files:**
- Modify: `kit/include/nk/nk.h` (`NK_KINDS_MAX`, `kind_names`)
- Modify: `kit/src/front/app.c:139, 143-149, 591` (the 16s, and the out-of-range message)
- Modify: `consoles/snes/src/front/snes_console.c` (it names its 4 kinds: optional, but give them, from `nshvc.h`'s `NSHVC_KIND_*` order)

**Interfaces:**
- Produces, in `nk.h`:
  - `#define NK_KINDS_MAX 16`;
  - `const char *const *kind_names;`, optional: `kind_count` names, for messages and a console's fitter.
- `app.c` keeps every output byte-identical:
  - the `--counts` file is unchanged;
  - the out-of-range message is unchanged when `kind_names` is NULL;
  - when the names are given, the message gains ` (the console's kinds: wram, rom, ...)` at its end. That happens only on a console bug, and no golden run reaches it.

- [ ] **Step 1: Implement**

In `app.c`:
- `static uint64_t counts[2][16];` becomes `[2][NK_KINDS_MAX]`;
- line ~591, `if (c->kind_count > 16)`, becomes `> NK_KINDS_MAX`, and its message uses the constant through `%d`;
- `count_kind`'s message appends the names when present:
```c
static int count_kind(uint32_t addr)
{
    int k = c->access_kind(addr);
    if (k < 0 || k >= c->kind_count) {
        char names[256] = "";
        int i;
        for (i = 0; c->kind_names && i < c->kind_count; i++)
            snprintf(names + strlen(names), sizeof names - strlen(names), "%s%s", i ? ", " : " (the console's kinds: ",
                     c->kind_names[i]);
        nk_fatal("an access of kind %d, not one of the console's %d%s%s", k, c->kind_count, names,
                 names[0] ? ")" : "");
    }
    return k;
}
```
Before replacing the existing format string, read it. The text that prints when the names are absent must be byte-identical: `"an access of kind %d, not one of the console's %d"`.

In `snes_console.c`, add `static const char *const kind_names[] = { "wram", "rom", "cart ram", "io" };` in `NSHVC_KIND_*` order (check `nshvc.h:106`), plus `.kind_names = kind_names,`. Add a `_Static_assert` that the array has `NSHVC_KINDS` entries.

- [ ] **Step 2: Run the gate**

Expected: all pass. The counts runs in the golden master are byte-identical.

- [ ] **Step 3: Commit**

```bash
git add kit/include/nk/nk.h kit/src/front/app.c consoles/snes/src/front/snes_console.c
git commit -m "nativekit: access kinds named; their cap a constant"
```

---

### Task 8: nk_quit, and a kit-only loop test for the battery save

**Files:**
- Modify: `kit/include/nk/nk.h` (`nk_quit`), `kit/src/front/app.c` (the flag; the frame's end)
- Create: `kit/tests/loop_check.c`
- Modify: `kit/tests/CMakeLists.txt` (`kit_loop_check` links `nk_app`, `nk_front`, `nk_input`, `nk_resample`, the host objects (`${NK_HOST_OBJECTS}`) and `nk_render`), and the kit-check target

**Interfaces:**
- Produces, in `nk.h`:
  ```c
  /* the loop ends at this frame's end, as when the window closes (the save
     written); a headless run ends as at its script's end */
  void nk_quit(void);
  ```
- In `app.c`, after `c->frame()` in the windowed frame, `if (quitting) quit();`. In the headless frame, after the console's frame hook, `if (quitting) exit(0);`, matching how the headless run ends today at its last frame (read `frame()`'s headless half first and do exactly what its end-of-script path does).

- [ ] **Step 1: Write the failing test**

`kit/tests/loop_check.c` is a stub console with a 16-byte battery whose first 4 bytes are the frame number, written by `run_frame`. Its `frame` hook:
- at frame 121, reads `<save dir>/loop.srm` and checks it holds frame 120's bytes, so the write at 120 happened;
- at frame 130, calls `nk_quit()`.

After `nk_main` returns or exits, the check has to verify the file holds frame 130's bytes. Do that through `atexit` registered in `main` before `nk_main`:
```c
/* loop_check: the kit's loop through frames on the null host. Windowed: the
   battery saved at frame 120 and at quit (nk_quit), as a new file renamed
   over the old; --headless: never saved. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nk/fatal.h"
#include "nk/host.h"
#include "nk/nk.h"

void nk_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(70);
}

static uint8_t ram[16];
static uint32_t frame;
static int headless_run, fails;
static char path[1024];

static int read_frame(uint32_t *f)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp)
        return 0;
    n = fread(f, 1, sizeof *f, fp);
    fclose(fp);
    return n == sizeof *f;
}

static void power(const void *cart) { (void)cart; }
static void run_frame(void) { frame++; memcpy(ram, &frame, sizeof frame); }
static uint64_t frame_ns(void) { return 1000000ull; }   /* 1 ms: the test runs fast */
static void app_name(const void *cart, const char **dir, const char **title) { (void)cart; *dir = "loop_check"; *title = "loop_check"; }
static void window_size(int *w, int *h) { *w = 8; *h = 8; }
static double aspect(void) { return 1.0; }
static void present(const int d[4]) { (void)d; }
static uint32_t pic[64];
static const uint32_t *shot(int *w, int *h, int *xs, int *ys) { *w = *h = 8; *xs = *ys = 1; return pic; }
static const uint32_t *picture(int *w, int *h) { *w = *h = 8; return pic; }
static int audio_take(int16_t *lr, int max) { (void)lr; (void)max; return 0; }
static int audio_queued(void) { return 0; }
static void input(int headless, const uint32_t *b) { (void)headless; (void)b; }
static int battery(nk_battery *b)
{
    b->ram = ram;
    b->bytes = sizeof ram;
    b->from = 0;
    b->size = 0;
    b->name = "loop";
    return 1;
}
static void at_frame(void)
{
    uint32_t f = 0;
    if (frame == 121 && !headless_run && !(read_frame(&f) && f == 120)) {
        fprintf(stderr, "loop_check: at frame 121 the save holds %u, not 120\n", f);
        fails++;
    }
    if (frame == 130)
        nk_quit();
}
static void at_exit(void)
{
    uint32_t f = 0;
    int have = read_frame(&f);
    if (headless_run ? have : !(have && f == 130)) {
        fprintf(stderr, "loop_check: at the end the save %s\n", have ? "is there" : "is missing");
        fails++;
    }
    printf(fails ? "loop_check: FAIL\n" : "loop_check: ok\n");
    if (fails)
        _exit(1);
}

static const nk_console loop = {
    .name = "loop_check", .env = "LOOP", .play_ext = ".loop",
    .power = power, .run_frame = run_frame, .frame_ns = frame_ns, .app_name = app_name,
    .window_size = window_size, .aspect = aspect, .present = present, .shot = shot, .picture = picture,
    .audio_hz = 48000.0, .audio_take = audio_take, .audio_queued = audio_queued,
    .players = 1, .input = input, .frame = at_frame, .battery = battery, .at_exit = at_exit,
};

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--headless"))
            headless_run = 1;
    /* the null host's save folder: PORT_SAVEDIR, emptied of a last run's file */
    snprintf(path, sizeof path, "%s/loop.srm", getenv("PORT_SAVEDIR") ? getenv("PORT_SAVEDIR") : ".");
    remove(path);
    return nk_main(&loop, NULL, argc, argv);
}
```
Adjust the field names to `nk.h` as it stands after Tasks 5–7, and include `#include <unistd.h>` for `_exit`. `at_exit` is the console's `at_exit`, which `nk_main` registers with `atexit`; check that this is how app.c uses it. A headless run with no script runs until… check what `nk_main` does headless without a script. If it runs forever, `nk_quit` at frame 130 ends it, which is the point of the headless half of the check.

In `kit/tests/CMakeLists.txt`, add `kit_loop_check`, built only when `NK_HOST` is `null` (`if(NK_HOST STREQUAL "null")`). In `kit-check`, add the windowed and headless runs with `PORT_SAVEDIR` set to a folder under `${CMAKE_BINARY_DIR}/kit/tests/loop_save`, created with `${CMAKE_COMMAND} -E make_directory`.

Run `nix develop -c mise run test:kit`. Expected: FAIL to build, because `nk_quit` doesn't exist.

- [ ] **Step 2: Implement nk_quit**

In `app.c`:
- add `static int quitting; void nk_quit(void) { quitting = 1; }`;
- in the windowed `frame()`, after `if (c->frame) c->frame();`, add `if (quitting) quit();`;
- in the headless half, after its frame hook call, add the same end the script's end takes.

- [ ] **Step 3: Run the tests**

Run `test:kit`. Expected: both `loop_check: ok` lines. Then run the gate: the SNES never calls `nk_quit`, so the golden master is unchanged.

- [ ] **Step 4: Commit**

```bash
git add kit
git commit -m "nativekit: nk_quit, and a loop check that pins the battery save's timing"
```

---

### Task 9: Documents and the stage's verification

**Files:**
- Modify: `kit/docs/kit.md` (the new `nk_console` fields `players`, `keys`, `key_count`, `kind_names`; `NK_KINDS_MAX`, `nk_quit`, `rr_set_headless_size`; the `nk/...` include rule; the `NK_*` options, `NK_CONSOLES` and the `NSHVC_*` aliases)
- Modify: `README.md` (the build section's option names; `-DNSHVC_*` noted as still working), `consoles/snes/README.md` if it names the options or the flat include path
- Modify: `kit/VERSION` → `0.2.0` (new fields: a minor version, per `kit/docs/contract-discipline.md`)

- [ ] **Step 1: Write the docs**

Write each new item as a short entry in the style kit.md already uses. Name no console under `kit/`: the name check reads `kit/docs`.

- [ ] **Step 2: Link check and name check**

Re-run the link checker from the extraction's Task 14 report (`.superpowers/sdd/2026-10-30-nativekit-extraction/task-14-report.md` has the script; if that file is gone, write a 10-line one that resolves every relative `](...)` link in `README.md`, `consoles/snes/README.md` and `kit/docs/*.md`). Expected: 0 missing. Then configure `build/null` so the name check runs. Expected: passes.

- [ ] **Step 3: The stage's verification**

Run the full gate, perf included, then `nix develop -c mise run test:rom`. Expected: `0 of 1164 differ; 56 more differ as KNOWN lists; 0 listed in KNOWN now pass`.

- [ ] **Step 4: Commit**

```bash
git add kit/docs kit/VERSION README.md consoles/snes/README.md
git commit -m "nativekit 0.2.0: the console's players, keys, kinds and size; neutral options"
```
