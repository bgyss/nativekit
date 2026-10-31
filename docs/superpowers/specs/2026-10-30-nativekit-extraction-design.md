# nativekit: extracting a console-independent kit from nativeshvc

Date: 2026-10-30. Status: design, awaiting review. Branch: `nativekit`.

## Purpose

nativeshvc is a native, behaviour-exact Super Nintendo for decompiled games. Most of
what makes it a usable platform is not SNES-specific: the hosts, the coroutines, the
routine table, the headless script runner with fork/replay states, the cost-profile
fitter, the presentation and the contract discipline. This sub-project extracts those
into a kit, **nativekit**, and rebuilds nativeshvc on it, so that further native consoles
(Mega Drive first, then Master System, NES, and PSX or N64) start with a working front
end, scripts, states and headless runs.

This is sub-project 1 of 5. Each later console has its own spec, plan and implementation.

## Decisions taken

- **Home:** a monorepo in the user's fork of `jeff-strutb/nativeshvc`. nativeshvc is
  AGPL-3.0; the kit is a work based on it and stays AGPL-3.0, with `NOTICE` and the
  copyright lines kept.
- **Bar for later consoles:** the full nativeshvc standard, staged (reference harness
  first, then the native console held to it). No specific game is targeted; test ROMs
  and homebrew are the proof.
- **Approach:** a kit with a console interface, extracted in two stages (pure moves,
  then the front end split around `nk_console`), chosen over moving only what is already
  generic, or building the Mega Drive first and extracting afterwards.

## Success criteria

1. Every output of nativeshvc is byte-for-byte unchanged on every test cartridge and
   unit test (the golden master, below), after every step.
2. `tests/rom/run.py` against the bsnes v115 reference gives the same result as before
   the work: 1,108 match, 42 multiplier/divider reads, 14 known, as `tests/rom/KNOWN`.
3. The SNES game contract (`machine.h`, `spc.h`, `nshvc.h`, `cart.h`, `hw.h`) is
   unchanged byte for byte; nativeshvc stays at contract 2.10.
4. `kit/` names no console (no SNES, PPU, 65816, SPC700 or similar term), checked in
   the build.
5. Headless frame time on the test cartridges is no more than 3% slower.
6. Every check in `tests/README.md` that ran before runs after, on the same backends
   and hosts.

## Non-goals

- No new console, and no change to any SNES behaviour, contract or extension.
- No new host (Linux windowed host, a run on Windows): their status is unchanged.
- No upstream contribution in this sub-project.

## Layout

```
nativeshvc/                       (the fork; AGPL-3.0, NOTICE and copyright kept)
  kit/                            nativekit: console-independent, own semver (0.1.0)
    include/nk/   nk.h            the console interface
                  host.h coro.h jump.h rr.h resample.h script.h routines.h input.h
    src/host/     posix win32 null macos windows ios        (moved as they are)
    src/coro/     coro.c coro_arm64.S coro_x86_64.S         (moved as they are)
    src/dsp/      resample.c                                 (moved as it is)
    src/render/   soft/ metal/ gl/ vulkan/: generic presentation only (open, begin,
                  image with sharp bilinear, overlays, present, read back), rr_size.h
    src/front/    app.c       the loop, pacing, audio drain, battery save, overlays,
                              fps meter, the generic rrcheck
                  script.c    parser, steps, repeat, fork/replay states, plays
                  input.c     native devices to abstract players
    src/dispatch/ routines.c  sorted routine table lookup
    tools/        nk_profile.py   the cost-profile fitter as a library
    docs/         kit.md, contract-discipline.md
    tests/        the kit's own unit tests
  consoles/snes/                  nativeshvc as it is, on the kit
    include/nshvc/                the game contract, unchanged
    src/console src/ppu src/dsp   unchanged but for includes and the kit's calls
    src/render/                   compose_body.h, rr_compose.h, rr_mode7.h, and each
                                  backend's compositor and Mode 7 extension
    src/front/                    the public calls and the SNES nk_console
    tests/ examples/ docs/ tools/nshvc-profile
  docs/superpowers/               specs and plans for the monorepo
  CMakeLists.txt                  kit + consoles/snes; NSHVC_HOST, NSHVC_RENDER,
                                  NSHVC_TESTS, NSHVC_EXAMPLES kept
```

The top-level `README.md`, `LICENSE` and `NOTICE` stay at the root. The root README gets
a short section on the monorepo; nativeshvc's own README moves to `consoles/snes/` with its
relative links kept working.

### Boundary rules

- The kit names no console. A CMake check greps `kit/` for a list of console terms and
  fails the configure on a hit.
- Consoles depend on the kit; the kit never on a console. A console's game-facing
  headers are its own contract; games never include kit headers.
- The renderer backends are split in stage 2, not moved whole: Metal, GL and Vulkan
  each carry the SNES compositor and Mode 7 GPU programs (`rr_compose`,
  `rr_compose_woven`, `rr_mode7`, the embedded SPIR-V). Generic presentation goes to the
  kit; the SNES GPU work becomes a backend extension in `consoles/snes/src/render/`, through
  a small per-backend hook that hands it the device, the command encoder and the target.
- Environment settings keep their names. The kit reads its settings with a prefix the
  console gives (`nk_console.env`): `NSHVC_STATES`, `NSHVC_SCRIPT_NOFORK`,
  `NSHVC_SCRIPT_PLAN`, `NSHVC_RRCHECK`, `NSHVC_FPS` and the rest keep working for the SNES
  as they are, and a later console uses its own prefix.

## The console interface

```c
/* kit/include/nk/nk.h */
typedef struct {                        /* a named memory, for dumps and checks */
    const char *ext;                    /* ".wram", ".vram", ... */
    const void *(*data)(size_t *bytes); /* as it stands now (a chip brought up to date) */
} nk_region;

typedef struct {                        /* a script button name and the console's bit */
    const char *name;                   /* "B", "START", ... */
    uint32_t bit;
} nk_button;

typedef struct nk_console {
    const char *name;                   /* "nativeshvc": messages, the fatal prefix */
    const char *env;                    /* "NSHVC": its environment settings' prefix */
    const char *play_ext;               /* ".sfx": the plays that save states */

    /* the machine */
    void   (*power)(const void *cart);
    void   (*run_frame)(void);          /* to this frame's end */
    uint64_t (*frame_ns)(void);         /* 16639267 NTSC, 19997194 PAL, ... */

    /* the picture */
    void   (*window_size)(int *w, int *h, const char **title);
    double (*aspect)(void);             /* the picture's display aspect now */
    void   (*present)(const int d[4]);  /* the picture onto target rectangle d */
    const uint32_t *(*picture)(int *w, int *h);   /* shots, the rrcheck reference */
    /* optional: its own reference for a frame it presents itself (want: tw x th);
       0 when the generic reference (the picture scaled) applies */
    int    (*rrcheck)(const int d[4], uint32_t *want, int tw, int th);
    void   (*scaled)(int w, int h);     /* optional: the picture's size on the window */
    int    (*picture_at)(float u, float v, int *x, int *y); /* optional: a point to coords */

    /* the sound */
    double audio_hz;                    /* 32040 */
    int    (*audio_take)(int16_t *lr, int max);
    int    (*audio_queued)(void);

    /* the controllers */
    int players;
    const nk_button *buttons;
    int button_count;
    void   (*player_state)(int player, const nk_pad *pad);
    int    (*script_step)(int argc, char **argv);   /* its own steps; 1 when taken */

    /* memories */
    const nk_region *regions;
    int region_count;
    uint8_t *(*battery)(size_t *bytes);             /* NULL: no save */
    void   (*counts_hooks)(void (*rd)(uint32_t, uint8_t), void (*wr)(uint32_t, uint8_t));
    int    (*access_kind)(uint32_t addr);
    const char *const *kinds;
    int kind_count;

    void   (*at_exit)(void);            /* its own checks' reports */
} nk_console;

int nk_main(const nk_console *c, const void *cart, int argc, char **argv);
```

Exact field types may tighten during implementation where the current code demands it
(for example the shape of `present` around Mode 7 and the canvas); any such change is
recorded in the plan, keeps the console owning its picture and the kit owning the
window, and does not touch the SNES game contract.

### How the parts use it

- **The loop (`app.c`).** Argument parsing (`--headless`, `--script`, `--out`,
  `--frames`, `--counts`, `--sound`), the window, the renderer, sound output through the
  resampler at `audio_hz`, frame pacing on `frame_ns`, the battery save
  (`SAVE_NAME.srm`, written when it changes and every 120 frames, never in a headless
  run), overlays, the fps meter and log, and the generic rrcheck. `nshvc_main` becomes
  `return nk_main(&snes_console, cart, argc, argv);`.
- **Input (`input.c`).** Keyboards, game controllers, the mouse and touches become an
  `nk_pad` per player: a positional pad (d-pad, four face buttons, shoulders, start,
  select, sticks), pointer and mouse motion, and the device's source and button names.
  The console maps `nk_pad` to its own controllers; the SNES keeps today's mapping table,
  its mouse, multitap and light guns, and fills its public `nshvc_input_source` and
  `nshvc_player_source` from the kit's.
- **Routines (`routines.h`).** `nk_routine_find(base, count, stride, addr)` over any
  sorted table whose first member is a `uint32_t addr`. The SNES asserts
  `offsetof(nshvc_routine, addr) == 0` at compile time and keeps its own types, messages
  and the SPC700's `uses` selection. Interrupt sequences and register files stay in each
  console.
- **Scripts and states (`script.c`).** The kit parses and runs `wait`, `hold`, `press`,
  `shot`, `dump`, `repeat`/`end`, `save` and `load`, with fork and with the no-fork
  replay in a new copy of the program. Button names come from `buttons`; any other step
  goes to `script_step` (the SNES: `plug`, `pad`, `mouse`, `gun`). `dump NAME` writes
  every region as `DIR/NAME<ext>`. A state is a process, so the kit never serializes a
  machine, for any console.
- **The cost profile.** `kit/tools/nk_profile.py` holds the fitter (subset least
  squares, the per-program chip fits), the error report and the profile file format, over
  kinds the caller names. `consoles/snes/tools/nshvc-profile` keeps its command line and
  file format and supplies the SNES's kinds, its reference runs and its C table.

## Migration and the regression gate

### The golden master

Recorded at the base commit before any code moves: every test cartridge run headless on
nativeshvc with its shots, dumps, `--sound` output and `--counts` file; the hello
example's headless outputs; and the outputs of each unit test. A script
(`consoles/snes/tests/golden.py`, added in step 0) records and compares them by hash and
names each file that differs. After every step, nothing may differ. A difference stops
the step and its cause is found; the master is re-recorded only for a difference the
user approves as intended, and none is expected in this sub-project.

The reference comparison (`tests/rom/run.py`) runs at step 0 and at the end of each
stage.

### Step 0: tooling and baseline

- Build asar from source and set `NSHVC_ASAR`.
- Build the bsnes v115 libretro core with the two front-end changes in
  `tests/README.md` (light-gun inputs passed through; display gamma 1.0) into
  `build/oracle-core` and set `NSHVC_ORACLE_CORE`.
- Build headless (`soft`) and macOS (`metal`), with tests and examples.
- Run `tests/rom/run.py`. If it does not give 1,108 / 42 / 14 here, stop and report
  before any code moves.
- Run every other check in `tests/README.md` that this machine can run, and record which
  ran and which could not.
- Record the golden master and the headless timing baseline.

### Steps

Each step is one commit or a short series on `nativekit`, gated by the golden master
plus the checks named.

1. **Restructure.** `git mv` the tree into `consoles/snes/`; a top-level CMake builds it.
   No code changes.
2. **Stage 1 moves.** Hosts, coroutines, `jump.h` and the resampler into `kit/`;
   includes and CMake adjusted.
3. **Routines and the profile library.** `nk_routine_find` used by `dispatch.c`'s
   S-CPU lookup (the SPC700's lookup, a scan with `uses` selection, stays as it is);
   `nk_profile.py` split from `nshvc-profile`. Plus: `profile_check.py`, and
   `nshvc-profile`'s output on a synthetic input identical before and after.
4. **Scripts.** The kit's parser and states; the SNES's steps through `script_step`;
   dumps through regions. Plus: `states.py` with fork and with `NSHVC_SCRIPT_NOFORK=1`.
5. **Input.** `nk_pad` and the SNES mapping. Plus: `tests/unit/input`, and
   `pointer_macos` in a window.
6. **The loop.** `nk_main`, with `nshvc_main` as its wrapper. Plus: hello in a window on
   macOS, and the headless timing check.
7. **The renderers, a backend a commit:** soft, Metal, GL, Vulkan. Plus, through that
   backend: `NSHVC_RRCHECK` on the test cartridges at two window sizes, `present_check`,
   `canvas_check`, `screen_check`, `overlay_check`.
8. **The kit's own tests, the no-console-names check, and the docs**
   (`kit/docs/kit.md`, `kit/docs/contract-discipline.md`, the root README's monorepo
   section). Then the reference comparison and every check once more.

### What runs and what only compiles

Run on this Mac: headless with `soft`, macOS with Metal, on arm64 and on x86-64 under
Rosetta. GL and Vulkan are run if this Mac provides them (GL is deprecated on macOS but
usually present; Vulkan needs MoltenVK), else compile-checked. Windows (MinGW cross
build) and iOS are compile-checked if their toolchains are present; neither has been run
on its platform before this work. Each report says which checks ran and which only
compiled.

## The kit's own tests

- Coroutines: switching, a coroutine's own stack, deep recursion on it.
- Routines: lookup over tables of several strides, absent addresses.
- Scripts: the parser's steps and errors (line numbers in messages), `repeat` nesting,
  a stub console's own step taken through `script_step`.
- States: `save` and `load` with fork and without, on a stub console whose machine is a
  counter and a pseudo-random sequence, dumps identical both ways.
- The profile fitter: `profile_check.py`'s cases on the library with made-up kinds.

## Versioning

nativekit starts at 0.1.0 and is internal to consoles until a second console uses it;
it reaches 1.0 when the Mega Drive builds on it unchanged. Its versioning rules follow
nativeshvc's (major changes break, minor adds, patch fixes) and are written in
`kit/docs/contract-discipline.md` together with the standards every console in the
monorepo keeps: behaviour exact against a pinned reference, a fatal error rather than
a guess, extensions off until asked for, and the same run on every host.

## Risks

- **The reference may not reproduce here.** The bsnes v115 build or asar may not give
  today's numbers on this Mac. Step 0 stops before code moves if so.
- **Indirection cost.** The kit adds an indirect call a frame and none an access; the
  timing check holds it to 3%.
- **Renderer split.** The GPU backends mix generic and SNES work closely; each is split
  alone and checked through rrcheck before the next. Vulkan's embedded SPIR-V
  (`rr_spv.h`) is generated from `src/render/vulkan/shaders/` by `build.sh`: the generic
  shaders (`quad.vert`, `sharp.frag`, `copy.frag`, `over.frag`) go to the kit and the
  compositor and Mode 7 shaders stay with the SNES, each side generating its own header;
  if `glslc` is not available here, the split keeps the existing SPIR-V words unchanged
  and divides them by shader.
- **Coroutine and jump subtleties.** `jump.h`'s clang note (x86-64: `_setjmp`/`_longjmp`,
  not the builtin pair) must survive the move; the x86-64 run under Rosetta covers it.
- **Platforms that only compile.** GL, Vulkan, Windows and iOS may only be
  compile-checked; reports say so.
