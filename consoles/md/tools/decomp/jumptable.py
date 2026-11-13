"""Where a 68000 computed transfer can go, read from the code around it.

A trace gives the targets a `jmp`/`jsr` through a register took while it was
watched. The instructions before the transfer often say where every target
is. Four forms are recognised, each by the opcodes that load the register:

  direct    lea T,An ... jsr (An)                       the target is T
  pointer   lea T,Am [adda.w x,Am] movea.l (..Am..),An ... jsr (An)
            movea.l (T,pc,Xn),An ... jsr (An)           rows of long addresses at T
  offset    lea T,An  move.w (d,An,Xm),Dn  jsr (d',An,Dn.w)
                                                        rows of words added to T+d'
  branch    jmp (T,pc,Xn)                               rows of bra.w at T
  inline    jmp (T,pc,Xn)                               code at T, entered at a computed
                                                        distance: only T is known

The register's value is followed backward over fall-through and branches. A
path that reaches a call, a routine entry, another write of the register or
the search depth is unknown, and what the other paths found is marked
incomplete. How many rows a table has is not written in the code, so a table
ends at the first row that: is not a target that decodes on an instruction
boundary; lies in decoded code; is where another table begins; or is at or
past the table's own lowest target (a table followed by its handlers). A
null long pointer is passed over; two in a row end the table.

This is inference, not observation. Its instructions carry `table` evidence,
the weakest, and a trace that saw the transfer reach an address the tables
do not hold refuses them.
"""
import m68k_dis

DEPTH = 24          # instructions followed backward on one path
MAX_ROWS = 256
FIRST_CODE = 0x200  # the vector table and header are never targets


def _word(data, offset):
    return int.from_bytes(data[offset:offset + 2], "big")


def _s8(value):
    return value - 0x100 if value & 0x80 else value


def _s16(value):
    return value - 0x10000 if value & 0x8000 else value


def _address_source(data, offset, address, mode, reg):
    """The address an effective address names when it needs no register."""
    if mode == 7 and reg == 2:
        return address + 2 + _s16(_word(data, offset + 2))
    if mode == 7 and reg == 1:
        return int.from_bytes(data[offset + 2:offset + 6], "big") & 0xFFFFFF
    if mode == 7 and reg == 0:
        return _s16(_word(data, offset + 2)) & 0xFFFFFF
    return None


def _writes_address_register(text, number):
    name = "a%d" % number
    return text.endswith("," + name) or "(%s)+" % name in text or "-(%s)" % name in text


class _Finder:
    def __init__(self, space):
        self.space = space
        self.data = space.data
        self.before = {}        # address -> instructions that can run just before it
        for address, record in space.instructions.items():
            if record["flow"] in (m68k_dis.SEQ, m68k_dis.COND, m68k_dis.CALL):
                self.before.setdefault(address + record["length"], []).append(address)
            if record["flow"] in (m68k_dis.JUMP, m68k_dis.COND):
                for target in record["targets"]:
                    self.before.setdefault(target, []).append(address)

    def loads(self, at, register):
        """What the paths into `at` leave in address register `register`:
        ([("direct", T) | ("pointer", T)], complete)."""
        found, complete = [], True
        seen = set()
        # state: (address reached, register wanted, None or the pointer displacement owed)
        work = [(previous, register, None, 0) for previous in self.before.get(at, ())]
        if not work:
            return [], False
        if at in self.space.entries:
            complete = False
        while work:
            address, wanted, pointer, depth = work.pop()
            if (address, wanted, pointer) in seen:
                continue
            seen.add((address, wanted, pointer))
            record = self.space.instructions[address]
            offset = address - self.space.base
            op = _word(self.data, offset)
            mode, reg = (op >> 3) & 7, op & 7
            to_wanted = (op >> 9) & 7 == wanted
            if record["flow"] == m68k_dis.CALL or depth >= DEPTH:
                complete = False
                continue
            if op & 0xF1C0 == 0x41C0 and to_wanted:                 # lea ea,An
                source = _address_source(self.data, offset, address, mode, reg)
                if source is None:
                    complete = False
                elif pointer is None:
                    found.append(("direct", source))
                else:
                    found.append(("pointer", (source + pointer) & 0xFFFFFF))
                continue
            if op & 0xF1C0 == 0x2040 and to_wanted and pointer is None:   # movea.l ea,An
                if mode == 7 and reg == 3:
                    found.append(("pointer", address + 2 + _s8(_word(self.data, offset + 2) & 0xFF)))
                    continue
                displacement = {2: 0, 5: _s16(_word(self.data, offset + 2)),
                                6: _s8(_word(self.data, offset + 2) & 0xFF)}.get(mode)
                if displacement is None:
                    complete = False
                    continue
                wanted, pointer = reg, displacement     # now: where does the table register come from
            elif op & 0xF1C0 == 0xD0C0 and to_wanted and pointer is not None:
                pass                                    # adda.w x,Am: the index into the table
            elif _writes_address_register(record["text"] or "", wanted):
                complete = False
                continue
            previous = self.before.get(address, ())
            if not previous or address in self.space.entries:
                complete = False                        # a routine's caller set the register
                if not previous:
                    continue
            work.extend((p, wanted, pointer, depth + 1) for p in previous)
        return sorted(set(found)), complete

    def candidates(self, site):
        """The tables a transfer at `site` reads: [(kind, at, base, complete)]."""
        offset = site - self.space.base
        op = _word(self.data, offset)
        if op & 0xFF80 != 0x4E80:
            return []
        mode, reg = (op >> 3) & 7, op & 7
        if mode == 2:
            found, complete = self.loads(site, reg)
            return [(kind, at, None, complete) for kind, at in found]
        if mode == 7 and reg == 3:
            at = site + 2 + _s8(_word(self.data, offset + 2) & 0xFF)
            if _word(self.data, at - self.space.base) == 0x6000:
                return [("branch", at, None, True)]
            return [("inline", at, None, False)]     # code entered at a computed distance
        if mode == 6:
            extension = _word(self.data, offset + 2)
            previous = self.before.get(site, ())
            if len(previous) != 1 or extension & 0x8800:      # index must be Dn.w
                return []
            load = previous[0]
            load_op = _word(self.data, load - self.space.base)
            load_extension = _word(self.data, load - self.space.base + 2)
            # move.w (d,An,Xm),Dn with Dn the transfer's index and An its base
            if load_op & 0xF1F8 != 0x3030 or load_op & 7 != reg or \
                    (load_op >> 9) & 7 != (extension >> 12) & 7:
                return []
            found, complete = self.loads(load, reg)
            return [("offset", at + _s8(load_extension & 0xFF), at + _s8(extension & 0xFF), complete)
                    for kind, at in found if kind == "direct"]
        return []

    def target_ok(self, target):
        space = self.space
        if target & 1 or target < FIRST_CODE or not space.contains(target):
            return False
        offset = target - space.base
        if space.classes[offset] >= 3 and target not in space.instructions:
            return False                                # inside another instruction
        insn = m68k_dis.decode(space.data, offset, target)
        return insn is not None and insn.text is not None

    def rows(self, kind, at, base, starts):
        """(rows read, targets) of the table at `at`; `starts` are the
        addresses where other tables begin."""
        space = self.space
        if kind in ("direct", "inline"):
            return 0, [at] if self.target_ok(at) else []
        width = {"pointer": 4, "offset": 2, "branch": 4}[kind]
        targets, lowest, nulls, count, rows = [], None, 0, 0, 0
        while count < MAX_ROWS:
            row = at + count * width
            offset = row - space.base
            if offset + width > len(space.data) or (lowest is not None and row >= lowest) or \
                    (count and row in starts):
                break
            if kind == "branch":
                if _word(space.data, offset) != 0x6000 or not self.target_ok(row):
                    break
                target, goes = row, row + 2 + _s16(_word(space.data, offset + 2))
            else:
                if any(space.classes[offset + index] >= 3 for index in range(width)):
                    break                               # decoded code is not a table
                if kind == "pointer":
                    target = int.from_bytes(space.data[offset:offset + 4], "big")
                    if target == 0:
                        nulls += 1
                        if nulls == 2:
                            break
                        count += 1
                        continue
                else:
                    target = base + _s16(_word(space.data, offset))
                if not self.target_ok(target):
                    break
                goes = target
            nulls = 0
            if goes > at and (lowest is None or goes < lowest):
                lowest = goes
            targets.append(target)
            count += 1
            rows = count                                # trailing nulls are not rows
        if kind == "offset" and lowest is None:
            return 0, []                                # nothing bounds a table of bare words
        return rows, sorted(set(targets))


def discover(space, sites, starts):
    """Tables for the computed transfers at `sites` in a ROM space, as
    records {site, kind, at, rows, targets, complete, status[, reason]}.
    `starts` is the set of addresses where tables found so far begin; it is
    added to. A site whose tables are all complete and still do not hold a
    target a trace saw has them refused."""
    finder = _Finder(space)
    found = {site: finder.candidates(site) for site in sorted(sites)}
    starts.update(at for candidates in found.values() for kind, at, _, _ in candidates
                  if kind in ("pointer", "offset"))
    records = []
    for site, candidates in found.items():
        tables = []
        for kind, at, base, complete in candidates:
            rows, targets = finder.rows(kind, at, base, starts)
            if targets:
                tables.append({"site": site, "kind": kind, "at": at, "rows": rows,
                               "targets": targets, "complete": complete, "status": "accepted"})
        held = {target for table in tables for target in table["targets"]}
        if all(table["complete"] for table in tables) and tables and \
                not space.unresolved.get(site, set()) <= held:
            for table in tables:
                table["status"] = "refused"
                table["reason"] = "a trace saw an observed target the table does not hold"
        records.extend(tables)
    return records
