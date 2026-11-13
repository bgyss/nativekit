#!/usr/bin/env python3
"""Check C routines against the original's recorded calls (D04, per call).

    check_calls.py --rom ROM --stream capture.nmdh --library hybrid.dylib --out calls.json

The stream is a full NMDH capture or a light capture's `window.nmdh`. The
original ran untouched. Every time it entered a routine the hybrid library
has C for, the call is cut from the stream: the registers at entry, the
routine's bus accesses in order, the calls it made, and the registers when
it returned. The C is then run on the entry registers with `check/replay.c`
standing in for the machine, and must end with the same registers, flags and
return address, the same memory, the same hardware accesses in the same order
with the same values, and the same calls.

A call is one entry of a routine up to the `rts`/`ret` that leaves the stack
where the entry found it. Verdicts, per routine:

  IDENTICAL   at least one call was judged and every judged call matched
  DIVERGENT   a judged call differed; the first difference is reported
  UNVERIFIED  it was called, but no call could be judged
  UNCOVERED   no call of it is in the stream

A call is not judged when an interrupt or exception was taken during it, when
the 68000 wrote Z80 RAM during a Z80 call, when it did not return by its own
`rts`/`ret`, when the C used a wait the replay cannot serve, or when the C
called a routine that has no C (the record stands in, and is counted).
Memory below the stack pointer at the return is not compared.
"""
import argparse
import ctypes
import hashlib
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import hybrid  # noqa: E402
import trace as nmdh_trace  # noqa: E402

SCHEMA = "nmd-calls"
VERSION = 1
M68K, Z80 = 1, 2
CPU_NAMES = {M68K: "m68k", Z80: "z80"}
WORK, ZRAM = 0, 1
LINK = {M68K: 4, Z80: 2}
FETCH = 1
NEEDED = {"bus accesses": 1 << 7, "interrupt events": 1 << 5}
STATUS = {1: ("hardware", "access"), 2: ("hardware", "value"), 3: ("hardware", "past the record"),
          4: ("memory", "read the original did not make"), 5: ("memory", "write to ROM"),
          6: ("memory", "unmapped address"), 7: ("memory", "odd address"),
          9: ("call", "no such call in the record")}
UNSUPPORTED = 8


class CheckError(RuntimeError):
    """The inputs cannot be checked."""


class Access(ctypes.Structure):
    _fields_ = [("address", ctypes.c_uint32), ("value", ctypes.c_uint32),
                ("width", ctypes.c_uint8), ("write", ctypes.c_uint8)]


class MadeCall(ctypes.Structure):
    _fields_ = [("address", ctypes.c_uint32), ("cpu", ctypes.c_uint8),
                ("fallback", ctypes.c_uint8)]


class Result(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int), ("address", ctypes.c_uint32),
                ("width", ctypes.c_uint32), ("value", ctypes.c_uint32), ("write", ctypes.c_int),
                ("hardware_index", ctypes.c_uint32), ("hardware_consumed", ctypes.c_uint32),
                ("accesses", ctypes.c_uint32), ("fallbacks", ctypes.c_uint32)]


class CpuState(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8), ("pc", ctypes.c_uint32),
                ("sr", ctypes.c_uint16), ("usp", ctypes.c_uint32), ("ssp", ctypes.c_uint32)]


class Z80State(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint8) for name in
                ("a", "f", "b", "c", "d", "e", "h", "l", "i", "r", "iff1", "iff2", "im")] + \
               [(name, ctypes.c_uint16) for name in ("ix", "iy", "sp", "pc")] + \
               [(name, ctypes.c_uint8) for name in
                ("a2", "f2", "b2", "c2", "d2", "e2", "h2", "l2")]


FALLBACK = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_uint32)


# ---- the original's state, as the C sees it ----

def m68k_registers(state, pc):
    """The reference snapshot as the contract's register names and values."""
    ccr = (bool(state["xflag"]) << 4 | bool(state["nflag"]) << 3 | bool(state["zflag"]) << 2 |
           bool(state["vflag"]) << 1 | bool(state["cflag"]))
    values = {"d%d" % index: value for index, value in enumerate(state["d"])}
    values.update({"a%d" % index: value for index, value in enumerate(state["a"])})
    values.update(sr=(state["status"] << 8 | ccr) & 0xFFFF, other_sp=state["other_sp"], pc=pc)
    return values


def z80_registers(state, pc):
    b, c, d, e, h, l, _, a = state["main"]
    flags = ((state["last_flag_result"] & 0xA8) | (0x40 if state["zflag"] else 0) |
             (0x10 if state["chflags"] & 0x08 else 0) | (0x04 if state["pvflag"] else 0) |
             (0x02 if state["nflag"] else 0) | (0x01 if state["chflags"] & 0x80 else 0))
    b2, c2, d2, e2, h2, l2, f2, a2 = state["alt"]
    return dict(a=a, f=flags, b=b, c=c, d=d, e=e, h=h, l=l, a2=a2, f2=f2, b2=b2, c2=c2,
                d2=d2, e2=e2, h2=h2, l2=l2, ix=state["ix"], iy=state["iy"], sp=state["sp"],
                i=state["i"], iff1=int(bool(state["iff1"])), iff2=int(bool(state["iff2"])),
                im=state["imode"], pc=pc)


REGISTERS = {M68K: m68k_registers, Z80: z80_registers}


def stack_pointer(cpu, state):
    return state["a"][7] if cpu == M68K else state["sp"]


def _is_call(cpu, opcode):
    if cpu == M68K:
        return opcode & 0xFF00 == 0x6100 or opcode & 0xFFC0 == 0x4E80
    return opcode == 0xCD or opcode & 0xC7 in (0xC4, 0xC7)


def _is_return(cpu, opcode, fetched):
    if cpu == M68K:
        return opcode == 0x4E75
    if opcode == 0xED:
        return len(fetched) > 1 and fetched[1] in (0x45, 0x4D)
    return opcode == 0xC9 or opcode & 0xC7 == 0xC0


# ---- cutting calls out of the stream ----

class Frame:
    """One open call of the original."""
    __slots__ = ("cpu", "address", "entry_sp", "entry", "start", "children", "tree",
                 "reasons", "low_sp", "sequence")

    def __init__(self, cpu, address, entry, start, sequence):
        self.cpu, self.address, self.entry, self.start = cpu, address, entry, start
        self.entry_sp = self.low_sp = stack_pointer(cpu, entry)
        self.children = []      # direct children: (address, start, end, exit registers)
        self.tree = []          # every descendant in call order: (address, descendants)
        self.reasons = set()
        self.sequence = sequence


class Call:
    """A closed call of a routine that has C."""

    def __init__(self, frame, accesses, exit_state, return_pc):
        self.cpu, self.address, self.entry = frame.cpu, frame.address, frame.entry
        self.accesses = accesses                    # (address, width, value, write)
        self.children = [(address, start - frame.start, end - frame.start, registers)
                         for address, start, end, registers in frame.children]
        self.tree = frame.tree
        self.reasons = frame.reasons
        self.exit = REGISTERS[frame.cpu](exit_state, return_pc)
        self.exit_sp = stack_pointer(frame.cpu, exit_state)
        self.low_sp = frame.low_sp
        self.sequence = frame.sequence


def calls(stream_path, routines):
    """Yield every closed call of the `(cpu, address)` in `routines`."""
    state = {M68K: None, Z80: None}
    entered_by_call = {M68K: False, Z80: False}
    accesses = {M68K: [], Z80: []}
    mark = {M68K: 0, Z80: 0}            # accesses[cpu][mark:] belong to the instruction under way
    stack = {M68K: [], Z80: []}
    handlers = {M68K: [], Z80: []}      # stack depth at each open interrupt or exception
    for event in nmdh_trace.read_nmdh(stream_path):
        cpu, kind, payload = event["cpu"], event["kind"], event["payload"]
        if kind == 3:
            if payload["provenance"] != FETCH:
                write = payload["operation"] == 2
                accesses[cpu].append((payload["address"], payload["width"], payload["value"], write))
                if cpu == M68K and write and 0xA00000 <= payload["address"] < 0xA10000:
                    for frame in stack[Z80]:
                        frame.reasons.add("foreign_write")
        elif kind in (5, 6):
            if payload["phase"] == 1:
                for frame in stack[cpu]:
                    frame.reasons.add("interrupt")
                handlers[cpu].append(len(stack[cpu]))
            elif handlers[cpu]:
                del stack[cpu][handlers[cpu].pop():]    # calls a handler left open
        elif kind == 4:
            state[cpu], entered_by_call[cpu] = None, False
            stack[cpu].clear()
            handlers[cpu].clear()
        elif kind == 1:
            before, after = state[cpu], payload["state"]
            frames = stack[cpu]
            if before is not None and (entered_by_call[cpu] or (cpu, event["pc"]) in routines):
                frames.append(Frame(cpu, event["pc"], before, mark[cpu], event["sequence"]))
            mark[cpu] = len(accesses[cpu])
            after_sp = stack_pointer(cpu, after)
            for frame in frames:
                frame.low_sp = min(frame.low_sp, after_sp)
            opcode = payload["opcode"]
            if frames and _is_return(cpu, opcode, payload["fetched_bytes"]):
                level = (after_sp - LINK[cpu]) & (0xFFFFFFFF if cpu == M68K else 0xFFFF)
                depth = next((index for index, frame in enumerate(frames)
                              if frame.entry_sp == level), None)
                if depth is not None:
                    closing = frames[depth:]
                    del frames[depth:]
                    # frames above the first at this stack level never returned by themselves
                    inner = None
                    for frame in reversed(closing):
                        if frame.entry_sp != level:
                            frame.reasons.add("irregular_return")
                        if inner is not None:
                            frame.tree.append((inner.address, len(inner.tree)))
                            frame.tree.extend(inner.tree)
                            frame.reasons |= inner.reasons & {"irregular_return"}
                        end = len(accesses[cpu])
                        registers = REGISTERS[cpu](after, event["next_pc"])
                        if inner is not None:
                            frame.children.append((inner.address, inner.start, end, registers))
                        if (cpu, frame.address) in routines:
                            yield Call(frame, accesses[cpu][frame.start:end], after,
                                       event["next_pc"])
                        inner = frame
                    if frames:
                        parent = frames[-1]
                        parent.tree.append((inner.address, len(inner.tree)))
                        parent.tree.extend(inner.tree)
                        parent.children.append((inner.address, inner.start,
                                                len(accesses[cpu]),
                                                REGISTERS[cpu](after, event["next_pc"])))
                    else:
                        accesses[cpu].clear()
                        mark[cpu] = 0
            entered_by_call[cpu] = (
                before is not None and _is_call(cpu, opcode) and
                after_sp == (stack_pointer(cpu, before) - LINK[cpu]) &
                (0xFFFFFFFF if cpu == M68K else 0xFFFF))
            state[cpu] = after


# ---- replaying one call through the C ----

class Checker:
    def __init__(self, rom, library_path, contracts=None):
        self.rom = bytes(rom)
        self.rom_buffer = (ctypes.c_uint8 * len(self.rom)).from_buffer_copy(self.rom)
        self.library = library = ctypes.CDLL(str(library_path))
        self.routines = {(M68K if cpu == "m68k" else Z80, address): (function, name)
                         for cpu, address, function, name in hybrid.tables(library)}
        self.contracts = contracts or {}
        library.nmd_check_run.restype = ctypes.POINTER(Result)
        library.nmd_check_run.argtypes = [ctypes.c_void_p]
        library.nmd_check_written.restype = ctypes.POINTER(ctypes.c_uint32)
        library.nmd_check_exit_byte.restype = ctypes.c_int
        self.cpu_state = CpuState.in_dll(library, "nmd_cpu")
        self.z80_state = Z80State.in_dll(library, "nmd_z80")

    def _load(self, cpu, registers):
        if cpu == M68K:
            target = self.cpu_state
            for index in range(8):
                target.d[index] = registers["d%d" % index]
                target.a[index] = registers["a%d" % index]
            target.sr, target.pc = registers["sr"], registers["pc"]
            if registers["sr"] & 0x2000:
                target.usp = registers["other_sp"]
            else:
                target.ssp = registers["other_sp"]
        else:
            for name, value in registers.items():
                setattr(self.z80_state, name, value)

    def _read(self, cpu):
        if cpu == M68K:
            source = self.cpu_state
            values = {"d%d" % index: source.d[index] for index in range(8)}
            values.update({"a%d" % index: source.a[index] for index in range(8)})
            values.update(sr=source.sr, pc=source.pc,
                          other_sp=source.usp if source.sr & 0x2000 else source.ssp)
            return values
        return {name: getattr(self.z80_state, name) for name, _ in Z80State._fields_
                if name != "r"}

    def _ram(self, cpu, address):
        """The RAM offset of a CPU address, or None when it is not RAM."""
        if cpu == M68K:
            address &= 0xFFFFFF
            return address & 0xFFFF if address >= 0xE00000 else None
        return address if address < 0x2000 else None

    def _is_rom(self, cpu, address):
        return cpu == M68K and (address & 0xFFFFFF) < len(self.rom)

    def check(self, call):
        """One call's outcome: {"result", "reason" | "difference", "compared", "fallbacks"}."""
        compared = {"registers": 0, "hardware_accesses": 0, "memory_bytes": 0, "calls": 0}
        outcome = {"compared": compared, "fallbacks": 0}
        if call.reasons:
            return dict(outcome, result="UNVERIFIED", reason=sorted(call.reasons)[0])
        cpu, library = call.cpu, self.library
        space = WORK if cpu == M68K else ZRAM
        library.nmd_check_reset(self.rom_buffer, len(self.rom))
        touched, final, hardware = set(), {}, []
        hardware_at = []                      # hardware entries before each access
        ram_writes = []                       # (access index, offset, byte)
        for index, (address, width, value, write) in enumerate(call.accesses):
            hardware_at.append(len(hardware))
            offset = self._ram(cpu, address)
            if offset is None:
                if not self._is_rom(cpu, address):
                    hardware.append(Access(address & 0xFFFFFF, value, width, write))
                continue
            for step in range(width):
                byte = (value >> (8 * (width - 1 - step))) & 0xFF
                at = (offset + step) & (0xFFFF if cpu == M68K else 0x1FFF)
                if write:
                    final[at] = byte
                    ram_writes.append((index, at, byte))
                elif at not in touched:
                    library.nmd_check_entry_byte(space, at, byte)
                touched.add(at)
        hardware_at.append(len(hardware))
        records = (Access * max(1, len(hardware)))(*hardware)
        library.nmd_check_hardware(cpu, records, len(hardware))
        cursor = [0]

        def stand_in(wanted_cpu, address):
            for position in range(cursor[0], len(call.children)):
                child_address, start, end, registers = call.children[position]
                if child_address != address or wanted_cpu != cpu:
                    continue
                if library.nmd_check_hardware_next() != hardware_at[start]:
                    return 0
                library.nmd_check_hardware_skip(hardware_at[end] - hardware_at[start])
                for index, at, byte in ram_writes:
                    if start <= index < end:
                        library.nmd_check_ram_store(space, at, byte)
                self._load(cpu, registers)
                cursor[0] = position + 1
                return 1
            return 0

        handler = FALLBACK(stand_in)
        library.nmd_check_set_fallback(handler)
        self._load(cpu, dict(REGISTERS[cpu](call.entry, call.address)))
        result = library.nmd_check_run(self.routines[(cpu, call.address)][0]).contents
        outcome["fallbacks"] = result.fallbacks
        compared["hardware_accesses"] = result.hardware_consumed

        def differs(kind, **detail):
            return dict(outcome, result="DIVERGENT", difference=dict(detail, kind=kind))

        if result.status == UNSUPPORTED:
            return dict(outcome, result="UNVERIFIED", reason="unsupported")
        if result.status:
            kind, what = STATUS[result.status]
            detail = {"what": what, "address": result.address, "width": result.width,
                      "write": bool(result.write), "value": result.value}
            if kind == "hardware" and result.hardware_index < len(hardware):
                expected = hardware[result.hardware_index]
                detail["expected"] = {"address": expected.address, "width": expected.width,
                                      "write": bool(expected.write), "value": expected.value}
            return differs(kind, record_position=result.hardware_index, **detail)
        # the calls it made, against the original's
        made = ctypes.POINTER(MadeCall)()
        count = library.nmd_check_calls(ctypes.byref(made))
        position = 0
        for index in range(1, count):
            if position >= len(call.tree):
                return differs("call", what="a call the original did not make",
                               made=made[index].address)
            address, descendants = call.tree[position]
            if address != made[index].address:
                return differs("call", what="target", expected=address, made=made[index].address)
            position += 1 + (descendants if made[index].fallback else 0)
            compared["calls"] += 1
        if position < len(call.tree):
            return differs("call", what="a call the original made is missing",
                           expected=call.tree[position][0])
        if result.hardware_consumed != len(hardware):
            expected = hardware[result.hardware_consumed]
            return differs("hardware", what="an access the original made is missing",
                           record_position=result.hardware_consumed,
                           expected={"address": expected.address, "width": expected.width,
                                     "write": bool(expected.write), "value": expected.value})
        # registers, flags and the return
        ignore = set(self.contracts.get("%s:%06X" % (CPU_NAMES[cpu], call.address), {})
                     .get("ignore", ()))
        got = self._read(cpu)
        for name, expected in call.exit.items():
            if name in ignore:
                continue
            compared["registers"] += 1
            if got[name] != expected:
                return differs("register", register=name, expected=expected, got=got[name])
        # memory effects, apart from the dead stack
        mask = 0xFFFF if cpu == M68K else 0x1FFF

        def dead(at):
            low, high = call.low_sp & mask, call.exit_sp & mask
            return self._ram(cpu, call.exit_sp) is not None and low <= at < high

        written_count = ctypes.c_uint32()
        written = library.nmd_check_written(space, ctypes.byref(written_count))
        wrote = {written[index] for index in range(written_count.value)}
        for at, expected in sorted(final.items()):
            if dead(at):
                continue
            compared["memory_bytes"] += 1
            value = library.nmd_check_exit_byte(space, at)
            if value < 0:
                return differs("memory", what="a write the original made is missing",
                               offset=at, expected=expected)
            if value != expected:
                return differs("memory", what="value", offset=at, expected=expected, got=value)
        for at in sorted(wrote - set(final)):
            if not dead(at):
                return differs("memory", what="a write the original did not make", offset=at,
                               got=library.nmd_check_exit_byte(space, at))
        if result.fallbacks:
            return dict(outcome, result="UNVERIFIED", reason="fallback")
        return dict(outcome, result="IDENTICAL")


def run(rom_path, stream_path, library_path, out=None, contracts=None):
    """Check every call in the stream; return (and write) the verdict report."""
    rom = pathlib.Path(rom_path).read_bytes()
    try:
        metadata, _ = nmdh_trace._scan(stream_path)
    except (OSError, nmdh_trace.NMDHError) as exc:
        raise CheckError("stream unreadable: %s" % exc)
    rom_sha256 = hashlib.sha256(rom).hexdigest()
    if metadata["identities"]["rom_sha256"] != rom_sha256:
        raise CheckError("the stream was made from a different ROM")
    for label, bit in NEEDED.items():
        if not metadata["capabilities"] & bit:
            raise CheckError("the stream holds no %s: no call in it can be judged" % label)
    checker = Checker(rom, library_path, contracts)
    rows = {key: {"cpu": CPU_NAMES[key[0]], "address": key[1], "name": name, "calls": 0,
                  "identical": 0, "divergent": 0, "unverified": {}, "fallbacks": 0,
                  "compared": {"registers": 0, "hardware_accesses": 0, "memory_bytes": 0,
                               "calls": 0}}
            for key, (_, name) in checker.routines.items()}
    for call in calls(stream_path, set(checker.routines)):
        row = rows[(call.cpu, call.address)]
        outcome = checker.check(call)
        row["calls"] += 1
        row["fallbacks"] += outcome["fallbacks"]
        if outcome["result"] == "UNVERIFIED":
            row["unverified"][outcome["reason"]] = row["unverified"].get(outcome["reason"], 0) + 1
            continue
        for name, value in outcome["compared"].items():
            row["compared"][name] += value
        if outcome["result"] == "IDENTICAL":
            row["identical"] += 1
        else:
            row["divergent"] += 1
            row.setdefault("first_difference", dict(outcome["difference"],
                                                    call=row["calls"], sequence=call.sequence))
    for row in rows.values():
        row["verdict"] = ("UNCOVERED" if not row["calls"] else
                          "DIVERGENT" if row["divergent"] else
                          "IDENTICAL" if row["identical"] else "UNVERIFIED")
    report = {
        "schema": SCHEMA, "version": VERSION,
        "identities": dict(metadata["identities"], rom_sha256=rom_sha256,
                           library_sha256=hashlib.sha256(
                               pathlib.Path(library_path).read_bytes()).hexdigest()),
        "window": metadata.get("window"),
        "contracts": contracts or {},
        "routines": [rows[key] for key in sorted(rows)],
    }
    if out is not None:
        pathlib.Path(out).write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    return report


def summary(report):
    lines = []
    for row in report["routines"]:
        lines.append("%-10s %s $%06X %-16s calls %d identical %d divergent %d unverified %d" % (
            row["verdict"], row["cpu"], row["address"], row["name"], row["calls"],
            row["identical"], row["divergent"], sum(row["unverified"].values())))
        if "first_difference" in row:
            lines.append("           first difference: " +
                         json.dumps(row["first_difference"], sort_keys=True))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--rom", required=True)
    parser.add_argument("--stream", required=True, help="capture.nmdh or window.nmdh")
    parser.add_argument("--library", required=True, help="the library hybrid.py built")
    parser.add_argument("--out", required=True)
    parser.add_argument("--contracts", help="JSON: {\"m68k:000400\": {\"ignore\": [\"d1\"]}}")
    args = parser.parse_args()
    contracts = json.loads(pathlib.Path(args.contracts).read_text()) if args.contracts else None
    try:
        report = run(args.rom, args.stream, args.library, args.out, contracts)
    except (CheckError, hybrid.HybridError) as exc:
        sys.exit("check_calls: " + str(exc))
    print(summary(report))
    sys.exit(1 if any(row["verdict"] == "DIVERGENT" for row in report["routines"]) else 0)


if __name__ == "__main__":
    main()
