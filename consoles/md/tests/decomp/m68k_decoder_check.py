#!/usr/bin/env python3
"""68000 decoder controls: every first opcode word against the pinned vasm,
flow facts on an authored program, and planted decoder errors.

Scope of the claim: the 65,536 first words, each with two fixed sets of
extension words. Set A's extension words are canonical, so every decode must
reassemble to its bytes. Set B has a byte immediate with a non-zero high byte
and a brief extension word with reserved bits set: those decodes must be
reported noncanonical (they stay raw bytes in a skeleton), and nothing else
may be. Whether a word the decoder calls illegal is truly illegal on hardware
is not checked here.
"""
import dataclasses
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))

import asmverify  # noqa: E402
import m68k_dis  # noqa: E402

EXT_A = bytes.fromhex("0012705600781234567800")
EXT_B = bytes.fromhex("F8FE345600781234567800")


def sweep(decode, extension):
    """(decoded, canonical, [(op, text)] noncanonical) over every first word."""
    items = []
    ops = []
    for op in range(0x10000):
        data = op.to_bytes(2, "big") + extension
        address = 0x10000 + op * 16
        insn = decode(data, 0, address)
        if insn is None or insn.text is None:
            continue
        items.append((address, data[:insn.length], insn.text))
        ops.append(op)
    verdicts = asmverify.verify("m68k", items)
    bad = [(op, item[2]) for op, item, good in zip(ops, items, verdicts) if not good]
    return len(items), sum(verdicts), bad


def explained_noncanonical(op, text):
    """Set B's two planted non-canonical operand encodings."""
    byte_immediate = (op & 0xF000) == 0 and (
        ".b #$" in text or text.split()[0] in ("btst", "bchg", "bclr", "bset") or "ccr" in text)
    move_to_ccr = text.startswith("move.w #") and text.endswith(",ccr")
    reserved_index = ",d3.w)" in text  # $3456: index d3.w with reserved bit 10 set
    byte_source = ".b #$FE," in text
    return byte_immediate or move_to_ccr or reserved_index or byte_source


FLOW_SOURCE = """
        org     $200
start:  bsr.w   sub
        beq.s   skip
        jmp     (a0)
        nop
skip:   jsr     (table,pc,d0.w)
        dbf     d1,skip
        jmp     ($400).w
        lea     (table,pc),a1
        rte
        move.l  #$12345678,($FF0000).l
sub:    movem.l d0-d2/a0,-(sp)
        jsr     ($123456).l
        rts
table:  dc.w    0
"""
# (address, length, flow, targets, indirect) in address order
FLOW_EXPECTED = [
    (0x200, 4, "call", [0x226], False),
    (0x204, 2, "cond", [0x20A], False),
    (0x206, 2, "jump", [], True),
    (0x208, 2, "seq", [], False),
    (0x20A, 4, "call", [], True),
    (0x20E, 4, "cond", [0x20A], False),
    (0x212, 4, "jump", [0x400], False),
    (0x216, 4, "seq", [], False),
    (0x21A, 2, "ret", [], False),
    (0x21C, 10, "seq", [], False),
    (0x226, 4, "seq", [], False),
    (0x22A, 6, "call", [0x123456], False),
    (0x230, 2, "ret", [], False),
]


def assemble(source, directory):
    import subprocess
    path = pathlib.Path(directory)
    (path / "flow.s").write_text(source)
    result = subprocess.run(
        [str(asmverify.assembler_path("m68k")), "-Fbin", "-m68000", "-no-opt", "-quiet",
         "-o", "flow.bin", "flow.s"], cwd=path, stdin=subprocess.DEVNULL,
        capture_output=True, text=True)
    if result.returncode:
        raise AssertionError("flow fixture did not assemble: " + result.stderr)
    return (path / "flow.bin").read_bytes()


def flow_facts(decode, program):
    facts = []
    offset = 0
    while offset < len(program) - 2:
        insn = decode(program, offset, 0x200 + offset)
        if insn is None:
            raise AssertionError("flow fixture: no decode at $%X" % (0x200 + offset))
        facts.append((0x200 + offset, insn.length, insn.flow, list(insn.targets), insn.indirect))
        offset += insn.length
    return facts


def planted(kind):
    """A decoder with one error planted."""
    def decode(data, offset, address):
        insn = m68k_dis.decode(data, offset, address)
        if insn is None or insn.text is None:
            return insn
        if kind == "length" and insn.text.startswith("lea "):
            return dataclasses.replace(insn, length=insn.length + 2)
        if kind == "target" and insn.targets:
            wrong = [target + 2 for target in insn.targets]
            text = insn.text.replace("$%X" % insn.targets[0], "$%X" % wrong[0])
            return dataclasses.replace(insn, targets=wrong, text=text)
        if kind == "mnemonic" and insn.text.startswith("add."):
            return dataclasses.replace(insn, text="sub." + insn.text[4:])
        return insn
    return decode


def main():
    import tempfile
    decoded, canonical, bad = sweep(m68k_dis.decode, EXT_A)
    if decoded < 40000 or bad:
        raise AssertionError("set A: %d of %d decodes do not reassemble, first %r"
                             % (len(bad), decoded, bad[:3]))
    decoded_b, canonical_b, bad_b = sweep(m68k_dis.decode, EXT_B)
    unexplained = [entry for entry in bad_b if not explained_noncanonical(*entry)]
    if decoded_b != decoded or not bad_b or unexplained:
        raise AssertionError("set B: %d unexplained noncanonical decodes, first %r"
                             % (len(unexplained), unexplained[:3]))

    with tempfile.TemporaryDirectory(prefix="nmd-m68kdis-") as directory:
        program = assemble(FLOW_SOURCE, directory)
    facts = flow_facts(m68k_dis.decode, program)
    if facts != FLOW_EXPECTED:
        for got, want in zip(facts, FLOW_EXPECTED):
            if got != want:
                raise AssertionError("flow fixture: got %r, want %r" % (got, want))
        raise AssertionError("flow fixture: %d facts, want %d" % (len(facts), len(FLOW_EXPECTED)))

    for kind in ("length", "target", "mnemonic"):
        _, _, wrong = sweep(planted(kind), EXT_A)
        if not wrong:
            raise AssertionError("planted %s error passed the assembler sweep" % kind)
    for kind in ("length", "target"):
        try:
            caught = flow_facts(planted(kind), program) != FLOW_EXPECTED
        except AssertionError:
            caught = True
        if not caught:
            raise AssertionError("planted %s error passed the flow control" % kind)

    print("m68k_decoder_check: ok (%d of 65536 first words decode; set A %d canonical; "
          "set B %d canonical, %d explained noncanonical; %d flow facts; 3 planted errors caught)"
          % (decoded, canonical, canonical_b, len(bad_b), len(facts)))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, asmverify.AssemblerError) as exc:
        sys.exit("m68k_decoder_check: " + str(exc))
