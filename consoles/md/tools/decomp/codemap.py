"""The code map: what each byte of a cartridge is known to be, and why.

A *space* is a run of bytes one CPU executes from: the ROM image (offsets are
file offsets; the base cartridge maps offset n at 68000 address n) or one
*generation* of a program in RAM (work RAM for the 68000, Z80 RAM for the
Z80), identified by CPU, load address and the SHA-256 of its bytes. One RAM
address holds different generations over time; they are never merged unless
both the address and the bytes are equal.

Per-byte classes, strongest first:
  opcode / operand   part of an instruction reached from an entry
  structure          the vector table and cartridge header, by format
  read               a CPU read it as data in a trace (it may still be code
                     that never ran: a checksum loop reads everything)
  unknown            nothing is known; never counted as code or data

Each instruction carries its evidence: `traced` (an execution trace ran it),
`vector` (reached from the reset or an exception vector without running) or
`static` (reached only by following code). Overlapping instructions are kept
as conflicts; computed transfers are kept as unresolved, with the targets a
trace saw. Nothing here guesses.
"""
import hashlib
import json

import m68k_dis
import z80_dis

SCHEMA = "nmd-codemap"
VERSION = 1
UNKNOWN, READ, STRUCTURE, OPERAND, OPCODE = range(5)
CLASS_NAMES = ("unknown", "read", "structure", "operand", "opcode")
EVIDENCE_RANK = {"static": 0, "vector": 1, "traced": 2}
DECODERS = {"m68k": m68k_dis.decode, "z80": z80_dis.decode}
OPCODE_BYTES = {"m68k": 2, "z80": 1}
Z80_RAM_SIZE = 0x2000
WORK_RAM_SIZE = 0x10000


class CodeMapError(ValueError):
    """Evidence or a map that cannot be used."""


def sha256(data):
    return hashlib.sha256(bytes(data)).hexdigest()


class Space:
    def __init__(self, space_id, cpu, kind, base, data):
        self.id = space_id
        self.cpu = cpu
        self.kind = kind            # "rom" or "ram"
        self.base = base            # CPU address of data[0]
        self.data = bytes(data)
        self.classes = bytearray(len(self.data))
        self.instructions = {}      # address -> record
        self.entries = {}           # address -> {"kinds": set, "evidence": str}
        self.conflicts = []         # (address, other address)
        self.unresolved = {}        # address -> set of observed targets
        self.undecodable = {}       # address -> evidence
        self.external = set()       # direct targets outside the space
        self.source_rom_offset = None
        self.occurrences = 1

    def sha256(self):
        return sha256(self.data)

    def contains(self, address):
        return self.base <= address < self.base + len(self.data)

    def add_entry(self, address, kind, evidence):
        entry = self.entries.setdefault(address, {"kinds": set(), "evidence": evidence})
        entry["kinds"].add(kind)
        if EVIDENCE_RANK[evidence] > EVIDENCE_RANK[entry["evidence"]]:
            entry["evidence"] = evidence

    def walk(self, starts, evidence):
        """Follow code from `starts`, recording every instruction reached."""
        decode = DECODERS[self.cpu]
        work = list(starts)
        while work:
            address = work.pop()
            if not self.contains(address):
                self.external.add(address)
                continue
            known = self.instructions.get(address)
            if known is not None:
                if EVIDENCE_RANK[evidence] <= EVIDENCE_RANK[known["evidence"]]:
                    continue
                known["evidence"] = evidence  # and carry the stronger evidence onward
                insn = decode(self.data, address - self.base, address)
            else:
                insn = decode(self.data, address - self.base, address)
                if insn is None:
                    old = self.undecodable.get(address)
                    if old is None or EVIDENCE_RANK[evidence] > EVIDENCE_RANK[old]:
                        self.undecodable[address] = evidence
                    continue
                self._claim(address, insn, evidence)
            if insn.indirect:
                self.unresolved.setdefault(address, set())
            for target in insn.targets:
                if insn.flow == m68k_dis.CALL and self.contains(target):
                    self.add_entry(target, "call", evidence)
                work.append(target)
            if insn.flow in (m68k_dis.SEQ, m68k_dis.COND, m68k_dis.CALL):
                work.append(address + insn.length)

    def _claim(self, address, insn, evidence):
        start = address - self.base
        for other in range(max(0, start - 15), start + insn.length):
            record = self.instructions.get(self.base + other)
            if record is not None and other != start and other + record["length"] > start:
                self.conflicts.append((address, self.base + other))
        self.instructions[address] = {
            "length": insn.length, "text": insn.text, "flow": insn.flow,
            "targets": list(insn.targets), "indirect": insn.indirect, "evidence": evidence,
        }
        head = OPCODE_BYTES[self.cpu]
        for index in range(insn.length):
            wanted = OPCODE if index < head else OPERAND
            if self.classes[start + index] < wanted:
                self.classes[start + index] = wanted

    def mark(self, start, length, value):
        for index in range(max(0, start), min(len(self.data), start + length)):
            if self.classes[index] < value:
                self.classes[index] = value

    def conflicted_bytes(self):
        flagged = set()
        for first, second in self.conflicts:
            for address in (first, second):
                record = self.instructions[address]
                flagged.update(range(address - self.base, address - self.base + record["length"]))
        return flagged

    def orphan_candidates(self):
        """Unclaimed bytes right after an instruction that never falls through
        which decode cleanly to another such instruction: possibly code no
        entry reaches. Candidates only; they earn no classification."""
        decode = DECODERS[self.cpu]
        found = []
        for address in sorted(self.instructions):
            record = self.instructions[address]
            if record["flow"] not in (m68k_dis.RET, m68k_dis.JUMP):
                continue
            start = address + record["length"]
            cursor = start
            count = 0
            while self.contains(cursor) and self.classes[cursor - self.base] < OPERAND:
                insn = decode(self.data, cursor - self.base, cursor)
                if insn is None or insn.text is None:
                    count = 0
                    break
                count += 1
                cursor += insn.length
                if insn.flow in (m68k_dis.RET, m68k_dis.JUMP):
                    break
            else:
                count = 0
            if count and any(self.data[start - self.base:cursor - self.base]):
                found.append({"at": start, "length": cursor - start, "instructions": count})
        return found

    def counts(self):
        totals = dict.fromkeys(CLASS_NAMES, 0)
        for value in self.classes:
            totals[CLASS_NAMES[value]] += 1
        by_evidence = dict.fromkeys(EVIDENCE_RANK, 0)
        for record in self.instructions.values():
            by_evidence[record["evidence"]] += record["length"]
        return totals, by_evidence

    def to_json(self):
        runs = []
        start = 0
        for index in range(1, len(self.classes) + 1):
            if index == len(self.classes) or self.classes[index] != self.classes[start]:
                runs.append([start, index - start, CLASS_NAMES[self.classes[start]]])
                start = index
        return {
            "id": self.id, "cpu": self.cpu, "kind": self.kind, "base": self.base,
            "size": len(self.data), "sha256": sha256(self.data),
            "source_rom_offset": self.source_rom_offset, "occurrences": self.occurrences,
            "classes": runs,
            "instructions": [dict(self.instructions[a], at=a) for a in sorted(self.instructions)],
            "entries": [{"at": a, "kinds": sorted(e["kinds"]), "evidence": e["evidence"]}
                        for a, e in sorted(self.entries.items())],
            "conflicts": [list(pair) for pair in sorted(set(self.conflicts))],
            "unresolved": [{"at": a, "observed_targets": sorted(t)}
                           for a, t in sorted(self.unresolved.items())],
            "undecodable": [{"at": a, "evidence": e} for a, e in sorted(self.undecodable.items())],
            "external_targets": sorted(self.external),
            "orphan_candidates": self.orphan_candidates(),
        }


class _Ram:
    """One CPU's RAM as a trace's writes leave it, and the program
    generations its executed instructions belong to."""

    def __init__(self, cpu, size, origin):
        self.cpu = cpu
        self.size = size
        self.origin = origin          # CPU address of offset 0
        self.bytes = bytearray(size)
        self.code = bytearray(size)   # 1 where the open generation executed
        self.executed = {}            # offset -> length, the open generation
        self.edges = set()            # (from, to) flow edges seen in it
        self.closed = []              # (snapshot, executed, edges)

    def write(self, address, value):
        offset = address % self.size
        if self.bytes[offset] == value:
            return
        if self.code[offset]:
            self.close()
        self.bytes[offset] = value

    def execute(self, address):
        offset = address % self.size
        if offset in self.executed:
            return
        insn = DECODERS[self.cpu](bytes(self.bytes[offset:offset + 16]), 0, self.origin + offset)
        length = insn.length if insn else 1
        self.executed[offset] = length
        for index in range(offset, min(self.size, offset + length)):
            self.code[index] = 1

    def close(self):
        if self.executed:
            self.closed.append((bytes(self.bytes), self.executed, self.edges))
            self.executed = {}
            self.edges = set()
            self.code = bytearray(self.size)


def load_evidence(capture_directory, rom_sha256):
    """The NMDH capture's instruction, flow and bus events, after its
    manifest proves it was made from this ROM and completed."""
    import pathlib
    import trace as nmdh_trace
    directory = pathlib.Path(capture_directory)
    try:
        manifest = json.loads((directory / "manifest.json").read_text())
    except (OSError, ValueError) as exc:
        raise CodeMapError("capture manifest unreadable: %s" % exc)
    identities = (manifest.get("metadata") or {}).get("identities") or {}
    if identities.get("rom_sha256") != rom_sha256:
        raise CodeMapError("capture was made from a different ROM")
    if manifest.get("status") != "complete":
        raise CodeMapError("capture is not complete: %r" % manifest.get("status"))
    return _Evidence(directory / "capture.nmdh")


class _Evidence:
    """One capture's instruction, flow and bus events, read from the file on
    each pass so a capture never has to fit in memory. The file is fully
    validated before its first event is yielded."""

    def __init__(self, path):
        self.path = path

    def __iter__(self):
        import trace as nmdh_trace
        instructions = 0
        try:
            for event in nmdh_trace.read_nmdh(self.path):
                if event["kind"] in (1, 2, 3):
                    instructions += event["kind"] == 1
                    yield event
        except (OSError, nmdh_trace.NMDHError) as exc:
            raise CodeMapError("capture unreadable: %s" % exc)
        if not instructions:
            raise CodeMapError("capture holds no instruction events")


def build(rom, evidence_sets=()):
    """The code map of `rom` from its vectors and any traces' events."""
    rom = bytes(rom)
    rom_space = Space("rom", "m68k", "rom", 0, rom)
    if len(rom) < 0x200:
        raise CodeMapError("image is shorter than a vector table and header")
    rom_space.mark(0, 0x100, STRUCTURE)
    if rom[0x100:0x104] == b"SEGA":
        rom_space.mark(0x100, 0x100, STRUCTURE)
    rams = {"m68k": _Ram("m68k", WORK_RAM_SIZE, 0xFF0000), "z80": _Ram("z80", Z80_RAM_SIZE, 0)}
    traced_rom = set()
    rom_edges = set()
    reads = set()

    for events in evidence_sets:
        for ram in rams.values():   # a new trace starts from power-on
            ram.close()
            ram.bytes = bytearray(ram.size)
        for event in events:
            cpu = "m68k" if event["cpu"] == 1 else "z80"
            if event["kind"] == 1:
                pc = event["pc"]
                if cpu == "m68k":
                    pc &= 0xFFFFFF
                    if pc < len(rom):
                        traced_rom.add(pc)
                    elif pc >= 0xE00000:
                        rams["m68k"].execute(pc)
                elif pc < Z80_RAM_SIZE:
                    rams["z80"].execute(pc)
            elif event["kind"] == 2:
                edge = (event["payload"]["previous_pc"], event["payload"]["current_pc"])
                if cpu == "m68k" and (edge[0] & 0xFFFFFF) < len(rom):
                    rom_edges.add((edge[0] & 0xFFFFFF, edge[1] & 0xFFFFFF))
                elif cpu == "m68k":
                    rams["m68k"].edges.add((edge[0] | 0xFF0000, edge[1] & 0xFFFFFF))
                else:
                    rams["z80"].edges.add(edge)
            else:
                payload = event["payload"]
                address, width = payload["address"], payload["width"]
                data = payload["value"].to_bytes(4, "big")[4 - width:]
                if payload["operation"] == 2:
                    for index, value in enumerate(data):
                        target = address + index
                        if cpu == "z80":
                            if target < Z80_RAM_SIZE:
                                rams["z80"].write(target, value)
                        elif 0xA00000 <= (target & 0xFFFFFF) < 0xA00000 + Z80_RAM_SIZE:
                            rams["z80"].write(target & 0x1FFF, value)
                        elif (target & 0xFFFFFF) >= 0xE00000:
                            rams["m68k"].write(target & 0xFFFF, value)
                elif cpu == "m68k" and payload["provenance"] != 1 and \
                        (address & 0xFFFFFF) < len(rom):
                    reads.add((address & 0xFFFFFF, width))
        for ram in rams.values():
            ram.close()

    # the ROM: traced code first, then vectors, each followed statically
    rom_space.walk(sorted(traced_rom, reverse=True), "traced")
    _downgrade_untraced(rom_space, traced_rom)
    reset = int.from_bytes(rom[4:8], "big") & 0xFFFFFF
    rom_space.add_entry(reset, "reset", "vector")
    vectors = [reset]
    for number in range(2, 64):
        target = int.from_bytes(rom[number * 4:number * 4 + 4], "big") & 0xFFFFFF
        if target < len(rom) and not target & 1 and target >= 0x200:
            rom_space.add_entry(target, "vector", "vector")
            vectors.append(target)
    rom_space.walk(vectors, "vector")
    for source, target in sorted(rom_edges):
        if source in rom_space.unresolved:
            rom_space.unresolved[source].add(target)
            if rom_space.contains(target):
                rom_space.add_entry(target, "computed", "traced")
    for address, width in sorted(reads):
        rom_space.mark(address, width, READ)

    spaces = [rom_space]
    by_identity = {}
    for cpu, ram in rams.items():
        for snapshot, executed, edges in ram.closed:
            # the generation's extent is what ran: first executed byte to last
            low = min(executed)
            high = max(offset + length for offset, length in executed.items())
            data = snapshot[low:high]
            base = ram.origin + low
            identity = (cpu, base, sha256(data))
            if identity in by_identity:
                by_identity[identity].occurrences += 1
                continue
            name = "%s-ram-%06X-%s" % (cpu, base, identity[2][:12])
            space = Space(name, cpu, "ram", base, data)
            ran = {ram.origin + offset for offset in executed}
            space.walk(sorted(ran, reverse=True), "traced")
            _downgrade_untraced(space, ran)
            for source, target in sorted(edges):
                if source in space.unresolved:
                    space.unresolved[source].add(target)
                    if space.contains(target):
                        space.add_entry(target, "computed", "traced")
            found = rom.find(data)
            if found >= 0 and rom.find(data, found + 1) < 0:
                space.source_rom_offset = found
            by_identity[identity] = space
            spaces.append(space)
    return spaces


def _downgrade_untraced(space, traced):
    """A walk from traced code reaches instructions the trace never ran:
    those are static evidence, not traced."""
    for address, record in space.instructions.items():
        if address not in traced:
            record["evidence"] = "static"
    for address, entry in space.entries.items():
        if address not in traced:
            entry["evidence"] = "static"


def document(rom, spaces, neutral_id=None):
    return {
        "schema": SCHEMA, "version": VERSION, "game": neutral_id,
        "rom": {"sha256": sha256(rom), "size": len(rom)},
        "spaces": [space.to_json() for space in spaces],
    }
