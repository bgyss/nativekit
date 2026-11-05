#!/usr/bin/env python3
"""Bounded NMDH capture and same-input resource preflight for the test core."""
import argparse
import ctypes as c
from dataclasses import dataclass
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
TRACE_PATH = ROOT / "consoles/md/tools/decomp/trace.py"
NMDT_PATH = ROOT / "consoles/md/tests/oracle/trace.py"
ABI_HEADER = ROOT / "consoles/md/tests/oracle/decomp_abi.h"
ABI_SOURCE = ROOT / "consoles/md/tests/oracle/decomp_abi.c"
PATCH_DIR = ROOT / "tools/toolchains/patches/blastem"
PINS_PATH = ROOT / "tools/toolchains/pins.env"

NMDH_CAPABILITIES = {
    "m68k_instruction": 1 << 0,
    "m68k_state": 1 << 1,
    "z80_instruction": 1 << 2,
    "z80_state": 1 << 3,
    "flow_edge": 1 << 4,
    "reset_exception_interrupt": 1 << 5,
    "fetch_provenance": 1 << 6,
    "bus_intervals": 1 << 7,
    "master_clock": 1 << 8,
    "sync_checkpoint": 1 << 9,
    "controlled_stop": 1 << 10,
    "replace_handoff": 1 << 11,
}
NMDT_CAP_M68K_CPU = 1 << 0
TRACE_M68K, TRACE_Z80 = 1, 2
TRACE_VDP, TRACE_YM2612, TRACE_PSG = 3, 4, 5
TRACE_FETCH, TRACE_READ, TRACE_WRITE, TRACE_DMA, TRACE_IRQ = 1, 2, 3, 4, 5
EVENT_INSTRUCTION, EVENT_FLOW, EVENT_BUS = 1, 2, 3
EVENT_RESET, EVENT_EXCEPTION, EVENT_INTERRUPT = 4, 5, 6
EVENT_STOP, EVENT_REPLACE = 7, 8
ACTION_CONTINUE, ACTION_STOP, ACTION_REPLACE = 0, 1, 2
BUTTONS = {"A": 0, "X": 1, "MODE": 2, "START": 3, "UP": 4, "DOWN": 5,
           "LEFT": 6, "RIGHT": 7, "B": 8, "Y": 9, "Z": 10, "C": 11}


class RunnerError(RuntimeError):
    pass


@dataclass
class ChildResult:
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False


def supervise_child(command, timeout_seconds):
    """Run one worker process and terminate it when its wall-clock limit expires."""
    if not isinstance(timeout_seconds, (int, float)) or timeout_seconds <= 0:
        raise ValueError("timeout_seconds must be positive")
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, cwd=ROOT)
    try:
        stdout, stderr = process.communicate(timeout=timeout_seconds)
        return ChildResult(process.returncode, stdout, stderr)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            stdout, stderr = process.communicate(timeout=1.0)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
        return ChildResult(process.returncode if process.returncode is not None else -1,
                           stdout or "", stderr or "", True)


def _load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


nmdh_trace = _load_module("nmdh_trace", TRACE_PATH)
nmdt_trace = _load_module("nmdt_trace", NMDT_PATH)


def _sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _pin_value(name):
    for line in PINS_PATH.read_text().splitlines():
        if line.startswith(name + "="):
            return line.split("=", 1)[1].split("#", 1)[0].strip()
    raise RunnerError("pinned core revision is missing")


def _hash_named_files(files, prefix=b""):
    digest = hashlib.sha256()
    digest.update(prefix)
    for path in sorted((Path(item) for item in files), key=lambda item: item.name):
        digest.update(path.name.encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def _required_capability_mask(scenario):
    names = scenario.get("required_capabilities")
    if not isinstance(names, list) or not names or len(names) != len(set(names)):
        raise RunnerError("scenario requires a nonempty unique required_capabilities list")
    mask = 0
    for name in names:
        if not isinstance(name, str) or name not in NMDH_CAPABILITIES:
            raise RunnerError("scenario names an unknown required capability")
        mask |= NMDH_CAPABILITIES[name]
    return mask


def _validate_scenario(scenario, frames):
    if not isinstance(scenario, dict) or scenario.get("version") != 1:
        raise RunnerError("unsupported scenario version")
    if scenario.get("region") not in ("J", "U", "E"):
        raise RunnerError("invalid scenario region")
    if scenario.get("model") not in ("md1va3", "md1va6"):
        raise RunnerError("invalid scenario model")
    if scenario.get("pad1") not in ("3", "6") or scenario.get("pad2") not in ("3", "6"):
        raise RunnerError("invalid scenario controller configuration")
    inputs = scenario.get("input")
    if not isinstance(inputs, dict):
        raise RunnerError("scenario input must map frames to controller buttons")
    for frame_text, ports in inputs.items():
        if (not isinstance(frame_text, str) or not frame_text.isdecimal() or
                str(int(frame_text)) != frame_text or not 1 <= int(frame_text) <= frames):
            raise RunnerError("scenario input frame is not canonical or in range")
        if not isinstance(ports, dict) or any(port not in ("1", "2") for port in ports):
            raise RunnerError("scenario input ports must be 1 or 2")
        for port, buttons in ports.items():
            if not isinstance(buttons, list) or any(
                    not isinstance(button, str) or button not in BUTTONS for button in buttons):
                raise RunnerError("scenario contains an unknown controller button")
            pad = scenario["pad1"] if port == "1" else scenario["pad2"]
            if pad == "3" and any(button in ("X", "Y", "Z", "MODE") for button in buttons):
                raise RunnerError("three-button pads do not have X, Y, Z or MODE")
    stop = scenario.get("stop")
    if not isinstance(stop, dict) or stop.get("cpu") not in ("m68k", "z80"):
        raise RunnerError("scenario stop must name m68k or z80")
    if stop.get("criterion") not in ("pc", "return_sentinel", "interrupt",
                                      "instruction_budget", "frame_limit"):
        raise RunnerError("unknown scenario stop criterion")
    if stop.get("action") not in ("stop", "replace"):
        raise RunnerError("scenario stop action must be stop or replace")
    needs_value = stop["criterion"] in ("pc", "return_sentinel", "instruction_budget")
    if needs_value:
        value = stop.get("value")
        if type(value) is not int or value < 0 or value > 0xFFFFFFFFFFFFFFFF:
            raise RunnerError("scenario stop criterion requires an unsigned integer value")
    elif "value" in stop:
        raise RunnerError("scenario stop criterion does not take a value")
    if stop["criterion"] == "frame_limit" and stop["action"] != "stop":
        raise RunnerError("frame_limit supports STOP only")
    required = _required_capability_mask(scenario)
    cpu_state = ((1 << 0) | (1 << 1)) if stop["cpu"] == "m68k" else ((1 << 2) | (1 << 3))
    if required & cpu_state != cpu_state:
        raise RunnerError("scenario must require instruction and state capabilities for its stop CPU")
    if stop["criterion"] != "frame_limit" and not (required & (1 << 10)):
        raise RunnerError("scenario stop criterion requires controlled_stop")
    if stop["criterion"] == "interrupt" and not (required & (1 << 5)):
        raise RunnerError("interrupt stop requires reset_exception_interrupt")
    if stop["action"] == "replace" and not (required & (1 << 11)):
        raise RunnerError("REPLACE action requires replace_handoff")
    return scenario


def _make_identity(core_path, rom_path, scenario, capabilities):
    blastem_pin = _pin_value("BLASTEM_PIN")
    scenario_bytes = nmdh_trace.canonical_json(scenario)
    patches = list(PATCH_DIR.glob("*.patch"))
    patches.extend((ABI_HEADER, ABI_SOURCE))
    patch_hash = _hash_named_files(patches, blastem_pin.encode("ascii"))
    runner_hash = _hash_named_files((Path(__file__), TRACE_PATH, NMDT_PATH))
    abi_hash = _hash_named_files((ABI_HEADER, ABI_SOURCE))
    identities = {
        "rom_sha256": _sha256_file(rom_path),
        "scenario_sha256": hashlib.sha256(scenario_bytes).hexdigest(),
        "core_sha256": _sha256_file(core_path),
        "patch_sha256": patch_hash,
        "abi_sha256": abi_hash,
        "runner_sha256": runner_hash,
    }
    metadata = {
        "abi_version": 1,
        "capabilities": capabilities,
        "identities": identities,
        "options": {key: scenario[key] for key in ("region", "model", "pad1", "pad2")},
        "blastem_revision": blastem_pin,
    }
    return metadata


def _preflight_core(core_path, scenario):
    try:
        core = c.CDLL(str(Path(core_path).resolve()))
        abi_version = core.nmd_decomp_abi_version
        abi_capabilities = core.nmd_decomp_capabilities
        register = core.nmd_decomp_register
        core.nmd_decomp_reset
        core.nmd_decomp_error
    except (OSError, AttributeError) as exc:
        raise RunnerError("core is missing required NMDH ABI symbols") from exc
    abi_version.restype = c.c_uint32
    abi_capabilities.restype = c.c_uint64
    version = abi_version()
    capabilities = abi_capabilities()
    if version != 1:
        raise RunnerError("core has an unsupported NMDH ABI version")
    required = _required_capability_mask(scenario)
    if capabilities & required != required:
        missing = [name for name, bit in NMDH_CAPABILITIES.items() if required & bit and not capabilities & bit]
        raise RunnerError("core lacks required capability: " + ", ".join(missing))
    try:
        trace_version = core.nmd_trace_version
        trace_capabilities = core.nmd_trace_capabilities
        core.nmd_trace_register
        core.nmd_trace_reset
        core.nmd_trace_error
    except AttributeError as exc:
        raise RunnerError("core is missing the existing NMDT v2 trace ABI") from exc
    trace_version.restype = c.c_uint32
    trace_capabilities.restype = c.c_uint64
    if trace_version() != 2 or not (trace_capabilities() & NMDT_CAP_M68K_CPU):
        raise RunnerError("core lacks the existing NMDT v2 68000 trace capability")
    return capabilities


def _require_libretro_api(core_path):
    required = (
        "retro_set_environment", "retro_set_video_refresh", "retro_set_audio_sample",
        "retro_set_audio_sample_batch", "retro_set_input_poll", "retro_set_input_state",
        "retro_init", "retro_get_system_info", "retro_load_game", "retro_get_system_av_info",
        "retro_run", "retro_unload_game", "retro_deinit",
    )
    try:
        core = c.CDLL(str(Path(core_path).resolve()))
        for symbol in required:
            getattr(core, symbol)
    except (OSError, AttributeError) as exc:
        raise RunnerError("core is missing a required Libretro symbol: " + str(exc)) from exc


class Game(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p), ("size", c.c_size_t),
                ("meta", c.c_char_p)]


class Geometry(c.Structure):
    _fields_ = [("base_width", c.c_uint), ("base_height", c.c_uint),
                ("max_width", c.c_uint), ("max_height", c.c_uint), ("aspect", c.c_float)]


class Timing(c.Structure):
    _fields_ = [("fps", c.c_double), ("sample_rate", c.c_double)]


class AVInfo(c.Structure):
    _fields_ = [("geometry", Geometry), ("timing", Timing)]


class SystemInfo(c.Structure):
    _fields_ = [("library_name", c.c_char_p), ("library_version", c.c_char_p),
                ("valid_extensions", c.c_char_p), ("need_fullpath", c.c_bool),
                ("block_extract", c.c_bool)]


class Variable(c.Structure):
    _fields_ = [("key", c.c_char_p), ("value", c.c_char_p)]


class M68KState(c.Structure):
    _fields_ = [
        ("d", c.c_uint32 * 8), ("a", c.c_uint32 * 8), ("other_sp", c.c_uint32),
        ("cycles", c.c_uint32), ("target_cycle", c.c_uint32), ("sync_cycle", c.c_uint32),
        ("int_cycle", c.c_uint32), ("prefetch", c.c_uint16), ("reserved", c.c_uint16),
        ("status", c.c_uint8), ("ccr", c.c_uint8), ("xflag", c.c_uint8),
        ("nflag", c.c_uint8), ("zflag", c.c_uint8), ("vflag", c.c_uint8),
        ("cflag", c.c_uint8), ("trace_pending", c.c_uint8), ("stopped", c.c_uint8),
        ("int_priority", c.c_uint8), ("int_pending_num", c.c_uint8),
        ("int_pending", c.c_uint8), ("int_num", c.c_uint8), ("int_ack", c.c_uint8),
        ("reserved_flags", c.c_uint8 * 2),
    ]


class Z80State(c.Structure):
    _fields_ = [
        ("main", c.c_uint8 * 8), ("alt", c.c_uint8 * 8),
        ("cycles", c.c_uint32), ("sync_cycle", c.c_uint32), ("nmi_cycle", c.c_uint32),
        ("int_end_cycle", c.c_uint32), ("int_cycle", c.c_uint32),
        ("wz", c.c_uint16), ("sp", c.c_uint16), ("pc", c.c_uint16),
        ("iy", c.c_uint16), ("ix", c.c_uint16), ("i", c.c_uint8), ("r", c.c_uint8),
        ("rhigh", c.c_uint8), ("zflag", c.c_uint8), ("reset", c.c_uint8),
        ("pvflag", c.c_uint8), ("nflag", c.c_uint8), ("last_flag_result", c.c_uint8),
        ("int_value", c.c_uint8), ("imode", c.c_uint8), ("iff2", c.c_uint8),
        ("iff1", c.c_uint8), ("chflags", c.c_uint8), ("busreq", c.c_uint8),
        ("busack", c.c_uint8),
    ]


class CPUState(c.Union):
    _fields_ = [("m68k", M68KState), ("z80", Z80State)]


class Instruction(c.Structure):
    _fields_ = [("fetched_length", c.c_uint8), ("fetched_bytes", c.c_uint8 * 16),
                ("opcode", c.c_uint16), ("reserved", c.c_uint16), ("state", CPUState)]


class FlowEdge(c.Structure):
    _fields_ = [("previous_pc", c.c_uint32), ("current_pc", c.c_uint32),
                ("transfer_kind", c.c_uint32), ("target_known", c.c_uint32)]


class BusAccess(c.Structure):
    _fields_ = [("start_clock", c.c_uint64), ("completion_clock", c.c_uint64),
                ("address", c.c_uint32), ("value", c.c_uint32), ("width", c.c_uint8),
                ("operation", c.c_uint8), ("provenance", c.c_uint8), ("reserved", c.c_uint8)]


class Interrupt(c.Structure):
    _fields_ = [("vector", c.c_uint32), ("reason", c.c_uint32),
                ("phase", c.c_uint32), ("stack_pointer", c.c_uint32)]


class StopEvent(c.Structure):
    _fields_ = [("criterion", c.c_uint32), ("reason", c.c_uint32),
                ("instruction_count", c.c_uint64), ("stop_clock", c.c_uint64)]


class EventPayload(c.Union):
    _fields_ = [("instruction", Instruction), ("m68k", M68KState), ("z80", Z80State),
                ("flow", FlowEdge), ("bus", BusAccess), ("interrupt", Interrupt),
                ("stop", StopEvent), ("bytes", c.c_uint8 * 256)]


class DecompEvent(c.Structure):
    _fields_ = [("struct_size", c.c_uint32), ("kind", c.c_uint32), ("cpu", c.c_uint32),
                ("flags", c.c_uint32), ("sequence", c.c_uint64),
                ("master_clock", c.c_uint64), ("epoch", c.c_uint64),
                ("pc", c.c_uint32), ("next_pc", c.c_uint32), ("payload", EventPayload)]


if c.sizeof(DecompEvent) != 304:
    raise RuntimeError("ctypes NMDH event layout does not match ABI v1")


def _state_dict(state):
    return {name: list(value) if isinstance(value, (c.Array,)) else int(value)
            for name, value in ((name, getattr(state, name)) for name, _ in state._fields_)}


def _event_dict(event):
    kind = event.kind
    if kind == EVENT_INSTRUCTION:
        instruction = event.payload.instruction
        payload = {
            "fetched_length": instruction.fetched_length,
            "fetched_bytes": list(instruction.fetched_bytes[:instruction.fetched_length]),
            "opcode": instruction.opcode,
            "state": (_state_dict(instruction.state.m68k) if event.cpu == TRACE_M68K
                      else _state_dict(instruction.state.z80)),
        }
    elif kind == EVENT_FLOW:
        data = event.payload.flow
        payload = {"previous_pc": data.previous_pc, "current_pc": data.current_pc,
                   "transfer_kind": data.transfer_kind, "target_known": data.target_known}
    elif kind == EVENT_BUS:
        data = event.payload.bus
        payload = {"start_clock": data.start_clock, "completion_clock": data.completion_clock,
                   "address": data.address, "value": data.value, "width": data.width,
                   "operation": data.operation, "provenance": data.provenance}
    elif kind in (EVENT_RESET, EVENT_EXCEPTION, EVENT_INTERRUPT):
        data = event.payload.interrupt
        payload = {"vector": data.vector, "reason": data.reason,
                   "phase": data.phase, "stack_pointer": data.stack_pointer}
    else:
        data = event.payload.stop
        payload = {"criterion": data.criterion, "reason": data.reason,
                   "instruction_count": data.instruction_count, "stop_clock": data.stop_clock}
    return {"struct_size": event.struct_size, "kind": kind, "cpu": event.cpu,
            "flags": event.flags, "sequence": event.sequence,
            "master_clock": event.master_clock, "epoch": event.epoch,
            "pc": event.pc, "next_pc": event.next_pc, "payload": payload}


def _load_inputs(scenario, frames):
    current = {}
    for frame_text, ports in scenario["input"].items():
        frame = {}
        for port, names in ports.items():
            frame[int(port) - 1] = {BUTTONS[name] for name in names}
        current[int(frame_text)] = frame
    return current


def instruction_stop_matches(stop, event, instruction_count, pending_interrupt=False):
    """Match a requested stop against the instruction just completed."""
    cpu_id = TRACE_M68K if stop["cpu"] == "m68k" else TRACE_Z80
    if event["cpu"] != cpu_id:
        return False
    if stop["criterion"] == "interrupt":
        return event["kind"] == EVENT_INSTRUCTION and pending_interrupt
    if event["kind"] != EVENT_INSTRUCTION:
        return False
    if stop["criterion"] in ("pc", "return_sentinel"):
        return event["next_pc"] == stop["value"]
    if stop["criterion"] == "instruction_budget":
        return instruction_count >= stop["value"]
    return False


class LimitedStream:
    """File wrapper that bounds NMDT writes while allowing its header seek."""

    def __init__(self, stream, max_bytes):
        self.stream = stream
        self.max_bytes = max_bytes
        self.high_water = stream.tell()

    def write(self, data):
        end = self.stream.tell() + len(data)
        if max(self.high_water, end) > self.max_bytes:
            raise nmdh_trace.CaptureLimitError("NMDT v2 stream exceeded its storage limit")
        count = self.stream.write(data)
        self.high_water = max(self.high_water, self.stream.tell())
        return count

    def __getattr__(self, name):
        return getattr(self.stream, name)


def _worker(args):
    core_path = Path(args.core)
    rom_path = Path(args.rom)
    scenario_path = Path(args.scenario)
    output = Path(args.out)
    scenario_frames = args.scenario_frames or args.frames
    scenario = _validate_scenario(json.loads(scenario_path.read_text()), scenario_frames)
    expected_metadata = json.loads(args.identity_json)
    capabilities = _preflight_core(core_path, scenario)
    metadata = _make_identity(core_path, rom_path, scenario, capabilities)
    if metadata != expected_metadata:
        raise RunnerError("capture identities changed after preflight")
    _require_libretro_api(core_path)

    output.mkdir(parents=True, exist_ok=True)
    estimate_mode = args.worker_mode == "estimate"
    stem = "sample" if estimate_mode else "capture"
    nmdh_path = output / (stem + ".nmdh")
    nmdt_partial = output / (stem + ".nmdt.partial")
    nmdt_path = output / (stem + ".nmdt")
    nmdh_writer = nmdh_trace.NMDHWriter(nmdh_path, metadata, args.max_bytes // 2)
    nmdt_file = nmdt_partial.open("xb")
    nmdt_stream = LimitedStream(nmdt_file, args.max_bytes // 2)
    nmdt_writer = nmdt_trace.Writer(nmdt_stream)
    core = c.CDLL(str(core_path.resolve()))
    trace_errors = []
    event_counts = {}
    instruction_counts = {"m68k": 0, "z80": 0}
    frame_count = 0
    stop_state = {"pending_interrupt": False, "triggered": False, "action": 0}
    current_input = [{}]
    video_hash = hashlib.sha256()
    audio_hash = hashlib.sha256()
    video_frames = [0]
    audio_frames = [0]
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
    directory = str(output.resolve()).encode()
    pixel_format = [None]
    queried = set()
    advertised = {}
    game_buffer = None
    decomp_callback = None
    nmdt_callback = None

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
            variable = c.cast(data, c.POINTER(Variable)).contents
            value = options.get(variable.key)
            if value is None:
                return False
            queried.add(variable.key)
            variable.value = value
            return True
        if command == 16:
            entries = c.cast(data, c.POINTER(Variable))
            for index in range(1024):
                item = entries[index]
                if item.key is None:
                    return True
                if item.value and b"; " in item.value:
                    advertised[item.key] = set(item.value.split(b"; ", 1)[1].split(b"|"))
            return False
        return False

    @c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t)
    def video(data, width, height, pitch):
        if (not data or pixel_format[0] != 1 or not 0 < width <= 4096 or
                not 0 < height <= 4096 or pitch < width * 4):
            trace_errors.append("missing or unsupported video frame")
            return
        for y in range(height):
            video_hash.update(c.string_at(data + y * pitch, width * 4))
        video_frames[0] += 1

    @c.CFUNCTYPE(c.c_size_t, c.POINTER(c.c_int16), c.c_size_t)
    def audio_batch(data, frames):
        if frames and data:
            audio_hash.update(c.string_at(data, frames * 4))
            audio_frames[0] += frames
        return frames

    @c.CFUNCTYPE(None, c.c_int16, c.c_int16)
    def audio_sample(left, right):
        audio_hash.update(int(left).to_bytes(2, sys.byteorder, signed=True))
        audio_hash.update(int(right).to_bytes(2, sys.byteorder, signed=True))
        audio_frames[0] += 1

    @c.CFUNCTYPE(None)
    def input_poll():
        pass

    @c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint)
    def input_state(port, device, index, button):
        return int(device == 1 and index == 0 and button in current_input[0].get(port, set()))

    @c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(DecompEvent))
    def on_decomp(user, event_pointer):
        try:
            event = event_pointer.contents
            name = "m68k" if event.cpu == TRACE_M68K else "z80"
            event_counts[str(event.kind)] = event_counts.get(str(event.kind), 0) + 1
            if event.kind == EVENT_INSTRUCTION:
                instruction_counts[name] += 1
            event_record = _event_dict(event)
            nmdh_writer.write_event(event_record)
            if (event.kind == EVENT_INTERRUPT and
                    name == scenario["stop"]["cpu"] and
                    scenario["stop"]["criterion"] == "interrupt"):
                stop_state["pending_interrupt"] = True
            if (not estimate_mode and not stop_state["triggered"] and
                    instruction_stop_matches(scenario["stop"], event_record,
                                             instruction_counts[name],
                                             stop_state["pending_interrupt"])):
                stop_state["triggered"] = True
                stop_state["action"] = (ACTION_REPLACE if scenario["stop"]["action"] == "replace"
                                         else ACTION_STOP)
                return stop_state["action"]
            if event.kind == EVENT_STOP:
                stop_state["triggered"] = True
                stop_state["action"] = ACTION_STOP
            elif event.kind == EVENT_REPLACE:
                stop_state["triggered"] = True
                stop_state["action"] = ACTION_REPLACE
            return ACTION_CONTINUE
        except Exception as exc:
            trace_errors.append("NMDH callback failed: " + str(exc))
            return ACTION_STOP

    @c.CFUNCTYPE(None, c.c_void_p, c.c_uint64, c.c_uint8, c.c_uint8, c.c_uint8,
                 c.c_uint32, c.c_uint32)
    def on_nmdt(user, clock, device, operation, width, address, value):
        try:
            nmdt_writer.emit(nmdt_trace.Event(clock, device, operation, width, address, value))
        except Exception as exc:
            trace_errors.append("NMDT callback failed: " + str(exc))

    start_time = time.monotonic()
    loaded = False
    initialized = False
    try:
        for name, callback in (("retro_set_environment", environment),
                               ("retro_set_video_refresh", video),
                               ("retro_set_audio_sample", audio_sample),
                               ("retro_set_audio_sample_batch", audio_batch),
                               ("retro_set_input_poll", input_poll),
                               ("retro_set_input_state", input_state)):
            getattr(core, name)(callback)

        unsupported = sorted(key.decode() for key, value in options.items()
                             if value not in advertised.get(key, set()))
        if trace_errors or unsupported:
            raise RunnerError("unsupported core options: " + ", ".join(unsupported))

        core.retro_init()
        initialized = True
        system_info = SystemInfo()
        core.retro_get_system_info(c.byref(system_info))
        rom_bytes = rom_path.read_bytes()
        if system_info.need_fullpath:
            game = Game(str(rom_path.resolve()).encode(), None, 0, None)
        else:
            game_buffer = c.create_string_buffer(rom_bytes)
            game = Game(None, c.cast(game_buffer, c.c_void_p), len(rom_bytes), None)
        if not core.retro_load_game(c.byref(game)):
            raise RunnerError("core refused cartridge")
        loaded = True

        core.nmd_decomp_register.argtypes = [c.c_uint32,
                                             c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(DecompEvent)),
                                             c.c_void_p]
        core.nmd_decomp_register.restype = c.c_int
        core.nmd_decomp_reset()
        if not core.nmd_decomp_register(1, on_decomp, None):
            raise RunnerError("core refused NMDH ABI version 1")
        core.nmd_trace_register.argtypes = [c.c_uint32, c.CFUNCTYPE(
            None, c.c_void_p, c.c_uint64, c.c_uint8, c.c_uint8, c.c_uint8,
            c.c_uint32, c.c_uint32), c.c_void_p]
        core.nmd_trace_register.restype = c.c_int
        core.nmd_trace_reset()
        if not core.nmd_trace_register(2, on_nmdt, None):
            raise RunnerError("core refused NMDT v2 callback")

        av_info = AVInfo()
        core.retro_get_system_av_info(c.byref(av_info))
        inputs = _load_inputs(scenario, args.frames)
        frame_limit = args.frames
        for frame_number in range(1, frame_limit + 1):
            current_input[0] = inputs.get(frame_number, {})
            core.retro_run()
            frame_count += 1
            if trace_errors:
                raise RunnerError("capture callback error: " + "; ".join(trace_errors))
            if not estimate_mode and stop_state["triggered"]:
                break
        if not estimate_mode:
            criterion = scenario["stop"]["criterion"]
            if criterion == "frame_limit":
                stop_state["triggered"] = frame_count == args.frames
            if not stop_state["triggered"]:
                raise RunnerError("scenario stop criterion was not reached")
        if core.nmd_decomp_error():
            raise RunnerError("core reported an NMDH trace error")
        if core.nmd_trace_error():
            raise RunnerError("core reported an NMDT trace error")

        nmdt_writer.close()
        nmdt_file.flush()
        os.fsync(nmdt_file.fileno())
        nmdt_file.close()
        os.replace(nmdt_partial, nmdt_path)
        nmdh_writer.finish("complete")
        elapsed = time.monotonic() - start_time
        stream_sizes = nmdh_path.stat().st_size + nmdt_path.stat().st_size
        result = {
            "status": "complete",
            "frames_run": frame_count,
            "events": nmdh_writer.count,
            "event_counts": event_counts,
            "instruction_counts": instruction_counts,
            "nmdt_records": nmdt_writer.count,
            "elapsed_seconds": elapsed,
            "stream_bytes": stream_sizes,
            "nmdh_bytes": nmdh_path.stat().st_size,
            "nmdt_bytes": nmdt_path.stat().st_size,
            "video_frames": video_frames[0],
            "audio_frames": audio_frames[0],
            "video_sha256": video_hash.hexdigest(),
            "audio_sha256": audio_hash.hexdigest(),
            "stop_action": stop_state["action"],
        }
        (output / "worker-result.json").write_bytes(nmdh_trace.canonical_json(result))
        output_manifest = {"format": "NMDH v1 capture", "metadata": metadata, **result,
                           "outputs": [nmdh_path.name, nmdt_path.name]}
        (output / "manifest.json").write_bytes(nmdh_trace.canonical_json(output_manifest))
        return 0
    except Exception as exc:
        limited = (isinstance(exc, nmdh_trace.CaptureLimitError) or
                   any("limit" in error.casefold() or "max_bytes" in error.casefold()
                       for error in trace_errors))
        status = "incomplete_storage_limit" if limited else "incomplete_core_error"
        if not nmdh_writer._closed:
            nmdh_writer.abort("storage_limit" if limited else "core_error")
        try:
            nmdt_file.close()
        except OSError:
            pass
        worker_status = {"status": status, "reason": str(exc),
                         "identities": metadata["identities"]}
        (output / "worker-status.json").write_bytes(nmdh_trace.canonical_json(worker_status))
        raise
    finally:
        if loaded:
            core.retro_unload_game()
        if initialized:
            core.retro_deinit()


def _status_file(output, status, message, metadata):
    output.mkdir(parents=True, exist_ok=True)
    document = {"status": status, "reason": message, "identities": metadata["identities"]}
    (output / "status.json").write_bytes(nmdh_trace.canonical_json(document))


def _run_parent(args):
    core_path = Path(args.core)
    rom_path = Path(args.rom)
    scenario_path = Path(args.scenario)
    output = Path(args.out)
    if not core_path.is_file() or not rom_path.is_file() or not scenario_path.is_file():
        raise RunnerError("core, ROM and scenario files must exist")
    if type(args.frames) is not int or args.frames < 1:
        raise RunnerError("frames must be positive")
    if args.max_seconds <= 0 or args.max_bytes < 1024:
        raise RunnerError("resource limits must be positive and large enough for a header")
    if output.exists():
        raise RunnerError("output directory already exists")
    scenario = _validate_scenario(json.loads(scenario_path.read_text()), args.frames)
    capabilities = _preflight_core(core_path, scenario)
    metadata = _make_identity(core_path, rom_path, scenario, capabilities)

    if args.mode == "capture":
        if not args.estimate_file or not Path(args.estimate_file).is_file():
            raise RunnerError("capture requires a matching preflight estimate file")
        estimate = json.loads(Path(args.estimate_file).read_text())
        if not isinstance(estimate, dict) or not isinstance(estimate.get("metadata"), dict):
            raise RunnerError("invalid preflight estimate document")
        nmdh_trace.validate_identity(estimate["metadata"], metadata)
        if type(estimate.get("frames")) is not int or estimate["frames"] != args.frames:
            raise RunnerError("preflight estimate frame count mismatch")
        if estimate.get("status") != "complete":
            raise RunnerError("preflight estimate is incomplete")
        estimated_seconds = estimate.get("estimated_seconds")
        estimated_bytes = estimate.get("estimated_bytes")
        if (type(estimated_seconds) not in (int, float) or
                not math.isfinite(estimated_seconds) or estimated_seconds < 0 or
                type(estimated_bytes) is not int or estimated_bytes < 0):
            raise RunnerError("invalid preflight resource estimate")
        if (estimated_seconds > args.max_seconds or estimated_bytes > args.max_bytes):
            raise RunnerError("preflight estimate exceeds the capture resource limits")
        worker_mode = "capture"
        worker_frames = args.frames
    else:
        worker_mode = "estimate"
        worker_frames = min(3, args.frames)

    output.mkdir(parents=True)
    command = [sys.executable, str(Path(__file__).resolve()), "--worker-mode", worker_mode,
               "--core", str(core_path.resolve()), "--rom", str(rom_path.resolve()),
               "--scenario", str(scenario_path.resolve()), "--frames", str(worker_frames),
               "--scenario-frames", str(args.frames),
               "--out", str(output.resolve()), "--max-bytes", str(args.max_bytes),
               "--identity-json", json.dumps(metadata, sort_keys=True, separators=(",", ":"))]
    child = supervise_child(command, args.max_seconds)
    if child.timed_out:
        _status_file(output, "incomplete_timeout", "child process exceeded max_seconds", metadata)
        raise RunnerError("capture worker timed out; partial output is incomplete")
    if child.returncode != 0:
        worker_status_path = output / "worker-status.json"
        if worker_status_path.is_file():
            worker_status = json.loads(worker_status_path.read_text())
            _status_file(output, worker_status.get("status", "incomplete_core_error"),
                         worker_status.get("reason", "worker failed"), metadata)
        else:
            _status_file(output, "incomplete_core_error", "worker failed", metadata)
        raise RunnerError(child.stderr.strip() or "capture worker failed")
    result_path = output / "worker-result.json"
    if not result_path.is_file():
        _status_file(output, "incomplete_core_error", "worker omitted its result", metadata)
        raise RunnerError("capture worker omitted its result")
    result = json.loads(result_path.read_text())
    if args.mode == "estimate":
        sample_frames = result["frames_run"]
        sample_bytes = result["stream_bytes"]
        elapsed = max(float(result["elapsed_seconds"]), 1e-9)
        estimated_bytes = (sample_bytes * args.frames + sample_frames - 1) // sample_frames
        estimated_seconds = elapsed * args.frames / sample_frames
        estimate = {
            "status": "complete",
            "metadata": metadata,
            "frames": args.frames,
            "sample_frames": sample_frames,
            "events_per_second": result["events"] / elapsed,
            "bytes_per_frame": sample_bytes / sample_frames,
            "estimated_bytes": estimated_bytes,
            "estimated_seconds": estimated_seconds,
        }
        (output / "estimate.json").write_bytes(nmdh_trace.canonical_json(estimate))
        print("decomp-run: preflight estimate complete (capture still enforces runtime caps)")
    else:
        print("decomp-run: capture complete")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker-mode", choices=("estimate", "capture"), help=argparse.SUPPRESS)
    parser.add_argument("--identity-json", help=argparse.SUPPRESS)
    parser.add_argument("--scenario-frames", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--mode", choices=("estimate", "capture"))
    parser.add_argument("--core", required=True)
    parser.add_argument("--rom", required=True)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--estimate-file")
    parser.add_argument("--out", required=True)
    parser.add_argument("--max-seconds", type=float, default=420)
    parser.add_argument("--max-bytes", type=int, default=268435456)
    args = parser.parse_args(argv)
    try:
        if args.worker_mode:
            if args.mode or not args.identity_json:
                raise RunnerError("invalid worker invocation")
            return _worker(args)
        if not args.mode:
            parser.error("--mode is required")
        return _run_parent(args)
    except (OSError, ValueError, RunnerError, nmdh_trace.NMDHError) as exc:
        print("decomp-run: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
