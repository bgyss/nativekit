"""The code map: what each byte of a cartridge is known to be, and why.

A *space* is a run of bytes one CPU executes from: the ROM image (offsets are
file offsets; the base cartridge maps offset n at 68000 address n) or one
*generation* of a program in RAM (work RAM for the 68000, Z80 RAM for the
Z80). One RAM address holds different generations over time. Two generations
are one program, a *family*, only when they overlap and every byte that
differs between them lies in an instruction both decode to the same length,
flow, transfer targets and text apart from its numeric literals: a program
whose immediate operands another CPU rewrites is one program with variants.
A differing opcode, register, branch target or non-instruction byte keeps
two generations apart. A family's space is named by CPU, load address and
the SHA-256 of one variant's bytes (the one with a verbatim ROM source when
there is one); the others are listed by hash with the bytes that vary.

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
import re
import zlib

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
        self.variant_sha256 = [sha256(self.data)]   # every content this program had
        self.patched = []           # offsets whose value differs between variants

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
            "variant_sha256": self.variant_sha256, "patched": _runs(self.patched),
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


def _runs(offsets):
    """Sorted offsets as [start, length] runs."""
    runs = []
    for offset in sorted(offsets):
        if runs and runs[-1][0] + runs[-1][1] == offset:
            runs[-1][1] += 1
        else:
            runs.append([offset, 1])
    return runs


_LITERAL = re.compile(r"(?<![A-Za-z0-9_$])(\$[0-9A-Fa-f]+|[0-9]+)")


def _shape(text):
    """Instruction text with its numeric literals blanked; register numbers
    are part of a name and stay."""
    return _LITERAL.sub("#", text)


def _pack(snapshot):
    return zlib.compress(snapshot, 1)


def _unpack(packed):
    return zlib.decompress(packed)


class _Family:
    """Generations of one RAM program: equal except in the numeric operands
    of instructions that otherwise decode alike (see the module text)."""

    def __init__(self, cpu, origin, generation):
        snapshot, executed, edges = generation
        self.cpu = cpu
        self.origin = origin
        self.first = snapshot
        self.members = [_pack(snapshot)]    # every generation's RAM, compressed
        self.low = min(executed)
        self.high = max(offset + length for offset, length in executed.items())
        self.starts = set(executed)
        self.edges = set(edges)
        self.space, self.owner = self._walk(snapshot, self.low, self.high, self.starts)

    def _walk(self, snapshot, low, high, starts):
        space = Space("", self.cpu, "ram", self.origin + low, snapshot[low:high])
        space.walk(sorted((self.origin + offset for offset in starts), reverse=True), "traced")
        owner = {}
        for address, record in space.instructions.items():
            for index in range(address - space.base, address - space.base + record["length"]):
                owner[index] = None if index in owner else address   # None: overlapping
        return space, owner

    @staticmethod
    def _alike(space, owner, other):
        """Whether `other` (bytes over the space's extent) is the space's
        program; returns the differing offsets, or None when it is not."""
        if other == space.data:
            return []
        differing = [index for index, (mine, theirs) in enumerate(zip(space.data, other))
                     if mine != theirs]
        touched = set()
        for index in differing:
            address = owner.get(index)
            if address is None:
                return None     # not in an instruction, or in overlapping ones
            touched.add(address)
        for address in touched:
            record = space.instructions[address]
            insn = DECODERS[space.cpu](other, address - space.base, address)
            if insn is None or insn.text is None or record["text"] is None or \
                    insn.length != record["length"] or insn.flow != record["flow"] or \
                    list(insn.targets) != record["targets"] or \
                    insn.indirect != record["indirect"] or \
                    _shape(insn.text) != _shape(record["text"]):
                return None
        return differing

    def admit(self, generation):
        """Add the generation if it is this program; False when it is not."""
        snapshot, executed, edges = generation
        low = min(executed)
        high = max(offset + length for offset, length in executed.items())
        if low >= self.high or high <= self.low:
            return False
        low, high = min(low, self.low), max(high, self.high)
        if (low, high) == (self.low, self.high) and self.starts.issuperset(executed):
            space, owner, recheck = self.space, self.owner, ()
        else:   # more of the program ran: every member must hold over the larger walk
            space, owner = self._walk(self.first, low, high, self.starts | set(executed))
            recheck = self.members[1:]
        if self._alike(space, owner, snapshot[low:high]) is None or any(
                self._alike(space, owner, _unpack(member)[low:high]) is None for member in recheck):
            return False
        self.members.append(_pack(snapshot))
        self.low, self.high, self.space, self.owner = low, high, space, owner
        self.starts.update(executed)
        self.edges.update(edges)
        return True

    def finish(self, rom):
        """The family's space: walked from every start a variant ran."""
        contents = {}
        for member in self.members:
            contents.setdefault(bytes(_unpack(member)[self.low:self.high]), 0)
        chosen, found = next(iter(contents)), None
        for data in contents:
            at = rom.find(data)
            if at >= 0 and rom.find(data, at + 1) < 0:
                chosen, found = data, at
                break
        base = self.origin + self.low
        space = Space("%s-ram-%06X-%s" % (self.cpu, base, sha256(chosen)[:12]),
                      self.cpu, "ram", base, chosen)
        ran = {self.origin + offset for offset in self.starts}
        space.walk(sorted(ran, reverse=True), "traced")
        _downgrade_untraced(space, ran)
        for source, target in sorted(self.edges):
            if source in space.unresolved:
                space.unresolved[source].add(target)
                if space.contains(target):
                    space.add_entry(target, "computed", "traced")
        space.source_rom_offset = found
        space.occurrences = len(self.members)
        space.variant_sha256 = sorted(sha256(data) for data in contents)
        space.patched = sorted({index for data in contents
                                for index, (mine, theirs) in enumerate(zip(chosen, data))
                                if mine != theirs})
        return space


class _Ram:
    """One CPU's RAM as a trace's writes leave it, and the program
    generations its executed instructions belong to."""

    def __init__(self, cpu, size, origin, on_generation):
        self.cpu = cpu
        self.size = size
        self.origin = origin          # CPU address of offset 0
        self.bytes = bytearray(size)
        self.code = bytearray(size)   # 1 where the open generation executed
        self.executed = {}            # offset -> length, the open generation
        self.edges = set()            # (from, to) flow edges seen in it
        self.on_generation = on_generation  # called with each generation as it closes

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
            # handed on now, in closing order, so a generation's maps are not all held to the end
            if self.on_generation:
                self.on_generation(self.cpu, bytes(self.bytes), self.executed, self.edges)
            self.executed = {}
            self.edges = set()
            self.code = bytearray(self.size)


class Ingest:
    """What a trace's events reduce to for the code map: the ROM addresses
    the 68000 executed, the ROM flow edges, the ROM reads, and each CPU's RAM
    program generations, handed to `on_generation(cpu, snapshot, executed,
    edges)` in the order they close. Nothing else of an event is used."""

    def __init__(self, rom_size, on_generation=None):
        self.rom_size = rom_size
        self.on_generation = on_generation
        self.traced_rom = set()
        self.rom_edges = set()
        self.reads = set()
        self.rams = {"m68k": _Ram("m68k", WORK_RAM_SIZE, 0xFF0000, on_generation),
                     "z80": _Ram("z80", Z80_RAM_SIZE, 0, on_generation)}

    def begin(self):
        """A new trace starts from power-on."""
        for ram in self.rams.values():
            ram.close()
            ram.bytes = bytearray(ram.size)

    def end(self):
        for ram in self.rams.values():
            ram.close()

    def merge(self, traced_rom, rom_edges, reads):
        self.traced_rom.update(traced_rom)
        self.rom_edges.update(rom_edges)
        self.reads.update(reads)

    def instruction(self, cpu, pc):
        if cpu == "m68k":
            pc &= 0xFFFFFF
            if pc < self.rom_size:
                self.traced_rom.add(pc)
            elif pc >= 0xE00000:
                self.rams["m68k"].execute(pc)
        elif pc < Z80_RAM_SIZE:
            self.rams["z80"].execute(pc)

    def flow(self, cpu, previous_pc, current_pc):
        if cpu == "m68k" and (previous_pc & 0xFFFFFF) < self.rom_size:
            self.rom_edges.add((previous_pc & 0xFFFFFF, current_pc & 0xFFFFFF))
        elif cpu == "m68k":
            self.rams["m68k"].edges.add((previous_pc | 0xFF0000, current_pc & 0xFFFFFF))
        else:
            self.rams["z80"].edges.add((previous_pc, current_pc))

    def bus(self, cpu, address, width, value, operation, provenance):
        if operation == 2:
            for index, byte in enumerate(value.to_bytes(4, "big")[4 - width:]):
                target = address + index
                if cpu == "z80":
                    if target < Z80_RAM_SIZE:
                        self.rams["z80"].write(target, byte)
                elif 0xA00000 <= (target & 0xFFFFFF) < 0xA00000 + Z80_RAM_SIZE:
                    self.rams["z80"].write(target & 0x1FFF, byte)
                elif (target & 0xFFFFFF) >= 0xE00000:
                    self.rams["m68k"].write(target & 0xFFFF, byte)
        elif cpu == "m68k" and provenance != 1 and (address & 0xFFFFFF) < self.rom_size:
            self.reads.add((address & 0xFFFFFF, width))

    def feed(self, event):
        """One NMDH event; kinds other than instruction, flow and bus are ignored."""
        cpu = "m68k" if event["cpu"] == 1 else "z80"
        kind = event["kind"]
        if kind == 1:
            self.instruction(cpu, event["pc"])
        elif kind == 2:
            payload = event["payload"]
            self.flow(cpu, payload["previous_pc"], payload["current_pc"])
        elif kind == 3:
            payload = event["payload"]
            self.bus(cpu, payload["address"], payload["width"], payload["value"],
                     payload["operation"], payload["provenance"])


def load_evidence(capture_directory, rom_sha256):
    """The capture's evidence, after its manifest proves it was made from
    this ROM and completed: its NMDH events, or for a light capture the NMDE
    digest they reduce to."""
    import pathlib
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
    if manifest.get("evidence") == "light":
        return _DigestEvidence(directory / "capture.nmde", rom_sha256)
    if "window" in manifest:
        # a window's RAM does not start from power-on
        raise CodeMapError("a window is not evidence for a code map")
    return _Evidence(directory / "capture.nmdh")


class _DigestEvidence:
    """A light capture's digest: what Ingest made of its events when they
    were emitted. The file is fully validated before any of it is used."""

    def __init__(self, path, rom_sha256):
        self.path = path
        self.rom_sha256 = rom_sha256

    def replay(self, ingest):
        import evidence
        import trace as nmdh_trace
        if evidence.RAM_SIZES != {"m68k": WORK_RAM_SIZE, "z80": Z80_RAM_SIZE}:
            raise CodeMapError("digest RAM sizes differ from the code map's")
        try:
            digest = evidence.read_digest(self.path)
            if digest.metadata["identities"]["rom_sha256"] != self.rom_sha256:
                raise CodeMapError("digest was made from a different ROM")
            ingest.begin()
            ingest.merge(digest.traced_rom, digest.rom_edges, digest.reads)
            for generation in digest.generations():
                ingest.on_generation(*generation)
        except (OSError, evidence.DigestError, nmdh_trace.NMDHError) as exc:
            raise CodeMapError("digest unreadable: %s" % exc)


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

    def replay(self, ingest):
        ingest.begin()
        for event in self:
            ingest.feed(event)
        ingest.end()


def build(rom, evidence_sets=()):
    """The code map of `rom` from its vectors and any traces' events."""
    rom = bytes(rom)
    rom_space = Space("rom", "m68k", "rom", 0, rom)
    if len(rom) < 0x200:
        raise CodeMapError("image is shorter than a vector table and header")
    rom_space.mark(0, 0x100, STRUCTURE)
    if rom[0x100:0x104] == b"SEGA":
        rom_space.mark(0x100, 0x100, STRUCTURE)
    families = {"m68k": [], "z80": []}    # program families, in the order generations closed

    def admit(cpu, snapshot, executed, edges):
        generation = (snapshot, executed, edges)
        if not any(family.admit(generation) for family in families[cpu]):
            families[cpu].append(_Family(cpu, 0xFF0000 if cpu == "m68k" else 0, generation))

    ingest = Ingest(len(rom), admit)
    for evidence in evidence_sets:
        evidence.replay(ingest)
    traced_rom, rom_edges, reads = ingest.traced_rom, ingest.rom_edges, ingest.reads

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
    for cpu in ("m68k", "z80"):
        spaces.extend(family.finish(rom) for family in families[cpu])
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
