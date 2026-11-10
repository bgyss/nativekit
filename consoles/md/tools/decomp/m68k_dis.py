"""MC68000 instruction decoder printing the pinned vasm's Motorola syntax.

`decode` reads one instruction and reports its length, text and control flow.
It decodes the base 68000 only: no 68010+ forms, line-A or line-F. The text is
a proposal: nothing here proves the assembler encodes it back to the same
bytes (reserved extension bits, an immediate's unused high byte and similar
are printed as their canonical form). `asmverify` makes that check, and a
caller must treat an unverified line as raw bytes.
"""
from dataclasses import dataclass, field

# flow kinds
SEQ, COND, JUMP, CALL, RET, HALT = "seq", "cond", "jump", "call", "ret", "halt"

CONDITIONS = ("t", "f", "hi", "ls", "cc", "cs", "ne", "eq",
              "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le")
SIZES = ("b", "w", "l")

# effective-address categories: the modes each admits, as (mode, reg-or-None)
_DN, _AN, _IND, _INC, _DEC, _D16, _IDX = range(7)
_ABSW, _ABSL, _PC16, _PCIDX, _IMM = 7, 8, 9, 10, 11
ALL = set(range(12))
DATA = ALL - {_AN}
ALTERABLE = {_DN, _AN, _IND, _INC, _DEC, _D16, _IDX, _ABSW, _ABSL}
DATA_ALT = ALTERABLE - {_AN}
MEM_ALT = DATA_ALT - {_DN}
CONTROL = {_IND, _D16, _IDX, _ABSW, _ABSL, _PC16, _PCIDX}
CONTROL_ALT = CONTROL - {_PC16, _PCIDX}


@dataclass
class Insn:
    length: int
    text: str | None
    flow: str = SEQ
    targets: list = field(default_factory=list)  # direct transfer targets (CPU addresses)
    indirect: bool = False                       # a transfer whose target is computed
    refs: list = field(default_factory=list)     # absolute/PC-relative operand addresses


class _Reader:
    def __init__(self, data, offset, address):
        self.data = data
        self.start = offset
        self.offset = offset
        self.address = address

    def word(self):
        if self.offset + 2 > len(self.data):
            raise IndexError
        value = self.data[self.offset] << 8 | self.data[self.offset + 1]
        self.offset += 2
        return value

    def here(self):
        return self.address + (self.offset - self.start)


def _signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def _kind(mode, reg):
    if mode < 7:
        return mode
    return 7 + reg if reg <= 4 else None


def _index(ext):
    return "%s%d.%s" % ("a" if ext & 0x8000 else "d", (ext >> 12) & 7,
                        "l" if ext & 0x0800 else "w")


def _ea(reader, mode, reg, size, allowed, insn):
    """The operand's text; None when the mode is not in `allowed`."""
    kind = _kind(mode, reg)
    if kind is None or kind not in allowed:
        return None
    if kind == _DN:
        return "d%d" % reg
    if kind == _AN:
        return "a%d" % reg
    if kind == _IND:
        return "(a%d)" % reg
    if kind == _INC:
        return "(a%d)+" % reg
    if kind == _DEC:
        return "-(a%d)" % reg
    if kind == _D16:
        return "(%d,a%d)" % (_signed(reader.word(), 16), reg)
    if kind == _IDX:
        ext = reader.word()
        return "(%d,a%d,%s)" % (_signed(ext & 0xFF, 8), reg, _index(ext))
    if kind == _ABSW:
        value = _signed(reader.word(), 16) & 0xFFFFFFFF
        insn.refs.append(value & 0xFFFFFF)
        return "($%X).w" % value
    if kind == _ABSL:
        value = reader.word() << 16
        value |= reader.word()
        insn.refs.append(value & 0xFFFFFF)
        return "($%X).l" % value
    if kind == _PC16:
        base = reader.here()
        target = (base + _signed(reader.word(), 16)) & 0xFFFFFFFF
        insn.refs.append(target & 0xFFFFFF)
        return "($%X,pc)" % target
    if kind == _PCIDX:
        base = reader.here()
        ext = reader.word()
        target = (base + _signed(ext & 0xFF, 8)) & 0xFFFFFFFF
        return "($%X,pc,%s)" % (target, _index(ext))
    if size == 2:
        value = reader.word() << 16
        value |= reader.word()
        return "#$%X" % value
    value = reader.word()
    return "#$%X" % (value & 0xFF if size == 0 else value)


def _reglist(mask, reversed_order):
    names = ["d%d" % i for i in range(8)] + ["a%d" % i for i in range(8)]
    if reversed_order:
        names.reverse()
    picked = [names[i] for i in range(16) if mask & (1 << i)]
    if reversed_order:
        picked.reverse()
    return "/".join(picked)


def _transfer_target(mode, reg, insn):
    """A JMP/JSR operand's direct target, or mark the transfer computed."""
    kind = _kind(mode, reg)
    if kind in (_ABSW, _ABSL, _PC16) and insn.refs:
        insn.targets.append(insn.refs[-1])
    else:
        insn.indirect = True


def _group0(reader, op, insn):
    mode, reg = (op >> 3) & 7, op & 7
    if op & 0x0100:
        if mode == 1:  # MOVEP
            size = "l" if op & 0x0040 else "w"
            memory = "(%d,a%d)" % (_signed(reader.word(), 16), reg)
            data = "d%d" % ((op >> 9) & 7)
            return "movep.%s %s" % (size, (data + "," + memory) if op & 0x0080
                                    else (memory + "," + data))
        which = (op >> 6) & 3
        allowed = DATA if which == 0 else DATA_ALT
        operand = _ea(reader, mode, reg, 0, allowed, insn)
        if operand is None:
            return None
        return "%s d%d,%s" % (("btst", "bchg", "bclr", "bset")[which], (op >> 9) & 7, operand)
    kind = (op >> 9) & 7
    size = (op >> 6) & 3
    if kind == 4:  # static bit number
        bit = reader.word() & 0xFF
        allowed = (DATA - {_IMM}) if size == 0 else DATA_ALT
        operand = _ea(reader, mode, reg, 0, allowed, insn)
        if operand is None:
            return None
        return "%s #%d,%s" % (("btst", "bchg", "bclr", "bset")[size], bit, operand)
    if kind == 7 or size == 3:
        return None
    name = ("ori", "andi", "subi", "addi", None, "eori", "cmpi")[kind]
    if (op & 0x3F) == 0x3C:  # to CCR / SR
        if kind not in (0, 1, 5) or size == 2:
            return None
        value = reader.word()
        return "%s #$%X,%s" % (name, value & 0xFF if size == 0 else value,
                               "ccr" if size == 0 else "sr")
    immediate = _ea(reader, 7, 4, size, {_IMM}, insn)
    operand = _ea(reader, mode, reg, size, DATA_ALT, insn)
    if operand is None:
        return None
    return "%s.%s %s,%s" % (name, SIZES[size], immediate, operand)


def _move(reader, op, insn):
    size = {1: 0, 3: 1, 2: 2}[op >> 12]
    source = _ea(reader, (op >> 3) & 7, op & 7, size, ALL if size else DATA, insn)
    if source is None:
        return None
    dest_mode, dest_reg = (op >> 6) & 7, (op >> 9) & 7
    if dest_mode == 1:
        if size == 0:
            return None
        return "movea.%s %s,a%d" % (SIZES[size], source, dest_reg)
    dest = _ea(reader, dest_mode, dest_reg, size, DATA_ALT, insn)
    if dest is None:
        return None
    return "move.%s %s,%s" % (SIZES[size], source, dest)


def _group4(reader, op, insn):
    mode, reg = (op >> 3) & 7, op & 7
    if op == 0x4AFC:
        insn.flow = HALT
        return "illegal"
    if (op & 0xFFF0) == 0x4E40:
        return "trap #%d" % (op & 15)
    if (op & 0xFFF8) == 0x4E50:
        return "link a%d,#%d" % (reg, _signed(reader.word(), 16))
    if (op & 0xFFF8) == 0x4E58:
        return "unlk a%d" % reg
    if (op & 0xFFF8) == 0x4E60:
        return "move.l a%d,usp" % reg
    if (op & 0xFFF8) == 0x4E68:
        return "move.l usp,a%d" % reg
    if (op & 0xFFF8) == 0x4E70:
        if reg == 2:
            return "stop #$%X" % reader.word()
        if reg in (3, 5, 7):
            insn.flow = RET
        return ("reset", "nop", None, "rte", None, "rts", "trapv", "rtr")[reg]
    if (op & 0xFF80) == 0x4E80:
        operand = _ea(reader, mode, reg, 2, CONTROL, insn)
        if operand is None:
            return None
        insn.flow = JUMP if op & 0x0040 else CALL
        _transfer_target(mode, reg, insn)
        return "%s %s" % ("jmp" if op & 0x0040 else "jsr", operand)
    if (op & 0x01C0) == 0x01C0:
        operand = _ea(reader, mode, reg, 2, CONTROL, insn)
        return None if operand is None else "lea %s,a%d" % (operand, (op >> 9) & 7)
    if (op & 0x01C0) == 0x0180:
        operand = _ea(reader, mode, reg, 1, DATA, insn)
        return None if operand is None else "chk.w %s,d%d" % (operand, (op >> 9) & 7)
    if op & 0x0100:
        return None
    size = (op >> 6) & 3
    kind = (op >> 9) & 7
    if kind in (0, 1, 2, 3):
        if size == 3:
            if kind == 1:
                return None
            if kind == 0:
                operand = _ea(reader, mode, reg, 1, DATA_ALT, insn)
                return None if operand is None else "move.w sr,%s" % operand
            operand = _ea(reader, mode, reg, 1, DATA, insn)
            return None if operand is None else "move.w %s,%s" % (
                operand, "ccr" if kind == 2 else "sr")
        operand = _ea(reader, mode, reg, size, DATA_ALT, insn)
        return None if operand is None else "%s.%s %s" % (
            ("negx", "clr", "neg", "not")[kind], SIZES[size], operand)
    if kind == 4:
        if size == 0:
            operand = _ea(reader, mode, reg, 0, DATA_ALT, insn)
            return None if operand is None else "nbcd.b %s" % operand
        if size == 1:
            if mode == 0:
                return "swap d%d" % reg
            operand = _ea(reader, mode, reg, 2, CONTROL, insn)
            return None if operand is None else "pea %s" % operand
        if mode == 0:
            return "ext.%s d%d" % ("w" if size == 2 else "l", reg)
        mask = reader.word()
        operand = _ea(reader, mode, reg, size - 1, CONTROL_ALT | {_DEC}, insn)
        if operand is None:
            return None
        if not mask:
            return ""  # decoded, but an empty register list has no assembler form
        return "movem.%s %s,%s" % ("w" if size == 2 else "l", _reglist(mask, mode == 4), operand)
    if kind == 5:
        if size == 3:
            operand = _ea(reader, mode, reg, 0, DATA_ALT, insn)
            return None if operand is None else "tas.b %s" % operand
        operand = _ea(reader, mode, reg, size, DATA_ALT, insn)
        return None if operand is None else "tst.%s %s" % (SIZES[size], operand)
    if kind == 6:
        if size < 2:
            return None
        mask = reader.word()
        operand = _ea(reader, mode, reg, size - 1, CONTROL | {_INC}, insn)
        if operand is None:
            return None
        if not mask:
            return ""
        return "movem.%s %s,%s" % ("w" if size == 2 else "l", operand, _reglist(mask, False))
    return None


def _group5(reader, op, insn):
    mode, reg = (op >> 3) & 7, op & 7
    size = (op >> 6) & 3
    if size == 3:
        condition = CONDITIONS[(op >> 8) & 15]
        if mode == 1:
            base = reader.here()
            target = (base + _signed(reader.word(), 16)) & 0xFFFFFF
            insn.flow = COND
            insn.targets.append(target)
            return "db%s d%d,$%X" % (condition, reg, target)
        operand = _ea(reader, mode, reg, 0, DATA_ALT, insn)
        return None if operand is None else "s%s.b %s" % (condition, operand)
    operand = _ea(reader, mode, reg, size, ALTERABLE if size else DATA_ALT, insn)
    if operand is None:
        return None
    return "%s.%s #%d,%s" % ("subq" if op & 0x0100 else "addq", SIZES[size],
                             ((op >> 9) & 7) or 8, operand)


def _group6(reader, op, insn):
    condition = (op >> 8) & 15
    base = reader.here()
    displacement = op & 0xFF
    if displacement == 0xFF:
        return None  # a 68020 long branch
    if displacement == 0:
        target = base + _signed(reader.word(), 16)
        width = "w"
    else:
        target = base + _signed(displacement, 8)
        width = "s"
    target &= 0xFFFFFF
    insn.targets.append(target)
    insn.flow = JUMP if condition == 0 else CALL if condition == 1 else COND
    name = "bra" if condition == 0 else "bsr" if condition == 1 else "b" + CONDITIONS[condition]
    return "%s.%s $%X" % (name, width, target)


def _arithmetic(reader, op, insn, name):
    """OR/AND/ADD/SUB and their BCD, extended and address forms."""
    mode, reg = (op >> 3) & 7, op & 7
    data = (op >> 9) & 7
    opmode = (op >> 6) & 7
    adds = name in ("add", "sub")
    if opmode in (3, 7):
        if adds:
            size = 1 if opmode == 3 else 2
            operand = _ea(reader, mode, reg, size, ALL, insn)
            return None if operand is None else "%sa.%s %s,a%d" % (name, SIZES[size], operand, data)
        operand = _ea(reader, mode, reg, 1, DATA, insn)
        if operand is None:
            return None
        if name == "or":
            return "%s.w %s,d%d" % ("divu" if opmode == 3 else "divs", operand, data)
        return "%s.w %s,d%d" % ("mulu" if opmode == 3 else "muls", operand, data)
    size = opmode & 3
    if opmode >= 4 and mode in (0, 1):
        if adds:
            pair = ("-(a%d),-(a%d)" if mode else "d%d,d%d") % (reg, data)
            return "%sx.%s %s" % (name, SIZES[size], pair)
        if opmode == 4:
            pair = ("-(a%d),-(a%d)" if mode else "d%d,d%d") % (reg, data)
            return "%s.b %s" % ("sbcd" if name == "or" else "abcd", pair)
        if name == "and":
            if opmode == 5:
                return "exg %s%d,%s%d" % (("a", data, "a", reg) if mode else ("d", data, "d", reg))
            if opmode == 6 and mode == 1:
                return "exg d%d,a%d" % (data, reg)
        return None
    if opmode >= 4:
        operand = _ea(reader, mode, reg, size, MEM_ALT, insn)
        return None if operand is None else "%s.%s d%d,%s" % (name, SIZES[size], data, operand)
    allowed = ALL if adds and size else DATA
    operand = _ea(reader, mode, reg, size, allowed, insn)
    return None if operand is None else "%s.%s %s,d%d" % (name, SIZES[size], operand, data)


def _compare(reader, op, insn):
    mode, reg = (op >> 3) & 7, op & 7
    data = (op >> 9) & 7
    opmode = (op >> 6) & 7
    if opmode in (3, 7):
        size = 1 if opmode == 3 else 2
        operand = _ea(reader, mode, reg, size, ALL, insn)
        return None if operand is None else "cmpa.%s %s,a%d" % (SIZES[size], operand, data)
    size = opmode & 3
    if opmode < 4:
        operand = _ea(reader, mode, reg, size, ALL if size else DATA, insn)
        return None if operand is None else "cmp.%s %s,d%d" % (SIZES[size], operand, data)
    if mode == 1:
        return "cmpm.%s (a%d)+,(a%d)+" % (SIZES[size], reg, data)
    operand = _ea(reader, mode, reg, size, DATA_ALT, insn)
    return None if operand is None else "eor.%s d%d,%s" % (SIZES[size], data, operand)


def _shift(reader, op, insn):
    mode, reg = (op >> 3) & 7, op & 7
    direction = "l" if op & 0x0100 else "r"
    size = (op >> 6) & 3
    names = ("as", "ls", "rox", "ro")
    if size == 3:
        if op & 0x0800:
            return None
        operand = _ea(reader, mode, reg, 1, MEM_ALT, insn)
        return None if operand is None else "%s%s.w %s" % (names[(op >> 9) & 3], direction, operand)
    count = (op >> 9) & 7
    source = "d%d" % count if op & 0x0020 else "#%d" % (count or 8)
    return "%s%s.%s %s,d%d" % (names[(op >> 3) & 3], direction, SIZES[size], source, reg)


def decode(data, offset, address):
    """Decode one instruction of `data` at `offset`, which the CPU sees at
    `address`. None when it is not a 68000 instruction or runs off the data."""
    if address & 1 or offset < 0:
        return None
    reader = _Reader(data, offset, address)
    insn = Insn(0, None)
    try:
        op = reader.word()
        group = op >> 12
        if group == 0:
            text = _group0(reader, op, insn)
        elif group in (1, 2, 3):
            text = _move(reader, op, insn)
        elif group == 4:
            text = _group4(reader, op, insn)
        elif group == 5:
            text = _group5(reader, op, insn)
        elif group == 6:
            text = _group6(reader, op, insn)
        elif group == 7:
            text = None if op & 0x0100 else "moveq #%d,d%d" % (_signed(op & 0xFF, 8), (op >> 9) & 7)
        elif group == 8:
            text = _arithmetic(reader, op, insn, "or")
        elif group == 9:
            text = _arithmetic(reader, op, insn, "sub")
        elif group == 0xB:
            text = _compare(reader, op, insn)
        elif group == 0xC:
            text = _arithmetic(reader, op, insn, "and")
        elif group == 0xD:
            text = _arithmetic(reader, op, insn, "add")
        elif group == 0xE:
            text = _shift(reader, op, insn)
        else:
            text = None
    except IndexError:
        return None
    if text is None:
        return None
    insn.length = reader.offset - offset
    insn.text = text or None
    return insn
