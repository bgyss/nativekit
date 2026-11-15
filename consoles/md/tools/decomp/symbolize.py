#!/usr/bin/env python3
"""Symbolic control flow for a decoded skeleton (D08, first step).

    symbolize.py --skeleton DIR --codemap codemap.json --out DIR [--names names.json]
                 [--baserom ROM]

Reads the skeleton's listings (skeleton.py) and writes a copy in which every
direct branch, call and jump whose target is assembled as code names its
target by a label, and every such target carries one: `sub_XXXXXX` at a
routine entry, `loc_XXXXXX` elsewhere, or the name `--names` gives the
address ({"$5D2A": "draw_text"}). A PC-relative operand that addresses a
raw span of the image names a `dat_XXXXXX` label, and the span's incbin is
cut there. An absolute operand is left as written: it may be a number.
With --baserom the copy is rebuilt (skeleton.rebuild) and must be the image
byte for byte; -no-opt keeps every branch at its written size.

The listing's addresses are recovered by walking it: `org` sets the address,
an incbin or byte line advances it by its length, and an instruction line
must be the code map's instruction at the running address (its text) and
advances by that instruction's length. A line that does not line up is an
error, not a guess.
"""
import argparse
import json
import pathlib
import re
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import skeleton  # noqa: E402

LABEL = re.compile(r"^(loc|sub|dat)_[0-9A-F]{6}:$")
# a PC-relative operand, ($454,pc) or ($5F9E,pc,d0.w): it can only address
# the image. An absolute one, ($B000).l, may be a number (a VRAM address) and
# is left as written.
MEMORY = re.compile(r"\(\$([0-9A-F]+)(?=,pc)")
BYTES_LINE = re.compile(r"^\t(?:dc\.b|db)\t([^\t;]*)")
INCBIN = re.compile(r'^\tincbin\t"([^"]+)"(?:,(\d+),(\d+))?')
DIRECT = ("jump", "cond", "call")


class SymbolizeError(RuntimeError):
    pass


def _walk(lines, instructions, program_sizes):
    """[(line index, address or None, kind, length)] for every line; kind is
    'code', 'label', 'raw' or 'other'."""
    out = []
    address = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith((";", "*")):
            out.append((index, address, "other", 0))
            continue
        if line.startswith("\torg\t$"):
            address = int(line.split("$")[1], 16)
            out.append((index, address, "other", 0))
            continue
        if LABEL.match(line):
            out.append((index, address, "label", 0))
            continue
        match = INCBIN.match(line)
        if match:
            name, _, length = match.groups()
            length = int(length) if length is not None else program_sizes.get(name)
            if length is None:
                raise SymbolizeError("line %d: an incbin of unknown length" % (index + 1))
            out.append((index, address, "raw", length))
            address += length
            continue
        match = BYTES_LINE.match(line)
        if match:
            length = len(match.group(1).split(","))
            out.append((index, address, "raw", length))
            address += length
            continue
        record = instructions.get(address)
        text = line[1:].split("\t")[0]
        if record is None or record["text"] != text:
            raise SymbolizeError("line %d: %r is not the code map's instruction at $%X"
                                 % (index + 1, text, address or 0))
        out.append((index, address, "code", record["length"]))
        address += record["length"]
    return out


def _split(line, start, cuts, label):
    """An incbin of the image's own bytes, cut at each address in `cuts` with
    that address's label between the pieces; None when the line is not one."""
    match = INCBIN.match(line)
    if not match or match.group(2) is None:
        return None
    name, offset, length = match.group(1), int(match.group(2)), int(match.group(3))
    rest = line[match.end():]
    pieces, at = [], start
    for cut in list(cuts) + [start + length]:
        if cut > at:
            pieces.append('\tincbin\t"%s",%d,%d%s' % (name, offset + at - start, cut - at, rest))
        if cut < start + length:
            pieces.append(label(cut) + ":")
        at = cut
    return pieces


def symbolize_text(text, space, names=None):
    """(new text, labels placed, operands rewritten) for one listing."""
    names = names or {}
    instructions = {i["at"]: i for i in space["instructions"]}
    entries = {e["at"] for e in space.get("entries", ())}
    lines = text.split("\n")
    walked = _walk(lines, instructions, space.get("_program_sizes", {}))
    coded = {address for _, address, kind, _ in walked if kind == "code"}
    # the raw spans a label can cut: incbins of the image's own bytes
    cuttable = [(address, address + length) for index, address, kind, length in walked
                if kind == "raw" and INCBIN.match(lines[index]) and
                INCBIN.match(lines[index]).group(2) is not None]

    def in_raw(address):
        return any(start <= address < end for start, end in cuttable)

    targets, data = set(), set()
    for index, address, kind, _ in walked:
        if kind != "code":
            continue
        record = instructions[address]
        if record["flow"] in DIRECT and not record["indirect"]:
            targets.update(t for t in record["targets"] if t in coded)
        for found in MEMORY.findall(lines[index].partition("\t;")[0]):
            target = int(found, 16)
            if target in coded:
                targets.add(target)
            elif in_raw(target):
                data.add(target)

    def label(address):
        if address in names:
            return names[address]
        if address in data:
            return "dat_%06X" % address
        return ("sub_%06X" if address in entries else "loc_%06X") % address

    out, placed, rewritten = [], 0, 0
    for index, address, kind, length in walked:
        line = lines[index]
        if kind == "label":
            continue                    # placed again below, by address
        if kind == "code" and (address in targets or address in entries):
            out.append(label(address) + ":")
            placed += 1
        if kind == "raw":
            cuts = sorted(t for t in data if address <= t < address + length)
            pieces = _split(line, address, cuts, label) if cuts else None
            if pieces:
                out.extend(pieces)
                placed += len(cuts)
                continue
        if kind == "code":
            record = instructions[address]
            body, sep, rest = line.partition("\t;")
            named = set()
            if record["flow"] in DIRECT and not record["indirect"]:
                named.update(t for t in record["targets"] if t in coded)
            named.update(t for t in (int(found, 16) for found in MEMORY.findall(body))
                         if t in targets or t in data)
            for target in sorted(named):
                new = re.sub(r"\$%X\b" % target, label(target), body, count=1)
                if new != body:
                    body = new
                    rewritten += 1
            line = body + sep + rest
        out.append(line)
    return "\n".join(out), placed, rewritten


def symbolize(skeleton_dir, codemap, out_dir, names=None):
    """Write the symbolic copy; return {listing: (labels, operands)}."""
    skeleton_dir, out_dir = pathlib.Path(skeleton_dir), pathlib.Path(out_dir)
    manifest = json.loads((skeleton_dir / skeleton.MANIFEST).read_text())
    spaces = {space["id"]: space for space in codemap["spaces"]}
    sizes = {program["id"] + ".bin": program["size"] for program in manifest["programs"]}
    (out_dir / "asm").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(skeleton_dir / skeleton.MANIFEST, out_dir / skeleton.MANIFEST)
    report = {}
    sources = [("rom", manifest["image_source"])] + [
        (program["id"], program["source"]) for program in manifest["programs"]]
    rom_space = codemap["spaces"][0]
    for space_id, source in sources:
        space = dict(rom_space if space_id == "rom" else spaces[space_id])
        space["_program_sizes"] = sizes
        text = (skeleton_dir / "asm" / source).read_text()
        new, placed, rewritten = symbolize_text(text, space, names if space_id == "rom" else None)
        (out_dir / "asm" / source).write_text(new)
        report[source] = {"labels": placed, "operands": rewritten}
    for path in (skeleton_dir / "asm").iterdir():   # anything else the listings include
        if not (out_dir / "asm" / path.name).exists():
            shutil.copyfile(path, out_dir / "asm" / path.name)
    return report


def load_names(path):
    """{"$5D2A": "draw_text"} -> {0x5D2A: "draw_text"}."""
    raw = json.loads(pathlib.Path(path).read_text())
    return {int(key.lstrip("$"), 16): value for key, value in raw.items()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--skeleton", required=True)
    parser.add_argument("--codemap", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--names")
    parser.add_argument("--baserom", help="rebuild the copy against this image and compare")
    args = parser.parse_args(argv)
    try:
        codemap = json.loads(pathlib.Path(args.codemap).read_text())
        report = symbolize(args.skeleton, codemap, args.out,
                           load_names(args.names) if args.names else None)
        for source, counts in report.items():
            print("symbolize: %s: %d labels, %d operands named"
                  % (source, counts["labels"], counts["operands"]))
        if args.baserom:
            rebuilt = skeleton.rebuild(args.out, args.baserom)
            print("symbolize: rebuild %s (%d bytes differ)"
                  % ("identical" if rebuilt["identical"] else "NOT identical",
                     rebuilt["differing_bytes"]))
            if not rebuilt["identical"]:
                return 1
    except (SymbolizeError, skeleton.RebuildError, OSError, ValueError) as exc:
        print("symbolize: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
