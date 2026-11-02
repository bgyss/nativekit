#!/usr/bin/env python3
"""Exercise the candidate hook ABI independently of any emulator."""
import ctypes as c
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as temp:
    library = pathlib.Path(temp) / ("trace.dylib" if sys.platform == "darwin" else "trace.so")
    subprocess.run(["cc", "-std=c11", "-fPIC", "-dynamiclib" if sys.platform == "darwin" else "-shared",
                    str(root / "oracle/trace_abi.c"), "-o", str(library)], check=True, capture_output=True)
    sink = c.CDLL(str(library))
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
    assert sink.nmd_trace_version() == 1
    assert sink.nmd_trace_register(2, receive, None) == 0
    assert sink.nmd_trace_register(1, receive, None) == 1
    sink.nmd_trace_reset()
    sink.nmd_trace_emit(1000, 1, 2, 2, 0x100, 0x1234)
    sink.nmd_trace_adjust(900)
    sink.nmd_trace_emit(105, 1, 2, 1, 0x102, 0x56)
    assert events == [(1000, 1, 2, 2, 0x100, 0x1234), (1005, 1, 2, 1, 0x102, 0x56)]
    assert sink.nmd_trace_error() == 0
    sink.nmd_trace_emit(99, 1, 2, 1, 0, 0)
    assert sink.nmd_trace_error() != 0 and len(events) == 2
    sink.nmd_trace_reset()
    sink.nmd_trace_emit(1, 255, 2, 1, 0, 0)
    assert sink.nmd_trace_error() != 0 and len(events) == 2
    sink.nmd_trace_reset()
    sink.nmd_trace_adjust((1 << 64) - 1)
    sink.nmd_trace_adjust(1)
    assert sink.nmd_trace_error() != 0
print("trace_abi_check: ok")
