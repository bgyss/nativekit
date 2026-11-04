#!/usr/bin/env python3
"""Exercise the candidate hook ABI independently of any emulator."""
import ctypes as c
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent
compiler = shlex.split(os.environ.get("CC", "/usr/bin/clang" if sys.platform == "darwin" else "cc"))
with tempfile.TemporaryDirectory() as temp:
    library = pathlib.Path(temp) / ("trace.dylib" if sys.platform == "darwin" else "trace.so")
    subprocess.run([*compiler, "-std=c11", "-fPIC", "-dynamiclib" if sys.platform == "darwin" else "-shared",
                    str(root / "oracle/trace_abi.c"), "-o", str(library)], check=True, capture_output=True)
    sink = c.CDLL(str(library))
    sink.nmd_trace_capabilities.restype = c.c_uint64
    assert sink.nmd_trace_capabilities() == 1
    instrumented = pathlib.Path(temp) / ("trace-instrumented.dylib" if sys.platform == "darwin" else "trace-instrumented.so")
    features = ["NMD_TRACE_HAS_VDP_PORT", "NMD_TRACE_HAS_PSG_WRITE", "NMD_TRACE_HAS_YM_WRITE",
                "NMD_TRACE_HAS_YM_PORT", "NMD_TRACE_HAS_Z80_CPU", "NMD_TRACE_HAS_VDP_DMA_68K",
                "NMD_TRACE_HAS_CHIP_AUDIO", "NMD_TRACE_HAS_VDP_DMA_FILL", "NMD_TRACE_HAS_VDP_DMA_COPY",
                "NMD_TRACE_HAS_VDP_HINT_LINE", "NMD_TRACE_HAS_CHIP_AUDIO_SOURCE"]
    subprocess.run([*compiler, "-std=c11", "-fPIC", "-dynamiclib" if sys.platform == "darwin" else "-shared",
                    *["-D" + feature for feature in features], str(root / "oracle/trace_abi.c"),
                    "-o", str(instrumented)], check=True, capture_output=True)
    instrumented_sink = c.CDLL(str(instrumented))
    instrumented_sink.nmd_trace_capabilities.restype = c.c_uint64
    assert instrumented_sink.nmd_trace_capabilities() == 0xfff
    audio_callback_type = c.CFUNCTYPE(None, c.c_void_p, c.c_uint8, c.c_int16, c.c_int16)
    audio_events = []

    @audio_callback_type
    def receive_audio(user, device, left, right):
        audio_events.append((device, left, right))

    instrumented_sink.nmd_trace_audio_register.argtypes = [c.c_uint32, audio_callback_type, c.c_void_p]
    instrumented_sink.nmd_trace_audio_register.restype = c.c_int
    instrumented_sink.nmd_trace_audio_active.restype = c.c_int
    instrumented_sink.nmd_trace_audio_emit.argtypes = [c.c_uint8, c.c_int16, c.c_int16]
    instrumented_sink.nmd_trace_audio_source_emit.argtypes = [c.c_uint8, c.c_int16, c.c_int16]
    assert instrumented_sink.nmd_trace_audio_register(1, receive_audio, None) == 0
    assert instrumented_sink.nmd_trace_audio_register(2, receive_audio, None) == 1
    assert instrumented_sink.nmd_trace_audio_active() == 1
    instrumented_sink.nmd_trace_audio_emit(5, 123, -456)
    assert audio_events == [(5, 123, -456)]
    instrumented_sink.nmd_trace_audio_source_emit(4, 321, -654)
    assert audio_events == [(5, 123, -456), (6, 321, -654)]
    instrumented_sink.nmd_trace_audio_source_emit(5, 222, -333)
    assert audio_events[-1] == (7, 222, -333)
    instrumented_sink.nmd_trace_audio_source_emit(4, 111, -222)
    assert audio_events[-1] == (6, 111, -222)
    callback_type = c.CFUNCTYPE(None, c.c_void_p, c.c_uint64, c.c_uint8, c.c_uint8, c.c_uint8,
                               c.c_uint32, c.c_uint32)
    events = []

    @callback_type
    def receive(user, clock, device, operation, width, address, value):
        events.append((clock, device, operation, width, address, value))

    sink.nmd_trace_register.argtypes = [c.c_uint32, callback_type, c.c_void_p]
    sink.nmd_trace_register.restype = c.c_int
    sink.nmd_trace_emit.argtypes = [c.c_uint32, c.c_uint8, c.c_uint8, c.c_uint8, c.c_uint32, c.c_uint32]
    sink.nmd_trace_adjust.argtypes = [c.c_uint64]
    sink.nmd_trace_error.restype = c.c_int
    assert sink.nmd_trace_version() == 2
    assert sink.nmd_trace_register(1, receive, None) == 0
    assert sink.nmd_trace_register(2, receive, None) == 1
    sink.nmd_trace_reset()
    sink.nmd_trace_emit(1000, 1, 2, 2, 0x100, 0x1234)
    sink.nmd_trace_emit(950, 2, 2, 1, 0x0000, 0x3E)
    sink.nmd_trace_adjust(900)
    sink.nmd_trace_emit(105, 1, 2, 1, 0x102, 0x56)
    sink.nmd_trace_emit(55, 2, 1, 1, 0x0000, 0x3E)
    assert events == [(1000, 1, 2, 2, 0x100, 0x1234), (950, 2, 2, 1, 0x0000, 0x3E),
                      (1005, 1, 2, 1, 0x102, 0x56), (955, 2, 1, 1, 0x0000, 0x3E)]
    assert sink.nmd_trace_error() == 0
    sink.nmd_trace_emit(10, 3, 5, 1, 120, 1)
    assert events[-1] == (910, 3, 5, 1, 120, 1)
    assert sink.nmd_trace_error() == 0
    sink.nmd_trace_emit(99, 1, 2, 1, 0, 0)
    assert sink.nmd_trace_error() != 0 and len(events) == 5
    sink.nmd_trace_reset()
    sink.nmd_trace_emit(1, 255, 2, 1, 0, 0)
    assert sink.nmd_trace_error() != 0 and len(events) == 5
    sink.nmd_trace_reset()
    sink.nmd_trace_adjust((1 << 64) - 1)
    sink.nmd_trace_adjust(1)
    assert sink.nmd_trace_error() != 0
print("trace_abi_check: ok")
