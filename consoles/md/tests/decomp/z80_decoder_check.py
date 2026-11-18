#!/usr/bin/env python3
"""Z80 decoder controls: every opcode of the unprefixed, CB, ED, DD, FD, DDCB
and FDCB tables against the pinned vasm, flow facts on an authored program,
and planted decoder errors.

Scope of the claim: 1,792 opcode positions, each with two sets of operand
bytes. A decode with text must reassemble to its bytes unless it is one of
three explained non-canonical families (a DD/FD prefix on an opcode it does
not change, a duplicate ED encoding, an indexed BIT with a register field);
those stay raw bytes in a skeleton. Opcodes with no assembler spelling are
decoded for their length only.
"""
import dataclasses
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))

import asmverify  # noqa: E402
import z80_dis  # noqa: E402

FILL_A = bytes.fromhex("05123456")
FILL_B = bytes.fromhex("FE80C3D2")
ED_DUPLICATES = {0x4C, 0x54, 0x5C, 0x64, 0x6C, 0x74, 0x7C,  # neg
                 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D,        # retn
                 0x4E, 0x66, 0x6E, 0x76, 0x7E,              # im
                 0x63, 0x6B}                                # ld (nn),hl / ld hl,(nn)


def cases(fill):
    for op in range(256):
        yield bytes([op]) + fill
        for prefix in (b"\xCB", b"\xED", b"\xDD", b"\xFD"):
            yield prefix + bytes([op]) + fill
        for prefix in (b"\xDD\xCB", b"\xFD\xCB"):
            yield prefix + fill[:1] + bytes([op]) + fill


def explained(data, text):
    if data[0] == 0xED:
        return data[1] in ED_DUPLICATES
    if data[0] in (0xDD, 0xFD):
        if data[1] == 0xCB:
            return (data[3] >> 6) == 1 and (data[3] & 7) != 6
        plain = z80_dis.decode(data, 1, 0)
        prefixed = z80_dis.decode(data, 0, 0)
        # the prefix changed nothing but the address a relative jump is taken from
        return plain is not None and plain.text is not None and (
            plain.text == prefixed.text or bool(plain.targets) and
            plain.text.split("$")[0] == prefixed.text.split("$")[0])
    return False


def sweep(decode, fill):
    """(positions, with text, canonical, unexplained, explained) for one fill."""
    items = []
    positions = 0
    for index, data in enumerate(cases(fill)):
        positions += 1
        address = 0x100 + (index * 8 & 0x7FFF)
        insn = decode(data, 0, address)
        if insn is None:
            raise AssertionError("no decode for " + data.hex())
        if insn.text is not None:
            items.append((address, data[:insn.length], insn.text, data))
    verdicts = asmverify.verify("z80", [item[:3] for item in items])
    bad = [(item[3], item[2]) for item, good in zip(items, verdicts) if not good]
    unexplained = [entry for entry in bad if not explained(*entry)]
    return positions, len(items), sum(verdicts), unexplained, len(bad) - len(unexplained)


FLOW_SOURCE = """
        org     $0000
start:  ld      sp,$1FF0
        call    sub
        jr      nz,skip
        jp      (hl)
skip:   djnz    skip
        jp      c,start
        rst     $38
        ld      a,(ix+5)
        bit     7,(iy-2)
        jp      (ix)
sub:    ld      (data),a
        reti
        ret     z
        jp      $1234
data:   db      0
"""
FLOW_EXPECTED = [
    (0x00, 3, "seq", [], False),
    (0x03, 3, "call", [0x18], False),
    (0x06, 2, "cond", [0x09], False),
    (0x08, 1, "jump", [], True),
    (0x09, 2, "cond", [0x09], False),
    (0x0B, 3, "cond", [0x00], False),
    (0x0E, 1, "call", [0x38], False),
    (0x0F, 3, "seq", [], False),
    (0x12, 4, "seq", [], False),
    (0x16, 2, "jump", [], True),
    (0x18, 3, "seq", [], False),
    (0x1B, 2, "ret", [], False),
    (0x1D, 1, "seq", [], False),
    (0x1E, 3, "jump", [0x1234], False),
]


def assemble(source, directory):
    path = pathlib.Path(directory)
    (path / "flow.s").write_text(source)
    result = subprocess.run(
        [str(asmverify.assembler_path("z80")), "-Fbin", "-quiet", "-o", "flow.bin", "flow.s"],
        cwd=path, stdin=subprocess.DEVNULL, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError("flow fixture did not assemble: " + result.stderr)
    return (path / "flow.bin").read_bytes()


def flow_facts(decode, program):
    facts = []
    offset = 0
    while offset < len(program) - 1:
        insn = decode(program, offset, offset)
        if insn is None:
            raise AssertionError("flow fixture: no decode at $%X" % offset)
        facts.append((offset, insn.length, insn.flow, list(insn.targets), insn.indirect))
        offset += insn.length
    return facts


def planted(kind):
    def decode(data, offset, address):
        insn = z80_dis.decode(data, offset, address)
        if insn is None or insn.text is None:
            return insn
        if kind == "length" and insn.text.startswith("ld sp,$"):
            return dataclasses.replace(insn, length=insn.length + 1)
        if kind == "target" and insn.targets and not insn.text.startswith("rst"):
            wrong = [(target + 1) & 0xFFFF for target in insn.targets]
            text = insn.text.replace("$%X" % insn.targets[0], "$%X" % wrong[0])
            return dataclasses.replace(insn, targets=wrong, text=text)
        if kind == "mnemonic" and insn.text.startswith("inc "):
            return dataclasses.replace(insn, text="dec " + insn.text[4:])
        return insn
    return decode


def main():
    totals = []
    for fill in (FILL_A, FILL_B):
        positions, with_text, canonical, unexplained, known = sweep(z80_dis.decode, fill)
        if positions != 1792 or unexplained or not known or canonical < 780:
            raise AssertionError("fill %s: %d unexplained noncanonical decodes, first %r"
                                 % (fill.hex(), len(unexplained),
                                    [(d.hex(), t) for d, t in unexplained[:3]]))
        totals.append((with_text, canonical, known))

    with tempfile.TemporaryDirectory(prefix="nmd-z80dis-") as directory:
        program = assemble(FLOW_SOURCE, directory)
    facts = flow_facts(z80_dis.decode, program)
    if facts != FLOW_EXPECTED:
        for got, want in zip(facts, FLOW_EXPECTED):
            if got != want:
                raise AssertionError("flow fixture: got %r, want %r" % (got, want))
        raise AssertionError("flow fixture: %d facts, want %d" % (len(facts), len(FLOW_EXPECTED)))

    for kind in ("length", "target", "mnemonic"):
        _, _, _, unexplained, _ = sweep(planted(kind), FILL_A)
        if not unexplained:
            raise AssertionError("planted %s error passed the assembler sweep" % kind)
    for kind in ("length", "target"):
        try:
            caught = flow_facts(planted(kind), program) != FLOW_EXPECTED
        except AssertionError:
            caught = True
        if not caught:
            raise AssertionError("planted %s error passed the flow control" % kind)

    print("z80_decoder_check: ok (1792 opcode positions; %d with text, %d canonical and %d "
          "explained noncanonical on fill A, %d and %d on fill B; %d flow facts; "
          "3 planted errors caught)"
          % (totals[0][0], totals[0][1], totals[0][2], totals[1][1], totals[1][2], len(facts)))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, asmverify.AssemblerError) as exc:
        sys.exit("z80_decoder_check: " + str(exc))
