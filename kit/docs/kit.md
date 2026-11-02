# nativekit

nativekit is the half of a native console that is not the console. A native console is
a behaviour-exact machine a decompiled game's C runs on directly: its bus, its picture and
sound units, its controllers and its clock, written as native code. Around that machine
every console needs the same application: a window, a frame's pace against the wall
clock, sound out at the device's rate, the players' keyboards, game controllers, mice
and touches, a battery save, a headless run driven by a script with shots, dumps and saved
states, the picture presented on the GPU at any window size, and the hosts under all of
it. nativekit is that application, written once, with no console in it.

A console fills one table (`nk_console`, in [`nk.h`](../include/nk/nk.h)) and calls
`nk_main`. The kit runs the program; the console owns its machine, its picture and its
controllers. Games never include the kit's headers: a console's own headers are its
game's contract.

nativekit is at 0.2.0 ([contract-discipline.md](contract-discipline.md)).

## The parts

Every header is in [`include/nk/`](../include/nk/); consoles include it as `nk/<name>.h`.
Every source is in [`src/`](../src/).

**The loop and `nk_console`** ([`nk.h`](../include/nk/nk.h),
[`src/front/app.c`](../src/front/app.c)). `nk_main(console, cart, argc, argv)` reads the
arguments (`--headless`, `--script FILE`, `--out DIR`, `--frames N`, `--counts FILE`,
`--sound FILE`), names the program from the cartridge (`app_name`: the save folder and
the window's title), opens the host, powers the console on and runs it a frame at a time
(`run_frame`). In a window it paces each frame on `frame_ns` against the wall clock,
drains the console's sound (`audio_take`, at `audio_hz`) through the resampler to the
device, brings the players' devices to the console (`input`), writes the battery save
and draws the game's overlays. Headless, it runs as fast as it can and takes the
script's buttons, shots and dumps. `--counts` writes each frame's accesses by the
console's kinds (`access_kind`, `kind_count`, at most `NK_KINDS_MAX`), for the cost profile's
fitter; a read in a run of more than 64 with no write between is left out. `--sound`
writes the console's samples as made. The table's fields are commented in `nk.h`;
those marked optional may be NULL. Optional `kind_names` identify access kinds in error
messages. `nk_quit()` ends the current frame; windowed runs save on quit, headless runs do not.

**Scripts and states** ([`script.h`](../include/nk/script.h),
[`src/front/script.c`](../src/front/script.c)). A headless run's script: `wait`,
`hold`, `press`, `shot`, `dump`, `repeat` ... `end`, `save` and `load`, with the
console's buttons (`buttons`, `N.NAME` for players 2 through `players`, up to `NK_PLAYERS`) and its own steps (`steps`,
each line given to `script_step`). `shot NAME` writes the console's picture
(`shot`) as a PPM; `dump NAME` writes each of the console's memories (`regions`) as
`DIR/NAME<ext>`, in the table's order. A state is a process: `save` forks and one copy
holds the machine while the other goes on, and a state no process holds is rebuilt by
replaying, from power on, the steps that saved it, the script's own or a play in
`<prefix>_STATES` (the files ending in `play_ext`). Without `fork` (Windows), or with
`<prefix>_SCRIPT_NOFORK=1`, the run goes on in a new copy of the program that replays to
the machine wanted. So the kit never serializes a machine, for any console. A script
error names its line.

**Input devices** ([`input.h`](../include/nk/input.h),
[`src/front/input.c`](../src/front/input.c)). The keyboard, up to eight game
controllers, the mouse and touches, as positional bits (the bottom, right, left and top
face buttons, the shoulders, view and menu, the d-pad, the left stick past half setting
the d-pad too), with each device's name and each button's label as the device shows it.
It tracks which device drives each player and reports changes (`nk_input_report`), and
maps a point in the window to the console's column and row (`nk_input_point`). The
console supplies its keyboard map in `keys` and `key_count`, and maps inputs onto its own
controllers in `input`.

**The save** ([`save.h`](../include/nk/save.h), [`src/front/save.c`](../src/front/save.c)).
The console's battery RAM (`battery` fills an `nk_battery`: the RAM, its size, the save
area within it and the file's name), loaded from `<save folder>/<name>.srm` at start and
written when its save area changes: checked every 120 frames and at quit, written to a
`.new` file renamed over the old. A headless run never reads or writes it. A save area
past the RAM is a fatal error.

**Presentation and the backend hooks** ([`rr.h`](../include/nk/rr.h),
[`rr_size.h`](../include/nk/rr_size.h), [`src/render/`](../src/render/)). Four backends,
Metal, OpenGL 3.3, Vulkan 1.0 and software, chosen at build time. Each draws a frame
into a target the size of the window's drawable: the target cleared, pictures of the
console's pixels placed on it with sharp bilinear filtering (whole pixels at any size,
blended only across the one target pixel a pixel's edge falls in), the game's overlays
blended over, then shown, and read back for checks. `rr_size.h` is the one definition of
sharp bilinear and of the overlay's drawing that the software backend draws and the GPU
backends' shaders compute. A console that draws on the GPU itself does so through its
backend's extension header ([`rr_metal_ext.h`](../include/nk/rr_metal_ext.h),
[`rr_gl_ext.h`](../include/nk/rr_gl_ext.h),
[`rr_vulkan_ext.h`](../include/nk/rr_vulkan_ext.h); the software backend's target,
[`rr_soft_ext.h`](../include/nk/rr_soft_ext.h)): the device, the frame's encoder or
command buffer, the target, the shared quad and sampler. Its pipelines, programs and
images are made in the open hook, `rr_set_open_hook(fn)`, which the backend calls at the
end of `rr_open`; `fn` returning 0 means the backend does not open. The console
registers the hook before `nk_main`, so a program must start through the console's own
main for the console's GPU work to be ready.

**Coroutines** ([`coro.h`](../include/nk/coro.h), [`jump.h`](../include/nk/jump.h),
[`src/coro/`](../src/coro/)). For processors that run beside the main one: each on a
stack of its own, one running at a time, on a deterministic schedule with no threads.
The switch is a few instructions of assembly on arm64 and x86-64, fibers on Windows.
`jump.h` is a non-local jump within one stack with no unwinding (on x86-64 with clang,
`_setjmp`/`_longjmp`, not the builtin pair, whose saved frame pointer a spill can
overwrite).

**Routines** ([`routines.h`](../include/nk/routines.h),
[`src/dispatch/routines.c`](../src/dispatch/routines.c)). `nk_routine_find(base, count,
stride, addr)`: the entry for an address in a table sorted by address, each entry's
first member its `uint32_t` address and the rest the console's. A console's dispatch
uses it to reach the C for a vector, a computed call or a jump.

**The resampler** ([`resample.h`](../include/nk/resample.h),
[`src/dsp/resample.c`](../src/dsp/resample.c)). Stereo, any rate to any rate, by a
32-tap Kaiser-windowed sinc, its ratio trimmed a fraction of a percent at a time so the
sound follows the console's pace without drifting.

**The hosts** ([`host.h`](../include/nk/host.h), [`src/host/`](../src/host/)). What each
operating system supplies: time, files and the save folder, a window, events, audio out,
game controllers. `posix` and `win32` give time and files; `null` (headless), `macos`,
`windows` and `ios` the rest. With host `none` an app supplies `host.h` itself. On iOS,
UIKit keeps the main thread and the game's main runs on a thread of its own as
`nk_game_main`, which the console's main macro defines.

**The fatal error** ([`fatal.h`](../include/nk/fatal.h)). The console defines
`nk_fatal(fmt, ...)`: its name before the message, and exit status 70. The kit's own
errors (a coroutine returning, a script's bad line, a save area past its RAM) reach it.

**Settings** ([`env.h`](../include/nk/env.h), [`src/front/env.c`](../src/front/env.c)).
Each of the kit's settings is read as `<prefix>_<NAME>`, the prefix the console's `env`,
so a console keeps its settings' names.

**The cost profile's fitter** ([`tools/nk_profile.py`](../tools/nk_profile.py)). The
fit a console's profile tool runs, over kinds the tool names: each kind's weight by
non-negative least squares (an exact search over which weights are not zero), the
error report, and a cartridge chip's programs fitted from its jobs (each program's fixed
part and op weights). The console's tool keeps its own command line, its reference runs
and its profile file.

## How a console uses it

A console is a library a game links, made of its own machine and the kit's objects. It
fills an `nk_console` and its main is `nk_main` with that table:

```c
const nk_console abc_console = {
    .name = "abc", .env = "ABC", .play_ext = ".play",
    .power = power, .run_frame = run_frame, .frame_ns = frame_ns, .app_name = app_name,
    .window_size = window_size, .aspect = aspect, .present = present,
    .shot = shot, .picture = picture,
    .audio_hz = 44100, .audio_take = audio_take, .audio_queued = audio_queued,
    .players = 2, .keys = keys, .key_count = 8,
    .buttons = buttons, .button_count = 8,
    .steps = steps, .step_count = 1, .script_step = script_step,
    .input = input,
    .regions = regions, .region_count = 3,
    .battery = battery, .counts_hooks = counts_hooks, .access_kind = access_kind,
    .kind_count = 4, .kind_names = kind_names,
};

int abc_main(const abc_cart *cart, int argc, char **argv)
{
    rr_set_open_hook(abc_gpu_open);      /* its own GPU work, if any */
    return nk_main(&abc_console, cart, argc, argv);
}
```

It defines `nk_fatal`, and keeps its `nk_battery`'s name never NULL (its own fallback).
Its `present(d)` draws the picture onto target rectangle `d`, with `rr_image` or its own
GPU work through the extension header. Its `rrcheck`, when it presents some frames its
own way, draws such a frame and gives the reference for it; otherwise the kit's generic
check holds the backend to the picture scaled on the CPU.

The kit's own checks run a minimal console:
[`tests/stub_console.c`](../tests/stub_console.c), whose machine is a frame counter
and a pseudo-random sequence the buttons stir, with three buttons, one step of its own
and four memories.

## The build

The kit is a set of CMake OBJECT libraries ([`CMakeLists.txt`](../CMakeLists.txt)) a
console folds into its own static library (`$<TARGET_OBJECTS:...>`), so a game links the
console's library alone:

| library | what it holds |
|---|---|
| `nk_coro` | the coroutines and the switch for the architecture built (one architecture a build) |
| `nk_resample` | the resampler |
| `nk_dispatch` | the routine table's lookup |
| `nk_host` | time and files, and the host `NK_HOST` names |
| `nk_front` | the settings, the battery save and the script |
| `nk_input` | the players' devices |
| `nk_app` | `nk_main`: the loop, the window, the pace, the sound out |
| `nk_render` | the backend `NK_RENDER` names; its frameworks or libraries are its link libraries, for the console's library to link |

`NK_HOST` (`null`, `macos`, `windows`, `ios`, `none`), `NK_RENDER` (`soft`, `metal`,
`gl`, `vulkan`), `NK_TESTS`, `NK_EXAMPLES`, and `NK_CONSOLES` are top-level options.
The top-level build chooses its default console and retains its legacy option aliases. The console links the host's own
frameworks and libraries. `NK_INCLUDE` is the kit's include path. The kit's version is
[`VERSION`](../VERSION).

## Settings

Read with the console's prefix (`<prefix>_NAME`):

| setting | what it does |
|---|---|
| `_STATES=DIR` | the plays (`*<play_ext>`) a `load` may replay to rebuild a state |
| `_SCRIPT_NOFORK=1` | `save` and `load` without `fork`, as on Windows |
| `_SCRIPT_PLAN`, `_SCRIPT_FRAMES`, `_SCRIPT_SAVED`, `_SCRIPT_RESUME` | the kit's own, for a copy of the program going on from a replay |
| `_RRCHECK=1` | headless: each frame through the renderer against the reference drawing, within 1 a channel |
| `_RRCHECK_WHY=1` | with it, the first differing frame's pixels, and the console's own account of them (`rrcheck_why`) |
| `_FPS=1`, `_FPS_LOG=FILE` | the frame-rate meter at the window's top left; a line a presented frame |
| `_SCALE=S` or `SXxSY` | headless: shots as a window that many pixels a console pixel shows them |

Read as they are, with no prefix: `RR_SIZE=WxH` (overrides the console-provided headless
size set with `rr_set_headless_size`), `PORT_SAVEDIR` and `PORT_RESDIR` (the save folder and the program's own
files), and the hosts' own `PORT_LOGFILE` (Windows), `PORT_NOACTIVATE` (macOS) and
`PORT_INPUTLOG` (iOS).

## The kit names no console

Nothing under `kit/` names a console, its chips or its reference, so that each console
is the kit's user and none is its owner. The configure fails when any file under
`kit/` (these documents included) holds one of the names listed in
[`tests/kit_names.cmake`](../tests/kit_names.cmake), matched without case. A console's
own terms stay in its own tree under `consoles/`.

## The kit's checks

`cmake --build <build> --target kit-check` (or `mise run test:kit`, on the headless
build) runs the name check and every check in [`tests/`](../tests/):

| check | holds |
|---|---|
| `loop_check` | frame-end quit and save timing; no headless save |
| `render_check` | console headless size, override and missing-size error |
| `kinds_check.py` | named and unnamed invalid access-kind diagnostics |
| `coro_check` | switching both ways, a coroutine's own stack, deep recursion on it |
| `fatal_check` | a coroutine that returns reaches the console's `nk_fatal` (exit 70) |
| `routines_check` | the lookup over tables of several strides, absent addresses |
| `env_check` | settings by prefix |
| `save_check` | the battery save loaded when there is one, written only when its save area changed, through a `.new` file renamed over the old; `--past`: a save area past the RAM, fatal |
| `input_check` | keys to positional bits and back to their names, a stick past half on the d-pad, a new press making a controller its player's source (reported once), the mouse's motion over a frame, two touches |
| `states_check.py` | the script on the stub console: steps and their errors, dumps, `save` and `load` with fork and without, a state rebuilt from a play |
| `profile_lib_check.py` | the fitter on made-up kinds: known weights given back, a kind not measured kept at its default, a chip's program fitted |
