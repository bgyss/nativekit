#!/usr/bin/env python3
"""The call segmenter over synthetic 68000 events (check_calls.segmenter).

The reference core keeps one exception or interrupt event at a time: an
interrupt taken inside a TRAP handler takes the return event, and the
TRAP's own RTE comes with none. The segmenter must still close the TRAP's
handler, so the call that took the TRAP pauses again at the next frame end.
A planted error (the return taken only from return events) must leave the
call without that pause.
"""
import importlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))

import check_calls  # noqa: E402

M68K = check_calls.M68K
ROUTINE = 0x1000


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def events():
    """A call to $1000 that takes TRAP #0; an interrupt lands in the handler;
    both handlers return (only the interrupt's return is reported); then the
    call runs on and the frame ends with it open."""
    sequence = [0]
    sp = [0xFFFE00]
    clock = [1000]

    def state():
        return {"d": [0] * 8, "a": [0] * 7 + [sp[0]], "status": 0x27, "xflag": 0, "nflag": 0,
                "zflag": 0, "vflag": 0, "cflag": 0, "other_sp": 0}

    def event(kind, pc=0, next_pc=0, **payload):
        sequence[0] += 1
        clock[0] += 10
        return {"cpu": M68K, "kind": kind, "sequence": sequence[0], "master_clock": clock[0],
                "pc": pc, "next_pc": next_pc, "payload": payload}

    def instruction(pc, length, opcode, sp_after=None):
        if sp_after is not None:
            sp[0] = sp_after
        return [event(1, pc, pc + length, opcode=opcode, fetched_bytes=bytes((opcode >> 8, opcode & 0xFF)),
                      state=state()),
                event(2, pc, pc + length)]

    out = []
    out += instruction(0x200, 2, 0x6100, sp_after=0xFFFDFC)     # bsr: the call
    out += instruction(ROUTINE, 2, 0x4E71)                      # nop, in the routine
    out.append(event(5, ROUTINE + 2, ROUTINE + 2, phase=1))     # TRAP #0 taken
    out += instruction(0x3000, 2, 0x4E71, sp_after=0xFFFDF6)    # in the TRAP handler
    out.append(event(6, 0x3002, 0x3002, phase=1))               # an interrupt inside it
    out += instruction(0x4000, 2, 0x4E71, sp_after=0xFFFDF0)
    out += instruction(0x4002, 2, 0x4E73, sp_after=0xFFFDF6)    # its RTE
    out.append(event(6, 0x4002, 0x3002, phase=2))               # ... reported
    out += instruction(0x3002, 2, 0x4E73, sp_after=0xFFFDFC)    # the TRAP's RTE: no return event
    out += instruction(ROUTINE + 4, 2, 0x4E71)                  # back in the routine
    return out


def pauses(module):
    feed = module.segmenter({(M68K, ROUTINE)})
    for item in events():
        feed(item)
    feed.frame_end()
    # the open call's pauses: read by closing it
    sequence = 1000
    close = {"cpu": M68K, "kind": 1, "sequence": sequence, "master_clock": 99999,
             "pc": ROUTINE + 6, "next_pc": 0x202,
             "payload": {"opcode": 0x4E75, "fetched_bytes": b"\x4e\x75",
                         "state": {"d": [0] * 8, "a": [0] * 7 + [0xFFFE00], "status": 0x27,
                                   "xflag": 0, "nflag": 0, "zflag": 0, "vflag": 0, "cflag": 0,
                                   "other_sp": 0}}}
    calls = feed(close)
    require(len(calls) == 1 and calls[0].address == ROUTINE, "the call did not close: %r" % calls)
    return [pause["kind"] for pause in calls[0].pauses]


def main():
    kinds = pauses(check_calls)
    require(kinds == ["exception", "frame"],
            "the call should pause at its TRAP and at the frame end: %r" % kinds)
    source = pathlib.Path(check_calls.__file__)
    original = source.read_text()
    old = '''        if pending_rte[cpu] and kind != 2 and not (kind in (5, 6) and payload["phase"] != 1):'''
    require(original.count(old) == 1, "the plant no longer matches")
    try:
        source.write_text(original.replace(old, "        if False:"))
        planted = importlib.reload(check_calls)
        require(pauses(planted) == ["exception"],
                "the planted segmenter still closed the TRAP's handler")
    finally:
        source.write_text(original)
        importlib.reload(check_calls)
    print("segment_check: ok (a TRAP handler whose return the core does not report is closed "
          "at its RTE, and the call pauses at the next frame end; the planted error is caught)")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("segment_check: " + str(exc))
