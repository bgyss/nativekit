#!/usr/bin/env python3
"""Exercise the NMDH v1 C ABI through a real shared library."""
import ctypes as c
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[4]
abi_dir = root / "consoles/md/tests/oracle"
source = abi_dir / "decomp_abi.c"
header = abi_dir / "decomp_abi.h"
compiler = shlex.split(os.environ.get("CC", "/usr/bin/clang" if sys.platform == "darwin" else "cc"))
extension = "dylib" if sys.platform == "darwin" else "so"
link_flag = "-dynamiclib" if sys.platform == "darwin" else "-shared"
capabilities = (
    "M68K_INSTRUCTION", "M68K_STATE", "Z80_INSTRUCTION", "Z80_STATE",
    "FLOW_EDGE", "RESET_EXCEPTION_INTERRUPT", "FETCH_PROVENANCE",
    "BUS_INTERVALS", "MASTER_CLOCK", "SYNC_CHECKPOINT", "CONTROLLED_STOP",
    "REPLACE_HANDOFF",
)

if not source.is_file() or not header.is_file():
    sys.exit("abi_check: NMDH v1 ABI source/header are not implemented")


class Payload(c.Union):
    _fields_ = [("bytes", c.c_uint8 * 256)]


class Event(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("kind", c.c_uint32),
        ("cpu", c.c_uint32),
        ("flags", c.c_uint32),
        ("sequence", c.c_uint64),
        ("master_clock", c.c_uint64),
        ("epoch", c.c_uint64),
        ("pc", c.c_uint32),
        ("next_pc", c.c_uint32),
        ("payload", Payload),
    ]


class Stop(c.Structure):
    _fields_ = [("criterion", c.c_uint32), ("reason", c.c_uint32),
                ("value", c.c_uint64), ("instruction_count", c.c_uint64),
                ("stop_clock", c.c_uint64)]


def build_library(directory, enabled_capabilities):
    name = "decomp-empty" if not enabled_capabilities else "decomp-" + "-".join(enabled_capabilities)
    library = pathlib.Path(directory) / (name + "." + extension)
    command = [*compiler, "-std=c11", "-fPIC", link_flag, "-I", str(abi_dir)]
    if enabled_capabilities:
        command.extend("-DNMD_DECOMP_HAS_" + name for name in enabled_capabilities)
    command.extend([str(source), "-o", str(library)])
    subprocess.run(command, check=True, capture_output=True, text=True)
    return c.CDLL(str(library))


def configure(library):
    library.nmd_decomp_abi_version.restype = c.c_uint32
    library.nmd_decomp_capabilities.restype = c.c_uint64
    callback_type = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(Event))
    library.nmd_decomp_register.argtypes = [c.c_uint32, callback_type, c.c_void_p]
    library.nmd_decomp_register.restype = c.c_int
    library.nmd_decomp_reset.argtypes = []
    library.nmd_decomp_error.restype = c.c_int
    library.nmd_decomp_publish.argtypes = [c.POINTER(Event)]
    library.nmd_decomp_publish.restype = c.c_int
    library.nmd_decomp_stop_pending.restype = c.c_int
    library.nmd_decomp_stop_action.restype = c.c_uint32
    library.nmd_decomp_stop_cpu.restype = c.c_uint32
    library.nmd_decomp_stop_clock.restype = c.c_uint64
    library.nmd_decomp_should_stop_cpu.argtypes = [c.c_uint32, c.c_uint64]
    library.nmd_decomp_should_stop_cpu.restype = c.c_int
    library.nmd_decomp_set_stop_metadata.argtypes = [c.c_uint32, c.c_uint64, c.c_uint64]
    library.nmd_decomp_fill_stop.argtypes = [c.POINTER(Stop)]
    library.nmd_decomp_mark_handed_off.argtypes = []
    return callback_type


with tempfile.TemporaryDirectory() as temporary:
    empty = build_library(temporary, [])
    callback_type = configure(empty)
    assert empty.nmd_decomp_abi_version() == 1
    assert empty.nmd_decomp_capabilities() == 0

    instrumented = build_library(temporary, capabilities)
    configure(instrumented)
    assert instrumented.nmd_decomp_abi_version() == 1
    assert instrumented.nmd_decomp_capabilities() == 0xFFF

    m68k_only = build_library(temporary, [
        "M68K_INSTRUCTION", "M68K_STATE", "FLOW_EDGE", "FETCH_PROVENANCE",
    ])
    configure(m68k_only)
    assert m68k_only.nmd_decomp_capabilities() == 0x53

    events = []

    @callback_type
    def receive(user, event_pointer):
        event = event_pointer.contents
        events.append((event.sequence, event.kind, event.cpu, event.master_clock,
                       event.epoch, event.pc, event.next_pc,
                       bytes(event.payload.bytes[:4])))
        return 0

    assert empty.nmd_decomp_register(1, receive, None) == 1
    assert empty.nmd_decomp_register(2, receive, None) == 0
    assert empty.nmd_decomp_register(1, callback_type(), None) == 0

    alternate_events = []

    @callback_type
    def alternate_receive(user, event_pointer):
        alternate_events.append(event_pointer.contents.sequence)
        return 0

    assert empty.nmd_decomp_register(1, alternate_receive, None) == 0

    event = Event()
    event.struct_size = c.sizeof(Event)
    event.kind = 1  # instruction boundary
    event.cpu = 1   # 68000
    event.master_clock = 500
    event.epoch = 2
    event.pc = 0x200
    event.next_pc = 0x204
    event.payload.bytes[:4] = (0x4E, 0x71, 0xA5, 0x5A)
    assert empty.nmd_decomp_publish(c.byref(event)) == 0
    assert event.sequence == 1
    assert events == [(1, 1, 1, 500, 2, 0x200, 0x204, bytes((0x4E, 0x71, 0xA5, 0x5A)))]

    event.pc = 0x204
    event.next_pc = 0x208
    assert empty.nmd_decomp_publish(c.byref(event)) == 0
    assert event.sequence == 2
    assert events[-1][:7] == (2, 1, 1, 500, 2, 0x204, 0x208)
    assert alternate_events == []

    @callback_type
    def invalid_action(user, event_pointer):
        return 99

    empty.nmd_decomp_reset()
    assert empty.nmd_decomp_register(1, invalid_action, None) == 1
    assert empty.nmd_decomp_publish(c.byref(event)) == 1
    assert empty.nmd_decomp_error() != 0
    empty.nmd_decomp_reset()
    assert empty.nmd_decomp_error() == 0
    assert empty.nmd_decomp_register(1, receive, None) == 1
    assert empty.nmd_decomp_publish(c.byref(event)) == 0
    assert event.sequence == 1

    @callback_type
    def replace_request(user, event_pointer):
        empty.nmd_decomp_set_stop_metadata(4, 0x1234, 17)
        return 2

    empty.nmd_decomp_reset()
    assert empty.nmd_decomp_register(1, replace_request, None) == 1
    event.kind = 1
    event.cpu = 2
    event.master_clock = 720
    assert empty.nmd_decomp_publish(c.byref(event)) == 2
    assert empty.nmd_decomp_stop_pending() == 1
    assert empty.nmd_decomp_stop_action() == 2
    assert empty.nmd_decomp_stop_cpu() == 2
    assert empty.nmd_decomp_stop_clock() == 720
    assert empty.nmd_decomp_should_stop_cpu(2, 0) == 1
    assert empty.nmd_decomp_should_stop_cpu(1, 719) == 0
    assert empty.nmd_decomp_should_stop_cpu(1, 720) == 1
    stop = Stop()
    empty.nmd_decomp_fill_stop(c.byref(stop))
    assert (stop.criterion, stop.reason, stop.value, stop.instruction_count,
            stop.stop_clock) == (4, 2, 0x1234, 17, 720)
    empty.nmd_decomp_mark_handed_off()
    assert empty.nmd_decomp_should_stop_cpu(1, 0) == 1

print("decomp ABI checks: ok")
