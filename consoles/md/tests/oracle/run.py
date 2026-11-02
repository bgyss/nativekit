#!/usr/bin/env python3
"""Exploratory libretro runner; not yet the clocked nativemd oracle."""
import argparse
import ctypes as c
import hashlib
import json
import pathlib
import sys
from trace import Event, Writer, M68K, READ, WRITE

TRACE_M68K_CPU = 1 << 0


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


class Variable(c.Structure):
    _fields_ = [("key", c.c_char_p), ("value", c.c_char_p)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=pathlib.Path, required=True)
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--input", type=pathlib.Path, help="JSON: frame numbers to port/button lists")
    parser.add_argument("--pad1", choices=("3", "6"), default="6")
    parser.add_argument("--pad2", choices=("3", "6"), default="6")
    parser.add_argument("--trace", action="store_true", help="require the candidate M68K bus trace hook")
    args = parser.parse_args()
    if args.frames < 1 or not args.core.is_file() or not args.rom.is_file():
        parser.error("core and ROM must exist, and frames must be positive")
    buttons = {"A": 0, "X": 1, "MODE": 2, "START": 3, "UP": 4, "DOWN": 5,
               "LEFT": 6, "RIGHT": 7, "B": 8, "Y": 9, "Z": 10, "C": 11}
    script = {}
    if args.input:
        raw = json.loads(args.input.read_text())
        if not isinstance(raw, dict):
            parser.error("input must be an object of frame numbers")
        for key, ports in raw.items():
            if not key.isdecimal() or str(int(key)) != key or not 1 <= int(key) <= args.frames:
                parser.error("input frame numbers must be canonical and in range")
            if not isinstance(ports, dict) or any(port not in ("1", "2") for port in ports):
                parser.error("input ports must be 1 or 2")
            frame = {}
            for port, names in ports.items():
                if not isinstance(names, list) or any(not isinstance(n, str) or n not in buttons for n in names):
                    parser.error("input contains an unknown button")
                if (args.pad1 if port == "1" else args.pad2) == "3" and any(
                        name in ("X", "Y", "Z", "MODE") for name in names):
                    parser.error("three-button pads do not have X, Y, Z or MODE")
                frame[int(port) - 1] = {buttons[name] for name in names}
            script[int(key)] = frame
    if args.out.exists() and any(args.out.iterdir()):
        raise RuntimeError("output directory must be empty")
    args.out.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    with args.core.open("rb") as core_file:
        for block in iter(lambda: core_file.read(1024 * 1024), b""):
            digest.update(block)
    core = c.CDLL(str(args.core.resolve()))
    options = {b"blastem_system_type": b"gen", b"blastem_model": b"md1va3",
               b"blastem_region": b"U", b"blastem_force_region": b"on",
               b"blastem_ram_init": b"zero", b"blastem_m68k_divider": b"7",
               b"blastem_megawifi": b"off", b"blastem_io_1": f"gamepad{args.pad1}.1".encode(),
               b"blastem_io_2": f"gamepad{args.pad2}.2".encode(), b"blastem_lowpass_cutoff": b"24000",
               b"blastem_gain": b"0.0", b"blastem_fm_gain": b"0.0", b"blastem_psg_gain": b"0.0",
               b"blastem_fm_dac": b"zero_offset", b"blastem_overscan_top": b"0",
               b"blastem_overscan_bottom": b"0", b"blastem_overscan_left": b"0",
               b"blastem_overscan_right": b"0"}
    directory = str(args.out.resolve()).encode()
    pixel_format = [None]
    frame_count = [0]
    frame_sizes = []
    audio = bytearray()
    error = []
    queried = set()
    advertised = {}
    current_input = [{}]
    trace_error = []
    trace_writer = None
    trace_file = None
    emit_error = None

    if args.trace:
        try:
            trace_version = core.nmd_trace_version
            trace_capabilities = core.nmd_trace_capabilities
            register_trace = core.nmd_trace_register
            reset_trace = core.nmd_trace_reset
            emit_error = core.nmd_trace_error
        except AttributeError as exc:
            raise RuntimeError("core does not export the trace hook ABI") from exc
        trace_version.restype = c.c_uint32
        trace_capabilities.restype = c.c_uint64
        if trace_version() != 1 or trace_capabilities() & TRACE_M68K_CPU != TRACE_M68K_CPU:
            raise RuntimeError("core lacks 68000 CPU-access trace capability")
        trace_cb_type = c.CFUNCTYPE(None, c.c_void_p, c.c_uint64, c.c_uint8, c.c_uint8,
                                    c.c_uint8, c.c_uint32, c.c_uint32)

        @trace_cb_type
        def on_trace(user, clock, device, operation, width, address, value):
            try:
                trace_writer.emit(Event(clock, device, operation, width, address, value))
            except (OSError, ValueError) as exc:
                trace_error.append(str(exc))

        register_trace.argtypes = [c.c_uint32, trace_cb_type, c.c_void_p]
        register_trace.restype = c.c_int
        if not register_trace(1, on_trace, None):
            raise RuntimeError("core refused trace ABI version 1")
        emit_error.restype = c.c_int
        reset_trace()
        trace_file = (args.out / "bus.trace").open("w+b")
        trace_writer = Writer(trace_file)

    @c.CFUNCTYPE(c.c_bool, c.c_uint, c.c_void_p)
    def environment(command, data):
        if command in (9, 31):  # system and save directories
            c.cast(data, c.POINTER(c.c_char_p))[0] = directory
            return True
        if command == 3:  # A null frame is not currently handled as a duplicate.
            c.cast(data, c.POINTER(c.c_bool))[0] = False
            return True
        if command == 10:
            pixel_format[0] = c.cast(data, c.POINTER(c.c_uint))[0]
            return pixel_format[0] == 1  # XRGB8888
        if command == 15:
            var = c.cast(data, c.POINTER(Variable)).contents
            value = options.get(var.key)
            if value is None:
                return False
            queried.add(var.key)
            var.value = value
            return True
        if command == 16:  # legacy core-option definitions, terminated by a null key
            entries = c.cast(data, c.POINTER(Variable))
            for index in range(1024):
                item = entries[index]
                if item.key is None:
                    return True
                if item.value and b"; " in item.value:
                    advertised[item.key] = set(item.value.split(b"; ", 1)[1].split(b"|"))
            error.append("unterminated core options")
            return False
        return False

    @c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t)
    def video(data, width, height, pitch):
        if (not data or pixel_format[0] != 1 or not 0 < width <= 4096 or
                not 0 < height <= 4096 or pitch < width * 4):
            error.append("missing or unsupported frame")
            return
        rows = bytearray()
        for y in range(height):
            row = c.string_at(data + y * pitch, width * 4)
            for x in range(width):
                pixel = int.from_bytes(row[x * 4:x * 4 + 4], sys.byteorder)
                rows.extend(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))
        try:
            (args.out / f"frame_{frame_count[0] + 1:06}.ppm").write_bytes(
                f"P6\n{width} {height}\n255\n".encode() + rows)
        except OSError:
            error.append("could not write frame")
            return
        frame_count[0] += 1
        frame_sizes.append([width, height])

    @c.CFUNCTYPE(c.c_size_t, c.POINTER(c.c_int16), c.c_size_t)
    def audio_batch(data, frames):
        audio.extend(c.string_at(data, frames * 4))
        return frames

    @c.CFUNCTYPE(None, c.c_int16, c.c_int16)
    def audio_sample(left, right):
        audio.extend(int(left).to_bytes(2, sys.byteorder, signed=True))
        audio.extend(int(right).to_bytes(2, sys.byteorder, signed=True))

    @c.CFUNCTYPE(None)
    def input_poll():
        pass

    @c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint)
    def input_state(port, device, index, button):
        return int(device == 1 and index == 0 and button in current_input[0].get(port, set()))

    for name, callback in (("retro_set_environment", environment), ("retro_set_video_refresh", video),
                           ("retro_set_audio_sample", audio_sample), ("retro_set_audio_sample_batch", audio_batch),
                           ("retro_set_input_poll", input_poll), ("retro_set_input_state", input_state)):
        getattr(core, name)(callback)
    unsupported = sorted(key.decode() for key, value in options.items() if value not in advertised.get(key, set()))
    if error or unsupported:
        raise RuntimeError("unsupported required options: " + ", ".join(unsupported))
    core.retro_init()
    core.retro_load_game.argtypes = [c.POINTER(Game)]
    core.retro_load_game.restype = c.c_bool
    game = Game(str(args.rom.resolve()).encode(), None, 0, None)
    if not core.retro_load_game(c.byref(game)):
        raise RuntimeError("core refused cartridge")
    info = AVInfo()
    core.retro_get_system_av_info(c.byref(info))
    for frame_number in range(1, args.frames + 1):
        current_input[0] = script.get(frame_number, {})
        core.retro_run()
        if args.trace and emit_error():
            trace_error.append("core trace error")
            break
    core.retro_unload_game()
    core.retro_deinit()
    if trace_error:
        raise RuntimeError("trace capture failed: " + "; ".join(trace_error))
    if trace_writer:
        trace_writer.close()
        trace_file.close()
    if set(options) != queried:
        raise RuntimeError("core did not request required options: " +
                           ", ".join(sorted(key.decode() for key in set(options) - queried)))
    if error or frame_count[0] != args.frames:
        raise RuntimeError("; ".join(error) or "frame count mismatch")
    if info.timing.sample_rate <= 0 or not audio:
        raise RuntimeError("missing audio or invalid sample rate")
    (args.out / "audio.raw").write_bytes(audio)
    button_names = {value: name for name, value in buttons.items()}
    recorded_input = {str(frame): {str(port + 1): [button_names[button] for button in sorted(values)]
                                   for port, values in sorted(ports.items())}
                      for frame, ports in sorted(script.items())}
    manifest = {"core_sha256": digest.hexdigest(), "frames": frame_count[0], "sample_rate": info.timing.sample_rate,
                "frame_sizes": frame_sizes,
                "audio_format": "s16-native-endian-stereo", "pixel_format": "XRGB8888",
                "options": {k.decode(): v.decode() for k, v in options.items()},
                "queried_options": sorted(key.decode() for key in queried),
                "options_advertised": True,
                "input": recorded_input,
                "input_semantics": "one-based frame snapshots; unspecified ports and frames released",
                "qualification": "exploratory; remaining core options use defaults"}
    if args.trace:
        manifest["trace_file"] = "bus.trace"
        manifest["trace_capabilities"] = ["m68k_cpu_access"]
        manifest["trace_events"] = trace_writer.count
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, json.JSONDecodeError) as exc:
        sys.exit(f"oracle: {exc}")
