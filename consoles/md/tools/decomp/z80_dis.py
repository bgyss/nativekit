"""Z80 instruction decoder printing the pinned vasm's oldstyle syntax.

Covers the unprefixed, CB, ED, DD, FD, DDCB and FDCB tables, including the
undocumented forms the assembler has a spelling for (ixh/ixl/iyh/iyl, sll,
in f,(c), out (c),0). Forms with no spelling (the DDCB/FDCB register copies,
undefined ED opcodes, a prefix that prefixes nothing) are decoded with their
length and `text` None. As with the 68000 decoder the text is a proposal that
`asmverify` must hold to the bytes: a redundant prefix or a duplicate ED
encoding prints its canonical form and does not reassemble to itself.
"""
from m68k_dis import CALL, COND, JUMP, RET, SEQ, Insn

R = ("b", "c", "d", "e", "h", "l", "(hl)", "a")
RP = ("bc", "de", "hl", "sp")
RP2 = ("bc", "de", "hl", "af")
CC = ("nz", "z", "nc", "c", "po", "pe", "p", "m")
ALU = ("add a,", "adc a,", "sub ", "sbc a,", "and ", "xor ", "or ", "cp ")
ROT = ("rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl")
BLOCK = (("ldi", "cpi", "ini", "outi"), ("ldd", "cpd", "ind", "outd"),
         ("ldir", "cpir", "inir", "otir"), ("lddr", "cpdr", "indr", "otdr"))


class _Reader:
    def __init__(self, data, offset, address):
        self.data = data
        self.start = offset
        self.offset = offset
        self.address = address

    def byte(self):
        if self.offset >= len(self.data):
            raise IndexError
        value = self.data[self.offset]
        self.offset += 1
        return value

    def word(self):
        low = self.byte()
        return low | self.byte() << 8

    def relative(self):
        displacement = self.byte()
        if displacement & 0x80:
            displacement -= 0x100
        return (self.address + (self.offset - self.start) + displacement) & 0xFFFF


def _indexed(index, displacement):
    if displacement & 0x80:
        return "(%s-%d)" % (index, 0x100 - displacement)
    return "(%s+%d)" % (index, displacement)


def _main(reader, op, insn, index):
    """An unprefixed-table opcode; `index` is ix/iy under a DD/FD prefix."""
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    hl = index or "hl"
    memory = None

    def reg(number, with_memory):
        nonlocal memory
        if number == 6:
            if index and memory is None:
                memory = _indexed(index, reader.byte())
            return memory if index else "(hl)"
        if index and not with_memory and number in (4, 5):
            return index + ("h" if number == 4 else "l")
        return R[number]

    def pair(table, number):
        return hl if table[number] == "hl" else table[number]

    if x == 0:
        if z == 0:
            if y == 0:
                return "nop"
            if y == 1:
                return "ex af,af'"
            target = reader.relative()
            insn.targets.append(target)
            insn.flow = JUMP if y == 3 else COND
            if y == 2:
                return "djnz $%X" % target
            return "jr $%X" % target if y == 3 else "jr %s,$%X" % (CC[y - 4], target)
        if z == 1:
            if q == 0:
                return "ld %s,$%X" % (pair(RP, p), reader.word())
            return "add %s,%s" % (hl, pair(RP, p))
        if z == 2:
            if p < 2:
                where = "(bc)" if p == 0 else "(de)"
                return "ld a,%s" % where if q else "ld %s,a" % where
            address = reader.word()
            insn.refs.append(address)
            register = hl if p == 2 else "a"
            return ("ld %s,($%X)" % (register, address) if q
                    else "ld ($%X),%s" % (address, register))
        if z == 3:
            return "%s %s" % ("dec" if q else "inc", pair(RP, p))
        if z in (4, 5):
            return "%s %s" % ("inc" if z == 4 else "dec", reg(y, y == 6))
        if z == 6:
            target = reg(y, y == 6)
            return "ld %s,$%X" % (target, reader.byte())
        return ("rlca", "rrca", "rla", "rra", "daa", "cpl", "scf", "ccf")[y]
    if x == 1:
        if y == 6 and z == 6:
            return "halt"
        uses_memory = y == 6 or z == 6
        return "ld %s,%s" % (reg(y, uses_memory), reg(z, uses_memory))
    if x == 2:
        return ALU[y] + reg(z, z == 6)
    if z == 0:
        return "ret " + CC[y]
    if z == 1:
        if q == 0:
            return "pop " + pair(RP2, p)
        if p == 0:
            insn.flow = RET
            return "ret"
        if p == 1:
            return "exx"
        if p == 2:
            insn.flow = JUMP
            insn.indirect = True
            return "jp (%s)" % hl
        return "ld sp," + hl
    if z == 2:
        target = reader.word()
        insn.flow = COND
        insn.targets.append(target)
        return "jp %s,$%X" % (CC[y], target)
    if z == 3:
        if y == 0:
            target = reader.word()
            insn.flow = JUMP
            insn.targets.append(target)
            return "jp $%X" % target
        if y == 2:
            return "out ($%X),a" % reader.byte()
        if y == 3:
            return "in a,($%X)" % reader.byte()
        if y == 4:
            return "ex (sp)," + hl
        if y == 5:
            return "ex de,hl"
        return "di" if y == 6 else "ei"
    if z == 4:
        target = reader.word()
        insn.flow = CALL
        insn.targets.append(target)
        return "call %s,$%X" % (CC[y], target)
    if z == 5:
        if q == 0:
            return "push " + pair(RP2, p)
        target = reader.word()
        insn.flow = CALL
        insn.targets.append(target)
        return "call $%X" % target
    if z == 6:
        return ALU[y] + "$%X" % reader.byte()
    insn.flow = CALL
    insn.targets.append(y * 8)
    return "rst $%X" % (y * 8)


def _bits(op, operand):
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    if x == 0:
        return "%s %s" % (ROT[y], operand)
    return "%s %d,%s" % (("bit", "res", "set")[x - 1], y, operand)


def _extended(reader, op, insn):
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    if x == 1:
        if z == 0:
            return "in %s,(c)" % ("f" if y == 6 else R[y])
        if z == 1:
            return "out (c),%s" % ("0" if y == 6 else R[y])
        if z == 2:
            return "%s hl,%s" % ("adc" if q else "sbc", RP[p])
        if z == 3:
            address = reader.word()
            insn.refs.append(address)
            return ("ld %s,($%X)" % (RP[p], address) if q
                    else "ld ($%X),%s" % (address, RP[p]))
        if z == 4:
            return "neg"
        if z == 5:
            insn.flow = RET
            return "reti" if y == 1 else "retn"
        if z == 6:
            return "im %d" % (0, 0, 1, 2, 0, 0, 1, 2)[y]
        return ("ld i,a", "ld r,a", "ld a,i", "ld a,r", "rrd", "rld", "", "")[y]
    if x == 2 and z <= 3 and y >= 4:
        return BLOCK[y - 4][z]
    return ""  # undefined: runs as a two-byte no-operation


def decode(data, offset, address):
    """Decode one instruction of `data` at `offset`, which the Z80 sees at
    `address`. None when the bytes run off the data."""
    if offset < 0:
        return None
    reader = _Reader(data, offset, address & 0xFFFF)
    insn = Insn(0, None)
    try:
        op = reader.byte()
        if op == 0xCB:
            op = reader.byte()
            text = _bits(op, R[op & 7])
        elif op == 0xED:
            text = _extended(reader, reader.byte(), insn)
        elif op in (0xDD, 0xFD):
            index = "ix" if op == 0xDD else "iy"
            if reader.offset >= len(data):
                raise IndexError
            following = data[reader.offset]
            if following in (0xDD, 0xED, 0xFD):
                text = ""  # the prefix alone: it prefixes nothing
            elif following == 0xCB:
                reader.byte()
                operand = _indexed(index, reader.byte())
                op = reader.byte()
                if (op & 7) != 6 and (op >> 6) != 1:
                    text = ""  # the undocumented copy into a register
                else:
                    text = _bits(op, operand)
            else:
                text = _main(reader, reader.byte(), insn, index)
        else:
            text = _main(reader, op, insn, None)
    except IndexError:
        return None
    insn.length = reader.offset - offset
    insn.text = text or None
    return insn
