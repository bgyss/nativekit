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
holds. Only the schedule is the reference's. A call is left to the original
(and counted, by reason) when an interrupt or exception was taken in it,
when it spans a frame boundary, when the 68000 wrote Z80 RAM during a Z80
call, when it did not return by its own rts/ret, when the run ended before it
returned, or when it calls a routine that has no C. A run with such calls is not certified.
"""
import argparse
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
               4: "a wait, restart or call the live machine cannot serve"}


class LockstepError(RuntimeError):
    """The lockstep run could not be made."""


class Slot(c.Structure):
    _fields_ = [("clock", c.c_uint64), ("refresh_sync", c.c_uint64), ("address", c.c_uint32),
                ("refresh_counter", c.c_uint32), ("width", c.c_uint8), ("write", c.c_uint8)]


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
            for y in range(height):
                self.video.update(c.string_at(data + y * pitch, width * 4))

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

    def frame(self, inputs):
        """Run one frame; the hashes of its picture, sound and snapshot regions."""
        self.inputs = inputs
        self.video, self.audio = hashlib.sha256(), hashlib.sha256()
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
            hashes[self._name.value.decode()] = hashlib.sha256(self._buffer.raw[:size]).hexdigest()
            region += 1
        if region == 0:
            self.errors.append("the core gave no comparison snapshot")
        return hashes


def _schedule_after(state):
    """The 68000's next sync, next interrupt and slice end, in clocks after this state's."""
    interrupt = state["int_cycle"]
    return (state["sync_cycle"] - state["cycles"],
            (1 << 63) - 1 if interrupt == 0xFFFFFFFF else interrupt - state["cycles"],
            state["target_cycle"] - state["cycles"], state.get("slice_after", 0))


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

    def refresh(at_access_start):
        core[0].nmd_decomp_refresh_state(at_access_start, c.byref(counter), c.byref(last_sync))
        return counter.value, last_sync.value

    def on_event(event):
        # the machine's refresh counter rides along with the 68000's events: as it stood
        # when a bus access began, and as it stands after an instruction
        if event["cpu"] == M68K and event["kind"] == 3:
            event["payload"]["extra"] = refresh(1)
        elif event["cpu"] == M68K and event["kind"] == 1:
            event["payload"]["state"]["refresh"] = refresh(0)
            event["payload"]["state"]["slice_after"] = (
                core[0].nmd_decomp_m68k_slice_end() - event["master_clock"])
        for call in feed(event):
            schedule[(call.cpu, call.address)].append({
                "sequence": call.sequence, "entry_clock": call.entry_clock,
                "exit_clock": call.exit_clock, "reasons": sorted(call.reasons),
                "hardware": [(clock, address & 0xFFFFFF, width, int(write), extra)
                             for address, width, _, write, clock, extra in call.accesses
                             if _hardware(call.cpu, address, rom_size)],
                "exit_refresh": call.exit_state.get("refresh", (0, 0)),
                "exit_schedule": _schedule_after(call.exit_state) if call.cpu == M68K else None,
                "tree": [address for address, _ in call.tree],
                "r": call.exit_state.get("r", 0), "wz": call.exit_state.get("wz", 0)})

    machine = Machine(args.core, args.rom, scenario, args.out, on_event)
    core[0] = machine.core
    machine.core.nmd_decomp_m68k_slice_end.restype = c.c_uint64

    # the original is never replaced here: the handler only notes when each entry is reached
    def note(cpu, pc, clock):
        entries[(cpu, pc)].append(clock)
        return 0

    handler = SUBSTITUTE(note)
    _register(machine.core, handler, routines)
    inputs = runner._load_inputs(scenario, args.frames)
    frames = []
    for number in range(1, args.frames + 1):
        frame[0] = number
        frames.append(machine.frame(inputs.get(number, {})))
        if machine.errors:
            raise LockstepError("reference: " + "; ".join(machine.errors))
    for key, rows in schedule.items():
        rows.sort(key=lambda row: row["sequence"])     # entry order: nested calls close first
        if len(entries[key]) < len(rows):
            raise LockstepError("reference: $%06X returned more often than it was entered" % key[1])
        for row, clock in zip(rows, entries[key]):
            # the entry's own clock: a sync before the instruction can move it past the
            # clock of the instruction before
            if not row["entry_clock"] <= clock <= row["entry_clock"] + 4096:
                raise LockstepError("reference: the entries of $%06X do not line up with its "
                                    "calls" % key[1])
            row["entry_clock"] = clock
            row["slots"] = [(start, (extra or (0, 0))[1], address, (extra or (0, 0))[0], width,
                             write) for start, address, width, write, extra in row.pop("hardware")]
    return {"frames": frames, "schedule": schedule,
            "entered": {key: len(clocks) for key, clocks in entries.items()}}


def _register(core, handler, routines):
    m68k = [address for cpu, address in routines if cpu == M68K]
    z80 = [address for cpu, address in routines if cpu == Z80]
    core.nmd_decomp_set_substitute.argtypes = [SUBSTITUTE, c.POINTER(c.c_uint32), c.c_uint32,
                                               c.POINTER(c.c_uint32), c.c_uint32]
    if core.nmd_decomp_set_substitute(handler, (c.c_uint32 * max(1, len(m68k)))(*m68k), len(m68k),
                                      (c.c_uint32 * max(1, len(z80)))(*z80), len(z80)):
        raise LockstepError("the core refused the routine addresses")


def _hybrid(args, scenario, routines, rom_size, reference):
    """The same run with the C substituted; the result's body."""
    library = c.CDLL(str(pathlib.Path(args.library).resolve()))
    library.nmd_live_run.restype = c.POINTER(LiveResult)
    library.nmd_live_run.argtypes = [c.c_int, c.c_void_p]
    cpu_state = check_calls.CpuState.in_dll(library, "nmd_cpu")
    z80_state = check_calls.Z80State.in_dll(library, "nmd_z80")
    schedule = reference["schedule"]
    taken = dict.fromkeys(routines, 0)
    substituted = dict.fromkeys(routines, 0)
    skipped = {key: {} for key in routines}
    totals = {"accesses": 0, "hardware_accesses": 0}
    divergence = []
    machine = Machine(args.core, args.rom, scenario, args.out)
    core = machine.core
    core.nmd_decomp_m68k_live_state.argtypes = [c.POINTER(runner.M68KState)]
    core.nmd_decomp_z80_live_state.argtypes = [c.POINTER(runner.Z80State)]
    core.nmd_decomp_m68k_live_resume.argtypes = [
        c.POINTER(c.c_uint32), c.POINTER(c.c_uint32), c.c_uint32, c.c_uint32, c.c_uint32,
        c.c_uint64, c.c_uint32, c.c_uint64, c.c_int64, c.c_int64, c.c_int64, c.c_int64]
    core.nmd_decomp_z80_live_resume.argtypes = [c.POINTER(runner.Z80State), c.c_uint32,
                                                c.c_uint64]
    library.nmd_live_bind(c.cast(core.nmd_decomp_m68k_live_access, c.c_void_p),
                          c.cast(core.nmd_decomp_z80_live_access, c.c_void_p), rom_size)
    current = [0]

    def diverge(**detail):
        if not divergence:
            divergence.append(dict(detail, frame=current[0]))

    def substitute(cpu, pc, clock):
        key = (cpu, pc)
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
        if reason:
            skipped[key][reason] = skipped[key].get(reason, 0) + 1
            return 0
        slots = (Slot * max(1, len(record["slots"])))(*record["slots"])
        library.nmd_live_begin(cpu, slots, len(record["slots"]))
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
        result = library.nmd_live_run(cpu, routines[key][0]).contents
        totals["accesses"] += result.accesses
        totals["hardware_accesses"] += result.hardware
        if result.status or result.slots_used != len(record["slots"]):
            what = LIVE_STATUS.get(result.status, "a hardware access the original made is missing")
            expected = record["slots"][result.slot] if result.slot < len(record["slots"]) else None
            expected = expected and (expected[0], expected[2], expected[4], expected[5])
            diverge(kind="hardware", what=what, cpu=check_calls.CPU_NAMES[cpu], address=pc,
                    name=name, call=index + 1, record_position=result.slot,
                    access={"address": result.address, "width": result.width,
                            "write": bool(result.write)},
                    expected=expected and {"address": expected[1], "width": expected[2],
                                           "write": bool(expected[3])})
            core.nmd_decomp_fail()      # the machine holds half a routine: stop the run
            return 0
        for address in record["tree"]:
            taken[(cpu, address)] += 1  # the C ran these inside this call
        if cpu == M68K:
            other = cpu_state.usp if cpu_state.sr & 0x2000 else cpu_state.ssp
            core.nmd_decomp_m68k_live_resume(cpu_state.d, cpu_state.a, other, cpu_state.sr,
                                             cpu_state.pc, record["exit_clock"],
                                             *record["exit_refresh"], *record["exit_schedule"])
        else:
            flags = z80_state.f
            state.main[:] = [z80_state.b, z80_state.c, z80_state.d, z80_state.e, z80_state.h,
                             z80_state.l, flags, z80_state.a]
            state.alt[:] = [z80_state.b2, z80_state.c2, z80_state.d2, z80_state.e2,
                            z80_state.h2, z80_state.l2, z80_state.f2, z80_state.a2]
            state.ix, state.iy, state.sp, state.i = (z80_state.ix, z80_state.iy, z80_state.sp,
                                                     z80_state.i)
            state.iff1, state.iff2, state.imode = z80_state.iff1, z80_state.iff2, z80_state.im
            state.last_flag_result = flags & 0xA8
            state.zflag, state.pvflag, state.nflag = (bool(flags & 0x40), bool(flags & 0x04),
                                                      bool(flags & 0x02))
            state.chflags = (0x08 if flags & 0x10 else 0) | (0x80 if flags & 0x01 else 0)
            state.r, state.wz = record["r"], record["wz"]
            core.nmd_decomp_z80_live_resume(c.byref(state), z80_state.pc, record["exit_clock"])
        substituted[key] += 1
        return 1

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
        hashes = machine.frame(inputs.get(number, {}))
        if divergence:
            break
        if machine.errors:
            raise LockstepError("hybrid: " + "; ".join(machine.errors))
        expected = reference["frames"][number - 1]
        regions = sorted(name for name in expected if hashes.get(name) != expected[name])
        if regions:
            diverge(kind="frame", what="state after the frame differs", regions=regions)
            break
        compared += 1
    for key in routines:        # calls the original made that the hybrid never reached
        if not divergence and taken[key] != reference["entered"][key]:
            diverge(kind="call", what="calls the original made are missing",
                    cpu=check_calls.CPU_NAMES[key[0]], address=key[1], name=routines[key][1],
                    expected=reference["entered"][key], got=taken[key])
    return {
        "frames_compared": compared, "divergence": divergence[0] if divergence else None,
        "totals": totals,
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


def run(core, rom, scenario, frames, library, out, hybrid_scenario=None, timeout=600):
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
    for stale in ("reference.pickle", "hybrid.json", "lockstep.json"):
        (out / stale).unlink(missing_ok=True)
    started = time.monotonic()
    for worker, worker_scenario in (("reference", scenario),
                                    ("hybrid", hybrid_scenario or scenario)):
        remaining = timeout - (time.monotonic() - started)
        command = [sys.executable, str(pathlib.Path(__file__).resolve()), "--worker", worker,
                   "--core", str(core), "--rom", str(rom), "--scenario", str(worker_scenario),
                   "--frames", str(frames), "--library", str(library), "--out", str(out)]
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
    parser.add_argument("--worker", choices=("reference", "hybrid"), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.worker:
        try:
            _worker(args)
        except (LockstepError, runner.RunnerError, hybrid.HybridError, OSError) as exc:
            sys.exit("lockstep: " + str(exc))
        return
    report = run(args.core, args.rom, args.scenario, args.frames, args.library, args.out,
                 args.hybrid_scenario, args.timeout)
    print(summary(report))
    sys.exit(0 if report["verdict"] == "IDENTICAL" else 1)


if __name__ == "__main__":
    main()
