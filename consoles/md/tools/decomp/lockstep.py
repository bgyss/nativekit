#!/usr/bin/env python3
"""Lockstep: every C routine substituted at once on the live reference machine (D05).

    lockstep.py --core CORE --rom ROM --scenario SCENARIO.json --frames N \\
        --library DIR/hybrid-live.dylib --out DIR [--hybrid-scenario OTHER.json]

Two runs of the pinned reference core from power-on with the same input, one
after the other, each in its own process:

  reference  the original, untouched. Every call of a routine the library
             has C for is cut from its events (as check_calls.py does) with
             the clock it began and ended at and the clock each of its
             hardware accesses began at. After every frame: a SHA-256 of each
             region of the comparison snapshot, of the picture and of the sound.
  hybrid     the same machine, but when a routine's entry is reached and the
             reference's matching call can be substituted, the C runs in its
             place: on the machine's own registers and memory, each hardware
             access made for real at the clock the original's began, and the
             CPU resumed at the C's return address at the clock the original
             returned. After every frame the same hashes, which must equal
             the reference's.

Nothing is played back into the C: what it reads is what the hybrid machine
holds. Only the schedule is the reference's. A 68000 call the original was
interrupted in, took a TRAP in or ran past a frame's end in is substituted:
its C pauses there, the machine takes the interrupt or TRAP (or ends the
frame), and the C goes on where the handler returns. A call is left to the
original (and counted, by reason) when it took an exception other than a
TRAP, when the 68000 wrote Z80 RAM during a Z80
call, when it did not return by its own rts/ret, when the run ended before it
returned, when it calls a routine that has no C, or when it is a Z80 call
the original paused inside at the end of a Z80 run slice. A run with such calls is not certified.
"""
import argparse
import bisect
import ctypes as c
import hashlib
import json
import pathlib
import pickle
import sys
import time

TOOLS = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import check_calls  # noqa: E402
import hybrid  # noqa: E402
import run as runner  # noqa: E402

SCHEMA = "nmd-lockstep"
VERSION = 1
M68K, Z80 = check_calls.M68K, check_calls.Z80
UNTIMED = (1 << 64) - 1
LIVE_STATUS = {1: "a hardware access out of the original's order",
               2: "a hardware access after the original's last",
               3: "a word access at an odd address",
               4: "a wait, restart or call the live machine cannot serve",
               5: "the original took an exception the C did not"}


Z80_FETCH = 4 * 15     # one opcode fetch, in master clocks (patch 0005's live resume)


class LockstepError(RuntimeError):
    """The lockstep run could not be made."""


class PausePoint(c.Structure):
    _fields_ = [("clock", c.c_uint64), ("pc", c.c_uint32), ("d", c.c_uint32 * 8),
                ("a", c.c_uint32 * 8), ("ccr", c.c_uint32), ("kind", c.c_uint32),
                ("syncs_before", c.c_uint32), ("index", c.c_uint32 * 3)]


class SyncPoint(c.Structure):
    _fields_ = [("clock", c.c_uint64), ("refresh_sync", c.c_uint64),
                ("refresh_counter", c.c_uint32)]


class Slot(c.Structure):
    _fields_ = [("clock", c.c_uint64), ("refresh_sync", c.c_uint64), ("address", c.c_uint32),
                ("refresh_counter", c.c_uint32), ("prefetch", c.c_uint32), ("width", c.c_uint8),
                ("write", c.c_uint8)]


class LiveResult(c.Structure):
    _fields_ = [("status", c.c_int), ("address", c.c_uint32), ("width", c.c_uint32),
                ("write", c.c_int), ("slot", c.c_uint32), ("slots_used", c.c_uint32),
                ("accesses", c.c_uint32), ("hardware", c.c_uint32)]


SUBSTITUTE = c.CFUNCTYPE(c.c_int, c.c_uint32, c.c_uint32, c.c_uint64)


class Machine:
    """The reference core, driven a frame at a time."""

    def __init__(self, core_path, rom_path, scenario, directory, on_event=None):
        self.core = core = c.CDLL(str(pathlib.Path(core_path).resolve()))
        self.errors = []
        self.inputs = {}
        self.keep_picture = False       # keep the last frame's pixels in self.picture
        self.picture = None
        self.frames_run = 0
        # coverage_writes (run.py): work RAM writes before a frame runs
        self.coverage = {int(frame): items
                         for frame, items in scenario.get("coverage_writes", {}).items()}
        self.video = hashlib.sha256()
        self.audio = hashlib.sha256()
        options = {
            b"blastem_system_type": b"gen",
            b"blastem_model": scenario["model"].encode("ascii"),
            b"blastem_region": scenario["region"].encode("ascii"),
            b"blastem_force_region": b"on", b"blastem_ram_init": b"zero",
            b"blastem_m68k_divider": b"7", b"blastem_megawifi": b"off",
            b"blastem_io_1": ("gamepad" + scenario["pad1"] + ".1").encode("ascii"),
            b"blastem_io_2": ("gamepad" + scenario["pad2"] + ".2").encode("ascii"),
            b"blastem_lowpass_cutoff": b"24000", b"blastem_gain": b"0.0",
            b"blastem_fm_gain": b"0.0", b"blastem_psg_gain": b"0.0",
            b"blastem_fm_dac": b"zero_offset", b"blastem_overscan_top": b"0",
            b"blastem_overscan_bottom": b"0", b"blastem_overscan_left": b"0",
            b"blastem_overscan_right": b"0",
        }
        directory = str(pathlib.Path(directory).resolve()).encode()
        pixel_format = [None]

        @c.CFUNCTYPE(c.c_bool, c.c_uint, c.c_void_p)
        def environment(command, data):
            if command in (9, 31):
                c.cast(data, c.POINTER(c.c_char_p))[0] = directory
                return True
            if command == 3:
                c.cast(data, c.POINTER(c.c_bool))[0] = False
                return True
            if command == 10:
                pixel_format[0] = c.cast(data, c.POINTER(c.c_uint))[0]
                return pixel_format[0] == 1
            if command == 15:
                variable = c.cast(data, c.POINTER(runner.Variable)).contents
                value = options.get(variable.key)
                if value is None:
                    return False
                variable.value = value
                return True
            return False

        @c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t)
        def video(data, width, height, pitch):
            if not data or pixel_format[0] != 1:
                self.errors.append("missing or unsupported video frame")
                return
            rows = [c.string_at(data + y * pitch, width * 4) for y in range(height)]
            for row in rows:
                self.video.update(row)
            if self.keep_picture:
                self.picture = (width, height, b"".join(rows))

        @c.CFUNCTYPE(c.c_size_t, c.POINTER(c.c_int16), c.c_size_t)
        def audio_batch(data, frames):
            if frames and data:
                self.audio.update(c.string_at(data, frames * 4))
            return frames

        @c.CFUNCTYPE(None, c.c_int16, c.c_int16)
        def audio_sample(left, right):
            self.audio.update(int(left).to_bytes(2, sys.byteorder, signed=True))
            self.audio.update(int(right).to_bytes(2, sys.byteorder, signed=True))

        @c.CFUNCTYPE(None)
        def input_poll():
            pass

        @c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint)
        def input_state(port, device, index, button):
            return int(device == 1 and index == 0 and button in self.inputs.get(port, set()))

        @c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(runner.DecompEvent))
        def on_decomp(user, pointer):
            if on_event is not None:
                try:
                    on_event(runner._event_dict(pointer.contents))
                except Exception as exc:    # a ctypes callback cannot raise
                    self.errors.append("event handling failed: %r" % exc)
            return 0

        self._keep = (environment, video, audio_batch, audio_sample, input_poll, input_state,
                      on_decomp)
        for name, callback in (("retro_set_environment", environment),
                               ("retro_set_video_refresh", video),
                               ("retro_set_audio_sample", audio_sample),
                               ("retro_set_audio_sample_batch", audio_batch),
                               ("retro_set_input_poll", input_poll),
                               ("retro_set_input_state", input_state)):
            getattr(core, name)(callback)
        core.retro_init()
        rom = pathlib.Path(rom_path).read_bytes()
        self._rom = c.create_string_buffer(rom)
        game = runner.Game(None, c.cast(self._rom, c.c_void_p), len(rom), None)
        info = runner.SystemInfo()
        core.retro_get_system_info(c.byref(info))
        if info.need_fullpath:
            game = runner.Game(str(pathlib.Path(rom_path).resolve()).encode(), None, 0, None)
        if not core.retro_load_game(c.byref(game)):
            raise LockstepError("the core refused the cartridge")
        core.nmd_decomp_register.argtypes = [
            c.c_uint32, c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(runner.DecompEvent)),
            c.c_void_p]
        core.nmd_decomp_register.restype = c.c_int
        core.nmd_decomp_reset()
        if not core.nmd_decomp_register(1, on_decomp, None):
            raise LockstepError("the core refused NMDH ABI version 1")
        try:
            core.nmd_decomp_snapshot.argtypes = [c.c_uint32, c.c_void_p, c.c_size_t,
                                                 c.c_char_p, c.c_size_t]
            core.nmd_decomp_snapshot.restype = c.c_size_t
        except AttributeError as exc:
            raise LockstepError("the core has no live substitution (patch 0005)") from exc
        self._buffer = c.create_string_buffer(1 << 17)
        self._name = c.create_string_buffer(32)

    def close(self):
        """Unload the cartridge and shut the core down, so another Machine can
        use the same library in this process."""
        self.core.nmd_decomp_reset()
        self.core.retro_unload_game()
        self.core.retro_deinit()

    def _write_ram(self, address, width, value):
        """A coverage write: the core keeps work RAM as host-endian words."""
        self.core.retro_get_memory_data.restype = c.c_void_p
        base = self.core.retro_get_memory_data(2)       # RETRO_MEMORY_SYSTEM_RAM
        if not base:
            raise LockstepError("the core does not expose work RAM")
        ram = (c.c_uint8 * 0x10000).from_address(base)
        offset = address & 0xFFFF
        if width == 2:
            ram[offset], ram[offset + 1] = value & 0xFF, value >> 8
        else:
            ram[offset ^ 1] = value

    def frame(self, inputs, snapshots=None):
        """Run one frame; the hashes of its picture, sound and snapshot regions."""
        self.inputs = inputs
        self.video, self.audio = hashlib.sha256(), hashlib.sha256()
        self.frames_run += 1
        for item in self.coverage.get(self.frames_run, ()):
            self._write_ram(item["address"], item["width"], item["value"])
        self.core.retro_run()
        if self.core.nmd_decomp_error():
            self.errors.append("the core reported an NMDH error")
        hashes = {"video": self.video.hexdigest(), "audio": self.audio.hexdigest()}
        region = 0
        while True:
            size = self.core.nmd_decomp_snapshot(region, self._buffer, len(self._buffer),
                                                 self._name, len(self._name))
            if not size:
                break
            name = self._name.value.decode()
            data = self._buffer.raw[:size]
            hashes[name] = hashlib.sha256(data).hexdigest()
            if snapshots is not None and name == "work_ram":
                snapshots[name] = data
            region += 1
        if region == 0:
            self.errors.append("the core gave no comparison snapshot")
        return hashes


def _schedule_after(state):
    """The 68000's next sync, next interrupt and slice end, in clocks after this state's."""
    interrupt = state["int_cycle"]
    return (state["sync_cycle"] - state["cycles"],
            (1 << 63) - 1 if interrupt == 0xFFFFFFFF else interrupt - state["cycles"],
            state["target_cycle"] - state["cycles"], state.get("slice_after", 0),
            state["int_pending"] | state["int_pending_num"] << 8)


def _hardware(cpu, address, rom_size):
    if cpu == M68K:
        address &= 0xFFFFFF
        return not (address < rom_size or address >= 0xE00000)
    return address >= 0x2000


def _reference(args, scenario, routines, rom_size):
    """The original's frames and call schedule."""
    frame = [0]
    schedule = {key: [] for key in routines}
    entries = {key: [] for key in routines}     # the clock at each entry
    feed = check_calls.segmenter(set(routines), lambda: frame[0])
    core = [None]
    counter, last_sync = c.c_uint32(), c.c_uint64()
    last_slice = [None]                         # the Z80 run slice the last instruction ran in
    stop_at = [None]                            # where the Z80's last run stopped
    syncs, last_syncs = [], [0]                 # (instruction sequence, clock) per sync

    def refresh(at_access_start):
        core[0].nmd_decomp_refresh_state(at_access_start, c.byref(counter), c.byref(last_sync))
        return counter.value, last_sync.value

    def on_event(event):
        # the machine's refresh counter rides along with the 68000's events: as it stood
        # when a bus access began, and as it stands after an instruction
        if event["cpu"] == M68K and event["kind"] == 3:
            event["payload"]["extra"] = refresh(1) + (core[0].nmd_decomp_m68k_prefetch(),)
        elif event["cpu"] == M68K and event["kind"] == 1:
            event["payload"]["state"]["refresh"] = refresh(0)
            count = core[0].nmd_decomp_m68k_loop_syncs()
            if count != last_syncs[0]:          # synced before this instruction
                core[0].nmd_decomp_m68k_loop_sync_refresh(c.byref(counter), c.byref(last_sync))
                syncs.append((event["sequence"], (core[0].nmd_decomp_m68k_loop_sync_clock(),
                                                  last_sync.value, counter.value)))
                last_syncs[0] = count
            event["payload"]["state"]["slice_after"] = (
                core[0].nmd_decomp_m68k_slice_end() - event["master_clock"])
        z80_slice = None
        if event["cpu"] == Z80 and event["kind"] == 1:
            z80_slice = core[0].nmd_decomp_z80_slice_end()
            if z80_slice != last_slice[0]:
                stop_at[0] = core[0].nmd_decomp_z80_run_end()   # where the last run stopped
        closed = feed(event)
        straddled = None                # this instruction ended the Z80's last run
        if z80_slice is not None:
            # an instruction's event comes at the next opcode fetch: the first one in a
            # new run is of the instruction that ended the last run
            if last_slice[0] is not None and z80_slice != last_slice[0]:
                feed.slice_end(stop_at[0])
                straddled = stop_at[0]
            last_slice[0] = z80_slice
        for call in closed:
            schedule[(call.cpu, call.address)].append({
                "sequence": call.sequence, "entry_clock": call.entry_clock,
                "frame": call.frame_entered,
                "exit_clock": call.exit_clock, "reasons": sorted(call.reasons),
                # a Z80 call whose return ended a run: where that run stopped
                "exit_stop": straddled if call.cpu == Z80 else None,
                "hardware": [(clock, address & 0xFFFFFF, width, int(write), extra)
                             for address, width, _, write, clock, extra, _depth in call.accesses
                             if _hardware(call.cpu, address, rom_size)],
                "exit_refresh": call.exit_state.get("refresh", (0, 0)),
                "exit_registers": (check_calls.m68k_registers(call.exit_state, call.exit["pc"])
                                   if call.cpu == M68K else None),
                "pauses": [{"kind": pause["kind"], "clock": pause["clock"], "pc": pause["pc"],
                            "registers": check_calls.REGISTERS[call.cpu](pause["state"],
                                                                         pause["pc"]),
                            "refresh": pause["state"].get("refresh", (0, 0)),
                            "schedule": (_schedule_after(pause["state"])
                                         if call.cpu == M68K else None),
                            "r": pause["state"].get("r", 0), "wz": pause["state"].get("wz", 0),
                            "stop": pause.get("stop") or pause["clock"],
                            "span": (pause["sequence"], pause["end_sequence"])
                            if pause["kind"] in ("interrupt", "exception") else None,
                            # the events up to the pause: a frame's last, or the interrupt
                            "boundary": pause.get("boundary") or pause["sequence"]}
                           for pause in call.pauses],
                "exit_sequence": call.exit_sequence,
                "exit_schedule": _schedule_after(call.exit_state) if call.cpu == M68K else None,
                "tree": [address for address, _ in call.tree],
                "r": call.exit_state.get("r", 0), "wz": call.exit_state.get("wz", 0)})

    machine = Machine(args.core, args.rom, scenario, args.out, on_event)
    core[0] = machine.core
    machine.core.nmd_decomp_m68k_prefetch.restype = c.c_uint32
    machine.core.nmd_decomp_m68k_slice_end.restype = c.c_uint64
    machine.core.nmd_decomp_m68k_loop_syncs.restype = c.c_uint64
    machine.core.nmd_decomp_m68k_loop_sync_clock.restype = c.c_uint64
    machine.core.nmd_decomp_z80_slice_end.restype = c.c_uint64
    machine.core.nmd_decomp_z80_run_end.restype = c.c_uint64

    # the original is never replaced here: the handler only notes when each entry is reached
    def note(cpu, pc, clock):
        entries[(cpu, pc)].append(clock)
        return 0

    handler = SUBSTITUTE(note)
    _register(machine.core, handler, routines)
    inputs = runner._load_inputs(scenario, args.frames)
    frames = []
    snapshots = {}
    for number in range(1, args.frames + 1):
        frame[0] = number
        frames.append(machine.frame(inputs.get(number, {}),
                                    snapshots if number == args.inspect_frame else None))
        feed.frame_end()
        feed.slice_end()                        # the Z80 stopped where the frame ended
        if machine.errors:
            raise LockstepError("reference: " + "; ".join(machine.errors))
    # The core reports every arrival at an entry address, a loop back to the entry
    # included. Each call is matched to its first arrival; arrivals inside a call are its
    # loop passes, which the hybrid ignores; arrivals after the last call that returned
    # are calls still open at the end (the first of them) and their loop passes.
    sync_sequences = [sequence for sequence, _ in syncs]    # in event order: sorted
    loops = {key: set() for key in routines}
    entered = {}
    for key, rows in schedule.items():
        rows.sort(key=lambda row: row["sequence"])     # entry order: nested calls close first
        arrivals = list(entries[key])
        position = 0
        for row in rows:
            # the entry's own clock: a sync before the instruction can move it past the
            # clock of the instruction before
            while position < len(arrivals) and arrivals[position] < row["entry_clock"]:
                loops[key].add(arrivals[position])
                position += 1
            if position == len(arrivals) or arrivals[position] > row["entry_clock"] + 4096:
                raise LockstepError("reference: the entries of $%06X do not line up with its "
                                    "calls" % key[1])
            row["entry_clock"] = arrivals[position]
            position += 1
            while position < len(arrivals) and arrivals[position] <= row["exit_clock"]:
                loops[key].add(arrivals[position])
                position += 1
            row["slots"] = [(start, (extra or (0, 0, 0))[1], address, (extra or (0, 0, 0))[0],
                             (extra or (0, 0, 0))[2], width, write)
                            for start, address, width, write, extra in row.pop("hardware")]
        for row in rows:                        # syncs strictly inside the call (68000)
            last = row.pop("exit_sequence")
            low = bisect.bisect_right(sync_sequences, row["sequence"])
            high = bisect.bisect_right(sync_sequences, last)
            spans = [pause["span"] for pause in row["pauses"] if pause["span"]]
            # a handler's syncs happen on their own when the machine runs it
            inside = [(sequence, clock) for sequence, clock in syncs[low:high]
                      if not any(first < sequence <= (end or last) for first, end in spans)
                      ] if key[0] == M68K else []
            row["syncs"] = [clock for _, clock in inside]
            # the syncs the original made before each pause, in event order: a sync at
            # a frame's last clock can belong to the next frame
            for pause in row["pauses"]:
                pause["syncs_before"] = sum(1 for sequence, _ in inside
                                            if sequence <= pause["boundary"])
        leftover = arrivals[position:]
        loops[key].update(leftover[1:])
        entered[key] = len(rows) + (1 if leftover else 0)
    return {"snapshots": snapshots, "frames": frames, "schedule": schedule, "entered": entered, "loops": loops}


def _register(core, handler, routines):
    m68k = [address for cpu, address in routines if cpu == M68K]
    z80 = [address for cpu, address in routines if cpu == Z80]
    core.nmd_decomp_set_substitute.argtypes = [SUBSTITUTE, c.POINTER(c.c_uint32), c.c_uint32,
                                               c.POINTER(c.c_uint32), c.c_uint32]
    if core.nmd_decomp_set_substitute(handler, (c.c_uint32 * max(1, len(m68k)))(*m68k), len(m68k),
                                      (c.c_uint32 * max(1, len(z80)))(*z80), len(z80)):
        raise LockstepError("the core refused the routine addresses")


def _pause_point(cpu, pause):
    registers = pause["registers"]
    if cpu == Z80:
        return PausePoint(pause["clock"], pause["pc"],
                          (c.c_uint32 * 8)(*[registers[name] for name in "a f b c d e h l".split()]),
                          (c.c_uint32 * 8)(*[registers[name + "2"]
                                             for name in "a f b c d e h l".split()]),
                          0, 0, 0, (c.c_uint32 * 3)(registers["ix"], registers["iy"],
                                                    registers["sp"]))
    return PausePoint(pause["clock"], pause["pc"],
                      (c.c_uint32 * 8)(*[registers["d%d" % i] for i in range(8)]),
                      (c.c_uint32 * 8)(*[registers["a%d" % i] for i in range(8)]),
                      registers["sr"] & 0x1F, 1 if pause["kind"] == "exception" else 0,
                      pause["syncs_before"], (c.c_uint32 * 3)())


def _is_trap(image, pc):
    return pc + 2 <= len(image) and image[pc] == 0x4E and image[pc + 1] & 0xF0 == 0x40


def _hybrid(args, scenario, routines, rom_size, reference):
    """The same run with the C substituted; the result's body."""
    image = pathlib.Path(args.rom).read_bytes()
    library = c.CDLL(str(pathlib.Path(args.library).resolve()))
    library.nmd_live_start.argtypes = [c.c_int, c.c_void_p, c.c_uint32, c.c_void_p, c.c_uint32,
                                       c.c_void_p, c.c_uint32, c.c_void_p, c.POINTER(c.c_int)]
    library.nmd_live_result_of.restype = c.POINTER(LiveResult)
    library.nmd_live_syncs_made.restype = c.c_uint32
    library.nmd_live_pauses_made.restype = c.c_uint32
    cpu_state = check_calls.CpuState.in_dll(library, "nmd_cpu")
    z80_state = check_calls.Z80State.in_dll(library, "nmd_z80")
    schedule = reference["schedule"]
    taken = dict.fromkeys(routines, 0)
    substituted = dict.fromkeys(routines, 0)
    paused_counts = {}
    skipped = {key: {} for key in routines}
    totals = {"accesses": 0, "hardware_accesses": 0}
    divergence = []
    machine = Machine(args.core, args.rom, scenario, args.out)
    core = machine.core
    core.nmd_decomp_m68k_live_state.argtypes = [c.POINTER(runner.M68KState)]
    core.nmd_decomp_z80_live_state.argtypes = [c.POINTER(runner.Z80State)]
    core.nmd_decomp_z80_slice_end.restype = c.c_uint64
    core.nmd_decomp_m68k_live_resume.argtypes = [
        c.POINTER(c.c_uint32), c.POINTER(c.c_uint32), c.c_uint32, c.c_uint32, c.c_uint32,
        c.c_uint64, c.c_uint32, c.c_uint64, c.c_int64, c.c_int64, c.c_int64, c.c_int64,
        c.c_uint32]
    core.nmd_decomp_z80_live_resume.argtypes = [c.POINTER(runner.Z80State), c.c_uint32,
                                                c.c_uint64]
    core.nmd_decomp_m68k_live_sync.argtypes = [c.c_uint64, c.c_uint32, c.c_uint64]
    live_sync = c.cast(core.nmd_decomp_m68k_live_sync, c.c_void_p)
    library.nmd_live_bind(c.cast(core.nmd_decomp_m68k_live_access, c.c_void_p),
                          c.cast(core.nmd_decomp_z80_live_access, c.c_void_p), live_sync, rom_size)
    core.nmd_decomp_continuation.argtypes = [c.c_uint32, c.c_uint32, c.c_int]
    continuations = {}          # interrupted PC -> the paused call to continue there
    current = [0]

    def diverge(**detail):
        if not divergence:
            divergence.append(dict(detail, frame=current[0]))

    def substitute(cpu, pc, clock):
        key = (cpu, pc)
        if (cpu, pc) in continuations:
            return continue_call(cpu, pc)
        if clock in reference["loops"][key]:
            return 0            # the original's loop back to the entry, inside a call it runs
        index = taken[key]
        taken[key] += 1
        name = routines[key][1]
        if len(schedule[key]) <= index < reference["entered"][key]:
            # the original entered it too, and the run ended before it returned
            skipped[key]["unfinished"] = skipped[key].get("unfinished", 0) + 1
            return 0
        if index >= len(schedule[key]):
            diverge(kind="call", what="a call the original did not make", cpu=check_calls.CPU_NAMES[cpu],
                    address=pc, name=name, call=index + 1)
            return 0
        record = schedule[key][index]
        if record["entry_clock"] != clock:
            diverge(kind="call", what="entered at another clock", cpu=check_calls.CPU_NAMES[cpu],
                    address=pc, name=name, call=index + 1, expected=record["entry_clock"],
                    got=clock)
            return 0
        reason = record["reasons"][0] if record["reasons"] else (
            "fallback" if any((cpu, address) not in routines for address in record["tree"])
            else None)
        if cpu == Z80 and record["pauses"] and set(record["reasons"]) <= {"spans_frames"} and \
                all(pause["kind"] == "slice" for pause in record["pauses"]) and \
                not any((cpu, address) not in routines for address in record["tree"]):
            reason = None       # the C pauses where the original's run slices ended
        elif reason is None and cpu == Z80 and \
                record["exit_clock"] > core.nmd_decomp_z80_slice_end():
            # the original Z80 paused inside this call while the 68000 ran: run all at
            # once, its accesses would come before the 68000's that the original's followed
            reason = "spans_slice"
        if reason in ("interrupt", "spans_frames", "exception") and cpu == M68K and \
                set(record["reasons"]) <= {"interrupt", "spans_frames", "exception"} and \
                all(pause["kind"] in ("interrupt", "frame") or
                    pause["kind"] == "exception" and _is_trap(image, pause["pc"])
                    for pause in record["pauses"]) and \
                not any((cpu, address) not in routines for address in record["tree"]):
            # the C pauses where the original was interrupted, the frame ended or it
            # took a TRAP
            reason = None
        if reason:
            skipped[key][reason] = skipped[key].get(reason, 0) + 1
            return 0
        if cpu == M68K:
            state = runner.M68KState()
            core.nmd_decomp_m68k_live_state(c.byref(state))
            registers = check_calls.m68k_registers(runner._state_dict(state), pc)
            for position in range(8):
                cpu_state.d[position] = registers["d%d" % position]
                cpu_state.a[position] = registers["a%d" % position]
            cpu_state.sr, cpu_state.pc = registers["sr"], pc
            cpu_state.usp = cpu_state.ssp = registers["other_sp"]
        else:
            state = runner.Z80State()
            core.nmd_decomp_z80_live_state(c.byref(state))
            for field, value in check_calls.z80_registers(runner._state_dict(state), pc).items():
                setattr(z80_state, field, value)
        context = {
            "cpu": cpu, "pc": pc, "key": key, "name": name, "index": index, "record": record,
            "state": state,
            "slots": (Slot * max(1, len(record["slots"])))(*record["slots"]),
            "syncs": (SyncPoint * max(1, len(record["syncs"])))(*record["syncs"]),
            "pauses": (PausePoint * max(1, len(record["pauses"])))(
                *[_pause_point(cpu, pause) for pause in record["pauses"]]),
            "handle": c.c_int(-1),
        }
        status = library.nmd_live_start(
            cpu, context["slots"], len(record["slots"]), context["syncs"], len(record["syncs"]),
            context["pauses"], len(record["pauses"]), routines[key][0], c.byref(context["handle"]))
        if status < 0:
            raise LockstepError("too many substituted calls open at once")
        return after_run(context, status)

    def continue_call(cpu, pc):
        """The handler returned to where the original was interrupted (or the Z80's
        next run slice reached where its last ended): the C goes on, with the
        registers the machine holds now (the handler restored them)."""
        context = continuations.pop((cpu, pc))
        core.nmd_decomp_continuation(cpu, pc, 0)
        if cpu == Z80:
            state = runner.Z80State()
            core.nmd_decomp_z80_live_state(c.byref(state))
            for field, value in check_calls.z80_registers(runner._state_dict(state), pc).items():
                setattr(z80_state, field, value)
            if context["at_end"]:
                z80_state.sp = (z80_state.sp + 2) & 0xFFFF     # the C had already returned
                z80_state.pc = context["return_pc"]
            return after_run(context, library.nmd_live_continue(context["handle"].value))
        state = runner.M68KState()
        core.nmd_decomp_m68k_live_state(c.byref(state))
        registers = check_calls.m68k_registers(runner._state_dict(state), pc)
        for position in range(8):
            cpu_state.d[position] = registers["d%d" % position]
            cpu_state.a[position] = registers["a%d" % position]
        cpu_state.sr = registers["sr"]
        if context["at_end"]:
            cpu_state.a[7] += 4     # the C had already returned
            cpu_state.pc = context["return_pc"]
        return after_run(context, library.nmd_live_continue(context["handle"].value))

    def after_run(context, status):
        cpu, pc, key, name = context["cpu"], context["pc"], context["key"], context["name"]
        index, record, handle = context["index"], context["record"], context["handle"].value
        result = library.nmd_live_result_of(handle).contents
        if status == 1 and cpu == Z80:  # paused where the original's run slice ended
            pause = record["pauses"][library.nmd_live_pauses_made(handle) - 1]
            context["at_end"] = library.nmd_live_at_end(handle)
            context["return_pc"] = z80_state.pc
            # where the original's run stopped: the core sets the Z80's clock one
            # opcode fetch (4 T-states) before the clock given, since its run loop
            # fetches the opcode at pc again
            resume_z80(context, pause["pc"], min(pause["stop"] + Z80_FETCH, pause["clock"]),
                       pause["r"], pause["wz"],
                       (z80_state.sp - 2) & 0xFFFF if context["at_end"] else z80_state.sp)
            if (cpu, pause["pc"]) in continuations or \
                    core.nmd_decomp_continuation(Z80, pause["pc"], 1):
                raise LockstepError("cannot continue a paused Z80 call at $%04X" % pause["pc"])
            continuations[(cpu, pause["pc"])] = context
            paused_counts[key] = paused_counts.get(key, 0) + 1
            return 1
        if status == 1:             # paused where the original took an interrupt
            pause = record["pauses"][library.nmd_live_pauses_made(handle) - 1]
            context["at_end"] = library.nmd_live_at_end(handle)
            context["return_pc"] = cpu_state.pc
            a7 = cpu_state.a[7] - (4 if context["at_end"] else 0)
            address_registers = (c.c_uint32 * 8)(*[cpu_state.a[i] for i in range(7)], a7)
            other = cpu_state.usp if cpu_state.sr & 0x2000 else cpu_state.ssp
            core.nmd_decomp_m68k_live_resume(cpu_state.d, address_registers, other, cpu_state.sr,
                                             pause["pc"], pause["clock"], *pause["refresh"],
                                             *pause["schedule"])
            # the machine goes on at the paused instruction; the C, where the handler
            # returns: the same instruction after an interrupt, the one after a TRAP
            back = pause["pc"] + 2 if pause["kind"] == "exception" else pause["pc"]
            if (cpu, back) in continuations or core.nmd_decomp_continuation(M68K, back, 1):
                raise LockstepError("cannot continue a paused call at $%06X" % back)
            continuations[(cpu, back)] = context
            paused_counts[key] = paused_counts.get(key, 0) + 1
            return 1
        totals["accesses"] += result.accesses
        totals["hardware_accesses"] += result.hardware
        if status == 2 or result.slots_used != len(record["slots"]):
            what = LIVE_STATUS.get(result.status, "a hardware access the original made is missing")
            expected = record["slots"][result.slot] if result.slot < len(record["slots"]) else None
            expected = expected and (expected[0], expected[2], expected[5], expected[6])
            diverge(kind="hardware", what=what, cpu=check_calls.CPU_NAMES[cpu], address=pc,
                    name=name, call=index + 1, record_position=result.slot,
                    access={"address": result.address, "width": result.width,
                            "write": bool(result.write)},
                    expected=expected and {"address": expected[1], "width": expected[2],
                                           "write": bool(expected[3])})
            library.nmd_live_release(handle)
            core.nmd_decomp_fail()      # the machine holds half a routine: stop the run
            return 0
        library.nmd_live_release(handle)
        if cpu == M68K:             # the C's registers at its return, against the original's
            got = {"d%d" % i: cpu_state.d[i] for i in range(8)}
            got.update({"a%d" % i: cpu_state.a[i] for i in range(8)})
            got.update(sr=cpu_state.sr, pc=cpu_state.pc)
            for register, value in got.items():
                expected = record["exit_registers"][register]
                if value != expected:
                    diverge(kind="register", what="the C returned another value",
                            cpu=check_calls.CPU_NAMES[cpu], address=pc, name=name, call=index + 1,
                            register=register, expected=expected, got=value)
                    core.nmd_decomp_fail()
                    return 0
        for address in record["tree"]:
            taken[(cpu, address)] += 1  # the C ran these inside this call
        if cpu == M68K:
            other = cpu_state.usp if cpu_state.sr & 0x2000 else cpu_state.ssp
            core.nmd_decomp_m68k_live_resume(cpu_state.d, cpu_state.a, other, cpu_state.sr,
                                             cpu_state.pc, record["exit_clock"],
                                             *record["exit_refresh"], *record["exit_schedule"])
        else:
            exit_clock = record["exit_clock"]
            if record.get("exit_stop") is not None:     # see the slice pauses' resume
                exit_clock = min(record["exit_stop"] + Z80_FETCH, exit_clock)
            resume_z80(context, z80_state.pc, exit_clock, record["r"], record["wz"],
                       z80_state.sp)
        substituted[key] += 1
        return 1

    def resume_z80(context, pc, clock, r, wz, sp):
        """The Z80 goes on at `pc` and `clock` with the C's registers."""
        state = context["state"]
        flags = z80_state.f
        state.main[:] = [z80_state.b, z80_state.c, z80_state.d, z80_state.e, z80_state.h,
                         z80_state.l, flags, z80_state.a]
        state.alt[:] = [z80_state.b2, z80_state.c2, z80_state.d2, z80_state.e2,
                        z80_state.h2, z80_state.l2, z80_state.f2, z80_state.a2]
        state.ix, state.iy, state.sp, state.i = z80_state.ix, z80_state.iy, sp, z80_state.i
        state.iff1, state.iff2, state.imode = z80_state.iff1, z80_state.iff2, z80_state.im
        state.last_flag_result = flags & 0xA8
        state.zflag, state.pvflag, state.nflag = (bool(flags & 0x40), bool(flags & 0x04),
                                                  bool(flags & 0x02))
        state.chflags = (0x08 if flags & 0x10 else 0) | (0x80 if flags & 0x01 else 0)
        state.r, state.wz = r, wz
        core.nmd_decomp_z80_live_resume(c.byref(state), pc, clock)

    def guarded(cpu, pc, clock):
        try:
            return substitute(cpu, pc, clock)
        except Exception as exc:    # a ctypes callback cannot raise
            machine.errors.append("substitution failed: %r" % exc)
            core.nmd_decomp_fail()
            return 0

    handler = SUBSTITUTE(guarded)
    _register(core, handler, routines)
    inputs = runner._load_inputs(scenario, args.frames)
    compared = 0
    for number in range(1, args.frames + 1):
        current[0] = number
        snapshot = {} if number == args.inspect_frame else None
        hashes = machine.frame(inputs.get(number, {}), snapshot)
        if divergence:
            break
        if machine.errors:
            raise LockstepError("hybrid: " + "; ".join(machine.errors))
        expected = reference["frames"][number - 1]
        regions = sorted(name for name in expected if hashes.get(name) != expected[name])
        if regions:
            detail = {}
            if snapshot is not None and "work_ram" in regions:
                expected_ram = reference["snapshots"]["work_ram"]
                actual_ram = snapshot["work_ram"]
                for offset in range(0, len(expected_ram), 2):
                    expected_word = int.from_bytes(expected_ram[offset:offset + 2], sys.byteorder)
                    actual_word = int.from_bytes(actual_ram[offset:offset + 2], sys.byteorder)
                    if expected_word != actual_word:
                        detail.update(word_address=0xff0000 + offset, expected_word=expected_word,
                                      actual_word=actual_word)
                        break
            diverge(kind="frame", what="state after the frame differs", regions=regions, **detail)
            break
        compared += 1
    for key in routines:        # calls the original made that the hybrid never reached
        if not divergence and taken[key] != reference["entered"][key]:
            diverge(kind="call", what="calls the original made are missing",
                    cpu=check_calls.CPU_NAMES[key[0]], address=key[1], name=routines[key][1],
                    expected=reference["entered"][key], got=taken[key])
    return {
        "frames_compared": compared, "divergence": divergence[0] if divergence else None,
        "totals": dict(totals, pauses=sum(paused_counts.values())),
        "routines": [{"cpu": check_calls.CPU_NAMES[key[0]], "address": key[1],
                      "name": routines[key][1], "calls": len(schedule[key]),
                      "substituted": substituted[key], "left_to_original": skipped[key]}
                     for key in sorted(routines)],
    }


def _worker(args):
    scenario = runner._validate_scenario(
        json.loads(pathlib.Path(args.scenario).read_text()), args.frames)
    library = c.CDLL(str(pathlib.Path(args.library).resolve()))
    routines = {(M68K if cpu == "m68k" else Z80, address): (function, name)
                for cpu, address, function, name in hybrid.tables(library)}
    rom_size = pathlib.Path(args.rom).stat().st_size
    out = pathlib.Path(args.out)
    if args.worker == "reference":
        with (out / "reference.pickle").open("wb") as stream:
            pickle.dump(_reference(args, scenario, routines, rom_size), stream)
    else:
        with (out / "reference.pickle").open("rb") as stream:
            reference = pickle.load(stream)
        (out / "hybrid.json").write_text(json.dumps(
            _hybrid(args, scenario, routines, rom_size, reference)))


def _sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def run(core, rom, scenario, frames, library, out, hybrid_scenario=None, timeout=600, inspect_frame=None):
    """Both runs; return (and write) the lockstep report."""
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": SCHEMA, "version": VERSION, "frames": frames,
        "identities": {
            "rom_sha256": _sha256(rom), "core_sha256": _sha256(core),
            "library_sha256": _sha256(library), "scenario_sha256": _sha256(scenario),
            "hybrid_scenario_sha256": _sha256(hybrid_scenario or scenario),
            "runner_sha256": hashlib.sha256(b"".join(
                (TOOLS / name).read_bytes()
                for name in ("lockstep.py", "check_calls.py", "run.py", "check/live.c"))).hexdigest(),
        },
    }
    if inspect_frame is not None:
        if not 1 <= inspect_frame <= frames:
            raise ValueError("inspection frame must be inside the run")
        report["inspect_frame"] = inspect_frame
    for stale in ("reference.pickle", "hybrid.json", "lockstep.json"):
        (out / stale).unlink(missing_ok=True)
    started = time.monotonic()
    for worker, worker_scenario in (("reference", scenario),
                                    ("hybrid", hybrid_scenario or scenario)):
        remaining = timeout - (time.monotonic() - started)
        command = [sys.executable, str(pathlib.Path(__file__).resolve()), "--worker", worker,
                   "--core", str(core), "--rom", str(rom), "--scenario", str(worker_scenario),
                   "--frames", str(frames), "--library", str(library), "--out", str(out)]
        if inspect_frame is not None:
            command.extend(["--inspect-frame", str(inspect_frame)])
        child = runner.supervise_child(command, max(remaining, 0.001))
        if child.timed_out:
            report.update(verdict="INCONCLUSIVE", certified=False,
                          reason="the %s run did not finish in %d s" % (worker, timeout))
            break
        if child.returncode:
            report.update(verdict="INCONCLUSIVE", certified=False,
                          reason="the %s run failed: %s" % (worker, child.stderr.strip()[-2000:]))
            break
    else:
        body = json.loads((out / "hybrid.json").read_text())
        substituted = sum(row["substituted"] for row in body["routines"])
        left = sum(sum(row["left_to_original"].values()) for row in body["routines"])
        if body["divergence"]:
            verdict = "DIVERGENT"
        elif not substituted or body["frames_compared"] != frames:
            verdict = "UNVERIFIED"
        else:
            verdict = "IDENTICAL"
        report.update(body, verdict=verdict, calls_substituted=substituted,
                      calls_left_to_original=left,
                      certified=verdict == "IDENTICAL" and left == 0)
    (out / "reference.pickle").unlink(missing_ok=True)
    (out / "lockstep.json").write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    return report


def summary(report):
    lines = ["lockstep: %s%s" % (report["verdict"],
                                 "" if report.get("certified") else " (not certified)")]
    if "reason" in report:
        lines.append("  " + report["reason"])
        return "\n".join(lines)
    lines.append("  %d of %d frames compared; %d calls substituted, %d left to the original; "
                 "%d bus accesses by the C, %d of them hardware" % (
                     report["frames_compared"], report["frames"], report["calls_substituted"],
                     report["calls_left_to_original"], report["totals"]["accesses"],
                     report["totals"]["hardware_accesses"]))
    if report["divergence"]:
        lines.append("  first difference: " + json.dumps(report["divergence"], sort_keys=True))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--core", required=True)
    parser.add_argument("--rom", required=True)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--library", required=True, help="hybrid.py --runtime live")
    parser.add_argument("--out", required=True)
    parser.add_argument("--hybrid-scenario", help="another scenario for the hybrid run (a control)")
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--inspect-frame", type=int, help="compare RAM words at this frame while preserving the full schedule")
    parser.add_argument("--worker", choices=("reference", "hybrid"), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.worker:
        try:
            _worker(args)
        except (LockstepError, runner.RunnerError, hybrid.HybridError, OSError) as exc:
            sys.exit("lockstep: " + str(exc))
        return
    report = run(args.core, args.rom, args.scenario, args.frames, args.library, args.out,
                 args.hybrid_scenario, args.timeout, args.inspect_frame)
    print(summary(report))
    sys.exit(0 if report["verdict"] == "IDENTICAL" else 1)


if __name__ == "__main__":
    main()
