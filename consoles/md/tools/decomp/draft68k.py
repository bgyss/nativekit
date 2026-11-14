#!/usr/bin/env python3
"""Draft C for 68000 routines from their decoded instructions (D06/D07, T2).

    draft68k.py --rom ROM --codemap codemap.json --out drafts.c ADDRESS...

Each routine becomes a `void draft_XXXXXX(void)` over nmd_cpu and the bus
calls, written with nmd_decomp.h's helpers: one statement group per original
instruction, its address as a label where a branch lands. It is machine-shaped
on purpose: a faithful starting point that the per-call checker
(`check_calls.py`) and lockstep (`lockstep.py`) hold against the original,
and that a person then rewrites as plain typed C (D09). Passing those checks
is the only evidence a draft has; generating it proves nothing.

A routine's body is what its entry reaches by fall-through and branches. A
branch or jump to another routine's entry is a tail call; BSR/JSR call the
other routine's draft or C. TRAP calls `trap_call(number, return_pc)`, which
the game supplies (the exception frame pushed, the vector's C run, the frame
popped). Computed jumps inside the routine go through the
code map's jump tables; others call by address. An instruction this module
does not translate (TRAP, the BCD and X-extended arithmetic, STOP, RESET,
CHK, MOVEP) makes the routine a refusal: no C is written for it
and the reason is listed.

Bus order follows the 68000's: a long is two word transfers, high word
first; a read-modify-write reads, then writes; CLR does not read (as the
pinned core).
"""
import argparse
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import m68k_dis  # noqa: E402

CONDITIONS = {"t": 0, "f": 1, "hi": 2, "ls": 3, "cc": 4, "hs": 4, "cs": 5, "lo": 5, "ne": 6,
              "eq": 7, "vc": 8, "vs": 9, "pl": 10, "mi": 11, "ge": 12, "lt": 13, "gt": 14,
              "le": 15, "ra": 0}
BITS = {"b": 8, "w": 16, "l": 32}
TYPES = {8: "uint8_t", 16: "uint16_t", 32: "uint32_t"}
MASKS = {8: "0xFFu", 16: "0xFFFFu", 32: "0xFFFFFFFFu"}


class Refusal(Exception):
    """An instruction this module does not translate."""


def number(text):
    text = text.strip()
    negative = text.startswith("-")
    text = text.lstrip("-")
    value = int(text[1:], 16) if text.startswith("$") else int(text)
    return -value if negative else value


def split_operands(text):
    parts, depth, current = [], 0, ""
    for char in text:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        if char == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += char
    if current:
        parts.append(current)
    return [part.strip() for part in parts]


def reg_expr(name):
    name = name.lower()
    if name in ("sp", "a7"):
        return "A[7]"
    if re.fullmatch(r"[da][0-7]", name):
        return "%s[%s]" % (name[0].upper(), name[1])
    raise Refusal("register " + name)


class Operand:
    """One effective address: how to compute its address and read or write it."""

    def __init__(self, text, size):
        self.text, self.size = text, size
        self.kind, self.reg, self.address = None, None, None
        t = text.lower()
        if re.fullmatch(r"d[0-7]", t):
            self.kind, self.reg = "dreg", reg_expr(t)
        elif re.fullmatch(r"a[0-7]|sp", t):
            self.kind, self.reg = "areg", reg_expr(t)
        elif t.startswith("#"):
            self.kind, self.value = "imm", number(t[1:])
        elif t in ("sr", "ccr", "usp"):
            self.kind = t
        elif re.fullmatch(r"\((a[0-7]|sp)\)", t):
            self.kind, self.reg = "ind", reg_expr(t[1:-1])
        elif re.fullmatch(r"\((a[0-7]|sp)\)\+", t):
            self.kind, self.reg = "post", reg_expr(t[1:-2])
        elif re.fullmatch(r"-\((a[0-7]|sp)\)", t):
            self.kind, self.reg = "pre", reg_expr(t[2:-1])
        elif re.fullmatch(r"\(\$?[0-9a-f]+\)\.[wl]", t):
            value = number(t[1:t.index(")")])
            if t.endswith(".w"):
                value = value - 0x10000 if value & 0x8000 else value
            self.kind, self.address = "abs", "0x%06Xu" % (value & 0xFFFFFF)
        else:
            m = re.fullmatch(r"\((-?\$?[0-9a-f]+),(a[0-7]|sp|pc)(?:,([da][0-7])\.([wl]))?\)", t)
            if not m:
                raise Refusal("operand " + text)
            displacement, base, index, index_size = m.groups()
            value = number(displacement)
            if base == "pc":
                base_expr = "0x%06Xu" % (value & 0xFFFFFF)     # the decoder gives the target
            else:
                base_expr = "%s + (uint32_t)%d" % (reg_expr(base), value)
            if index:
                index_expr = reg_expr(index)
                if index_size == "w":
                    index_expr = "SX16(%s)" % index_expr
                base_expr = "%s + %s" % (base_expr, index_expr)
            self.kind, self.address = "mem", base_expr

    def step(self):
        if self.size == 8 and self.reg == "A[7]":
            return 2
        return self.size // 8

    def compute(self, out, name="ea"):
        """Emit the address into `name` (memory operands) and any pre-decrement."""
        if self.kind == "pre":
            out.append("%s -= %d;" % (self.reg, self.step()))
            out.append("%s = %s;" % (name, self.reg))
        elif self.kind in ("ind", "post"):
            out.append("%s = %s;" % (name, self.reg))
        elif self.kind in ("abs", "mem"):
            out.append("%s = %s;" % (name, self.address))
        self.name = name

    def finish(self, out):
        if self.kind == "post":
            out.append("%s += %d;" % (self.reg, self.step()))

    def is_memory(self):
        return self.kind in ("ind", "post", "pre", "abs", "mem")

    def read(self):
        """An expression of the operand's value at its size."""
        cast = "(%s)" % TYPES[self.size]
        if self.kind == "dreg" or self.kind == "areg":
            return "%s%s" % (cast, self.reg)
        if self.kind == "imm":
            return "%s0x%Xu" % (cast, self.value & int(MASKS[self.size][:-1], 16))
        if self.is_memory():
            return "rd%d(%s)" % (self.size, self.name)
        raise Refusal("read of " + self.text)

    def write(self, value):
        if self.kind == "dreg":
            if self.size == 32:
                return "%s = %s;" % (self.reg, value)
            return "nmd_set%d(&%s, %s);" % (self.size, self.reg, value)
        if self.kind == "areg":
            return "%s = %s;" % (self.reg, value if self.size == 32 else "SX16(%s)" % value)
        if self.is_memory():
            return "wr%d(%s, %s);" % (self.size, self.name, value)
        raise Refusal("write to " + self.text)


def reglist(text):
    names = []
    for part in text.lower().split("/"):
        if "-" in part:
            first, last = part.split("-")
            names += ["%s%d" % (first[0], n) for n in range(int(first[1]), int(last[1]) + 1)]
        else:
            names.append(part)
    order = ["d%d" % n for n in range(8)] + ["a%d" % n for n in range(8)]
    return [n for n in order if n in {x.replace("sp", "a7") for x in names}]


class Drafter:
    def __init__(self, rom, codemap):
        self.rom = rom
        space = codemap["spaces"][0]
        self.instructions = {i["at"]: i for i in space["instructions"]}
        self.entries = {e["at"] for e in space["entries"]}
        self.tables = {}
        for table in space.get("tables", ()):
            if table["status"] == "accepted":
                self.tables.setdefault(table["site"], set()).update(table["targets"])

    def body(self, entry):
        seen, work = set(), [entry]
        while work:
            address = work.pop()
            if address in seen or address not in self.instructions:
                continue
            if address != entry and address in self.entries and address not in seen and \
                    self._tail(address, entry):
                continue
            seen.add(address)
            record = self.instructions[address]
            if record["flow"] in ("seq", "cond", "call"):
                work.append(address + record["length"])
            if record["flow"] in ("jump", "cond"):
                work.extend(t for t in record["targets"] if not self._tail(t, entry))
            if record["indirect"] and record["flow"] == "jump":
                work.extend(t for t in self.tables.get(address, ()) if not self._tail(t, entry))
        return sorted(seen)

    def _tail(self, target, entry):
        """A transfer to another routine's entry is a tail call, not this body."""
        return target != entry and target in self.entries

    def draft(self, entry, names):
        """C for the routine at `entry`; `names` maps an address to its C function."""
        body = self.body(entry)
        if not body:
            raise Refusal("no decoded instruction at $%06X" % entry)
        inside = set(body)
        targets = set()
        lines = []
        prologue = []
        if body[0] != entry:            # code below the entry belongs to it: start at the entry
            targets.add(entry)
            prologue.append("    goto L_%06X;" % entry)
        for address in body:
            record = self.instructions[address]
            insn = m68k_dis.decode(self.rom, address, address)
            out = []
            self._translate(address, insn, record, out, inside, targets, names, entry)
            lines.append((address, record["text"], out))
            # a body is a set of addresses: an address after it that is not in it is a jump
            following = address + record["length"]
            if record["flow"] in ("seq", "cond", "call") and following not in inside:
                if following in names or self._tail(following, entry):
                    out.append(self._tail_call(following, names))
                else:
                    raise Refusal("falls out of its body at $%06X" % following)
        code = list(prologue)
        for address, text, out in lines:
            if address in targets:
                code.append("L_%06X:" % address)
            code.append("    /* $%06X  %s */" % (address, text))
            code.extend("    " + line for line in out)
        return "\n".join(code)

    def _tail_call(self, target, names):
        if target in names:
            return "%s(); return;   /* continues into $%06X: its rts is this routine's */" % (
                names[target], target)
        return "jmp_address(0x%06Xu); return;" % target

    def _goto(self, target, inside, targets, names, entry):
        if target in inside:
            targets.add(target)
            return "goto L_%06X;" % target
        return self._tail_call(target, names)

    def _translate(self, address, insn, record, out, inside, targets, names, entry):
        text = record["text"]
        mnemonic, _, operand_text = text.partition(" ")
        base, _, suffix = mnemonic.partition(".")
        size = BITS.get(suffix, 16)
        ops = split_operands(operand_text) if operand_text else []
        following = address + record["length"]

        def operand(index, op_size=None):
            return Operand(ops[index], op_size or size)

        # control flow
        if base == "rts":
            out.append("nmd_rts(); return;")
            return
        if base == "rte":
            out.append("return;     /* rte: the native dispatcher pops SR and PC */")
            return
        if base == "nop":
            return
        if base in ("bsr", "jsr") and not record["indirect"]:
            target = record["targets"][0]
            call = names.get(target)
            out.append("NMD_JSR(0x%06Xu, %s);" % (following, call) if call else
                       "jsr_address(0x%06Xu, 0x%06Xu);" % (following, target))
            return
        if base == "jsr":
            source = operand(0)
            source.compute(out, "ea")
            out.append("jsr_address(0x%06Xu, ea);" % following)
            return
        if base in ("bra", "jmp") and not record["indirect"]:
            out.append(self._goto(record["targets"][0], inside, targets, names, entry))
            return
        if base == "jmp":
            source = operand(0)
            source.compute(out, "ea")
            known = sorted(self.tables.get(address, ()))
            out.append("switch (ea) {")
            for target in known:
                out.append("case 0x%06Xu: %s" % (target, self._goto(target, inside, targets, names, entry)))
            out.append("default: jmp_address(ea); return;")
            out.append("}")
            return
        if base.startswith("b") and base[1:] in CONDITIONS and record["flow"] == "cond":
            out.append("if (nmd_cc(%d)) %s" % (CONDITIONS[base[1:]],
                                               self._goto(record["targets"][0], inside, targets,
                                                          names, entry)))
            return
        if base.startswith("db") and base[2:] in CONDITIONS:
            reg = reg_expr(ops[0])
            cond = CONDITIONS[base[2:]]
            target = record["targets"][0]
            out.append("if (!nmd_cc(%d) && nmd_dbf(&%s)) %s" % (
                cond, reg, self._goto(target, inside, targets, names, entry)))
            return
        if base.startswith("s") and base[1:] in CONDITIONS and base not in ("sub", "swap", "subq",
                                                                            "suba", "subi", "sbcd"):
            dest = operand(0, 8)
            dest.compute(out)
            out.append(dest.write("nmd_cc(%d) ? 0xFF : 0x00" % CONDITIONS[base[1:]]))
            dest.finish(out)
            return
        if base == "trap":
            out.append("trap_call(%d, 0x%06Xu);" % (number(ops[0][1:]), following))
            return
        if base in ("trapv", "chk", "stop", "reset", "illegal", "abcd", "sbcd", "nbcd",
                    "addx", "subx", "negx", "movep", "roxl", "roxr", "rtr"):
            raise Refusal("%s at $%06X" % (base, address))

        # moves
        if base == "moveq":
            dest = operand(1, 32)
            value = number(ops[0][1:]) & 0xFF
            value = value - 0x100 if value & 0x80 else value
            out.append(dest.write("nmd_tst32(0x%08Xu)" % (value & 0xFFFFFFFF)))
            return
        if base in ("move", "movea"):
            if ops[1].lower() == "sr":
                source = operand(0, 16)
                source.compute(out, "ea")
                out.append("nmd_set_sr(%s);" % source.read())
                source.finish(out)
                return
            if ops[1].lower() == "ccr":
                source = operand(0, 16)
                source.compute(out, "ea")
                out.append("nmd_flags(0x1F, %s);" % source.read())
                source.finish(out)
                return
            if ops[0].lower() == "sr":
                dest = operand(1, 16)
                dest.compute(out)
                out.append(dest.write("nmd_cpu.sr"))
                dest.finish(out)
                return
            if "usp" in (ops[0].lower(), ops[1].lower()):
                if ops[1].lower() == "usp":
                    out.append("nmd_cpu.usp = %s;" % reg_expr(ops[0]))
                else:
                    out.append("%s = nmd_cpu.usp;" % reg_expr(ops[1]))
                return
            source, dest = operand(0), operand(1)
            source.compute(out, "src")
            value = source.read()
            out.append("value = %s;" % value)
            source.finish(out)
            dest.compute(out, "ea")
            if dest.kind == "areg":
                out.append(dest.write("value"))
            else:
                out.append(dest.write("nmd_tst%d((%s)value)" % (size, TYPES[size])))
            dest.finish(out)
            return
        if base == "movem":
            self._movem(ops, size, out)
            return
        if base in ("lea", "pea"):
            source = operand(0, 32)
            source.compute(out, "ea")
            if base == "lea":
                out.append("%s = ea;" % reg_expr(ops[1]))
            else:
                out.append("A[7] -= 4; wr32(A[7], ea);")
            return
        if base == "exg":
            out.append("value = %s; %s = %s; %s = value;" % (
                reg_expr(ops[0]), reg_expr(ops[0]), reg_expr(ops[1]), reg_expr(ops[1])))
            return
        if base == "link":
            reg = reg_expr(ops[0])
            out.append("A[7] -= 4; wr32(A[7], %s); %s = A[7]; A[7] += (uint32_t)%d;" % (
                reg, reg, number(ops[1][1:])))
            return
        if base == "unlk":
            reg = reg_expr(ops[0])
            out.append("A[7] = %s; %s = rd32(A[7]); A[7] += 4;" % (reg, reg))
            return
        if base == "swap":
            reg = reg_expr(ops[0])
            out.append("%s = nmd_swap(%s);" % (reg, reg))
            return
        if base == "ext":
            reg = reg_expr(ops[0])
            if size == 16:
                out.append("nmd_set16(&%s, nmd_tst16((uint16_t)(int16_t)(int8_t)%s));" % (reg, reg))
            else:
                out.append("%s = nmd_tst32(SX16(%s));" % (reg, reg))
            return

        # single operand
        if base in ("clr", "tst", "not", "neg", "tas"):
            dest = operand(0)
            dest.compute(out)
            if base == "clr":
                out.append(dest.write("nmd_tst%d(0)" % size))
            elif base == "tst":
                out.append("nmd_tst%d(%s);" % (size, dest.read()))
            elif base == "tas":
                out.append("value = nmd_tas(ea); nmd_tst8((uint8_t)value);")
            else:
                out.append("value = %s;" % dest.read())
                expr = ("nmd_tst%d((%s)~value)" % (size, TYPES[size]) if base == "not" else
                        "nmd_neg%d((%s)value)" % (size, TYPES[size]))
                out.append(dest.write(expr))
            dest.finish(out)
            return

        # bits
        if base in ("btst", "bset", "bclr", "bchg"):
            dest = operand(1, 32 if ops[1].lower().startswith("d") else 8)
            bit = ("%u" % (number(ops[0][1:]) & (31 if dest.kind == "dreg" else 7))
                   if ops[0].startswith("#") else
                   "(%s & %d)" % (reg_expr(ops[0]), 31 if dest.kind == "dreg" else 7))
            dest.compute(out)
            out.append("value = %s;" % (dest.reg if dest.kind == "dreg" else dest.read()))
            out.append("nmd_btst(value, %s);" % bit)
            if base != "btst":
                op = {"bset": "value | (1u << %s)", "bclr": "value & ~(1u << %s)",
                      "bchg": "value ^ (1u << %s)"}[base] % bit
                if dest.kind == "dreg":
                    out.append("%s = %s;" % (dest.reg, op))
                else:
                    out.append(dest.write("(uint8_t)(%s)" % op))
            dest.finish(out)
            return

        # shifts and rotates
        if base in ("lsl", "lsr", "asl", "asr", "rol", "ror"):
            if len(ops) == 1:
                dest, count = operand(0, 16), "1"
            else:
                dest = operand(1)
                count = ("%d" % number(ops[0][1:]) if ops[0].startswith("#") else
                         "(%s & 63)" % reg_expr(ops[0]))
            helper = {"lsl": "nmd_shl%d(%%s, %%s, 0)", "asl": "nmd_shl%d(%%s, %%s, 1)",
                      "lsr": "nmd_shr%d(%%s, %%s, 0)", "asr": "nmd_shr%d(%%s, %%s, 1)",
                      "rol": "nmd_rol%d(%%s, %%s, 0)", "ror": "nmd_rol%d(%%s, %%s, 1)"}[base]
            dest.compute(out)
            out.append("value = %s;" % dest.read())
            out.append(dest.write(helper % dest.size % ("(%s)value" % TYPES[dest.size], count)))
            dest.finish(out)
            return

        # multiply and divide
        if base in ("mulu", "muls", "divu", "divs"):
            source = operand(0, 16)
            source.compute(out, "src")
            out.append("nmd_%s(&%s, %s);" % (base, reg_expr(ops[1]), source.read()))
            source.finish(out)
            return

        # two-operand arithmetic and logic
        if base in ("add", "addq", "addi", "adda", "sub", "subq", "subi", "suba", "cmp", "cmpi",
                    "cmpa", "cmpm", "and", "andi", "or", "ori", "eor", "eori"):
            self._arith(base, ops, size, out, address)
            return
        raise Refusal("%s at $%06X" % (base, address))

    def _arith(self, base, ops, size, out, address):
        kind = base.rstrip("qia") if base not in ("adda", "suba", "cmpa") else base[:3]
        kind = {"ad": "add", "su": "sub", "cm": "cmp", "an": "and", "o": "or", "eo": "eor"}.get(
            kind, kind)
        if ops[1].lower() in ("sr", "ccr"):
            value = number(ops[0][1:])
            op = {"and": "&", "or": "|", "eor": "^"}[kind]
            if ops[1].lower() == "sr":
                out.append("nmd_set_sr((uint16_t)(nmd_cpu.sr %s 0x%Xu));" % (op, value & 0xFFFF))
            else:
                out.append("nmd_flags(0x1F, (nmd_cpu.sr %s 0x%Xu) & 0x1F);" % (op, value & 0x1F))
            return
        source, dest = Operand(ops[0], size), Operand(ops[1], size)
        if base.endswith("q"):
            source = Operand("#%d" % number(ops[0][1:]), size)
        if dest.kind == "areg":             # ADDA, SUBA, CMPA, ADDQ/SUBQ to An: long, no flags
            source.compute(out, "src")
            value = source.read() if size == 32 else "SX16(%s)" % source.read()
            source.finish(out)
            if kind == "cmp":
                out.append("nmd_cmp32(%s, %s);" % (dest.reg, value))
            else:
                out.append("%s %s= %s;" % (dest.reg, "+" if kind == "add" else "-", value))
            return
        if base == "cmpm":
            raise Refusal("cmpm at $%06X" % address)
        source.compute(out, "src")
        out.append("value = %s;" % source.read())
        source.finish(out)
        dest.compute(out, "ea")
        current = dest.read()
        typed = TYPES[size]
        if kind in ("add", "sub"):
            expr = "nmd_%s%d(%s, (%s)value)" % (kind, size, current, typed)
        elif kind == "cmp":
            out.append("nmd_cmp%d(%s, (%s)value);" % (size, current, typed))
            dest.finish(out)
            return
        else:
            expr = "nmd_tst%d((%s)(%s %s value))" % (size, typed, current,
                                                    {"and": "&", "or": "|", "eor": "^"}[kind])
        out.append(dest.write(expr))
        dest.finish(out)

    def _movem(self, ops, size, out):
        to_memory = not ops[0].startswith("(") and not ops[0].startswith("-(")
        names = reglist(ops[0] if to_memory else ops[1])
        memory = Operand(ops[1] if to_memory else ops[0], size)
        step = size // 8
        if to_memory and memory.kind == "pre":
            for name in reversed(names):
                out.append("A[%s] -= %d; wr%d(%s, (%s)%s);" % (
                    memory.reg[2], step, size, memory.reg, TYPES[size], reg_expr(name)))
            return
        if memory.kind == "post":
            out.append("ea = %s;" % memory.reg)
        else:
            memory.compute(out, "ea")
        for name in names:
            if to_memory:
                out.append("wr%d(ea, (%s)%s); ea += %d;" % (size, TYPES[size], reg_expr(name), step))
            else:
                value = "rd32(ea)" if size == 32 else "SX16(rd16(ea))"
                out.append("%s = %s; ea += %d;" % (reg_expr(name), value, step))
        if memory.kind == "post":
            out.append("%s = ea;" % memory.reg)


STANDALONE = """
/* standalone: the drafts are the whole routine table */
const nmd_routine *md_find(uint32_t address);
void jsr_address(uint32_t return_address, uint32_t address)
{
    const nmd_routine *routine = md_find(address);

    A[7] -= 4;
    wr32(A[7], return_address);
    if (routine)
        routine->abi();
    else
        nmd_check_call_fallback(1, address);
}

void jmp_address(uint32_t address)
{
    const nmd_routine *routine = md_find(address);

    if (routine)
        routine->abi();
    else
        nmd_check_call_fallback(1, address);
}

const nmd_routine *md_find(uint32_t address)
{
    for (size_t index = 0; index < nmd_game_routine_count; index++)
        if (nmd_game_routines[index].addr == address)
            return &nmd_game_routines[index];
    return 0;
}

void trap_call(unsigned number, uint32_t return_pc)
{
    (void)return_pc;
    nmd_check_call_fallback(1, rd32(4u * (32u + number)));
}

const nmd_z80_routine nmd_game_z80_routines[] = {{0, 0, 0}};
const size_t nmd_game_z80_routine_count = 0;
"""

HEADER = """#include <stddef.h>
#include <stdint.h>

#include "nmd/cart.h"
#include "nmd_decomp.h"

#ifndef D
#define D nmd_cpu.d
#define A nmd_cpu.a
#endif
#ifndef SX16
#define SX16(value) ((uint32_t)(int32_t)(int16_t)(value))
#endif

void jsr_address(uint32_t return_address, uint32_t address);
void jmp_address(uint32_t address);
void trap_call(unsigned number, uint32_t return_pc);
"""


def write(rom, codemap, addresses, out_path, existing=None, table="md_draft_routines",
          standalone=False):
    """Draft every address; write one C file. Returns (drafted, refusals). With
    `standalone` the table is the game's (nmd_game_routines) and the call-by-
    address helpers are defined here."""
    drafter = Drafter(rom, codemap)
    names = dict(existing or {})
    for address in addresses:
        names.setdefault(address, "draft_%06X" % address)
    drafted, refusals, bodies = [], {}, []
    for address in addresses:
        if (existing or {}).get(address):
            continue
        try:
            code = drafter.draft(address, names)
        except Refusal as exc:
            refusals[address] = str(exc)
            continue
        drafted.append(address)
        bodies.append((address, code))
    # a refused routine has no C: calls to it go by address
    for address in refusals:
        names.pop(address, None)
    if refusals:
        drafter2 = Drafter(rom, codemap)
        bodies = [(address, drafter2.draft(address, names)) for address, _ in bodies]
    lines = ["/* Generated by nativekit consoles/md/tools/decomp/draft68k.py: draft C (T2),",
             " * one group per original instruction. Held only by check_calls.py and",
             " * lockstep.py; a person rewrites it as plain C (D09). */", HEADER]
    if standalone:
        table = "nmd_game_routines"
        lines.append("extern const nmd_routine nmd_game_routines[];")
        lines.append("extern const size_t nmd_game_routine_count;")
    for address in drafted:
        lines.append("void draft_%06X(void);" % address)
    for name in sorted(set((existing or {}).values())):
        lines.append("void %s(void);" % name)
    lines.append("")
    for address, code in bodies:
        lines.append("/* $%06X */" % address)
        lines.append("void draft_%06X(void)" % address)
        lines.append("{")
        lines.append("    uint32_t ea = 0, src = 0, value = 0;")
        lines.append("    (void)ea; (void)src; (void)value;")
        lines.append(code)
        lines.append("}")
        lines.append("")
    lines.append("const nmd_routine %s[] = {" % table)
    for address in drafted:
        lines.append('    {0x%06X, draft_%06X, "draft_%06X"},' % (address, address, address))
    if not drafted:
        lines.append("    {0, 0, 0},")
    lines.append("};")
    lines.append("const size_t %s_count = %d;" % (table[:-1] if table.endswith("s") else table,
                                                  len(drafted)))
    if standalone:
        lines.append(STANDALONE)
    pathlib.Path(out_path).write_text("\n".join(lines) + "\n")
    return drafted, refusals


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--rom", required=True)
    parser.add_argument("--codemap", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--have", default="", help="ADDRESS=name,... routines that already have C")
    parser.add_argument("--table", default="md_draft_routines", help="the exported table's name")
    parser.add_argument("--standalone", action="store_true",
                        help="export nmd_game_routines and define the call-by-address helpers")
    parser.add_argument("address", nargs="+")
    args = parser.parse_args()
    existing = {}
    for item in filter(None, args.have.split(",")):
        address, name = item.split("=")
        existing[int(address, 16)] = name
    drafted, refusals = write(pathlib.Path(args.rom).read_bytes(),
                              json.loads(pathlib.Path(args.codemap).read_text()),
                              [int(a, 16) for a in args.address], args.out, existing,
                              args.table, args.standalone)
    print("draft68k: %d drafted, %d refused" % (len(drafted), len(refusals)))
    for address, reason in sorted(refusals.items()):
        print("  $%06X refused: %s" % (address, reason))


if __name__ == "__main__":
    main()
