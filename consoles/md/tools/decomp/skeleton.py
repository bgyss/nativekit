"""The lossless assembly skeleton and its rebuild gate.

Every byte of the image is placed explicitly. A code map instruction becomes
an assembler line only if `asmverify` showed the pinned vasm encodes it back
to the same bytes; otherwise its bytes are included raw and counted as raw.
Bytes that are not code are `incbin`ned from the external baserom, which is
never copied into the skeleton. A RAM program with a verbatim source in the
ROM is assembled on its own and included where it was uploaded from, so the
image gate depends on its decoded code too.

`rebuild` assembles the skeleton against a baserom whose SHA-256 must equal
the manifest's, then compares length, SHA-256 and every byte.
"""
import json
import pathlib
import shutil
import subprocess

import asmverify
import codemap

MANIFEST = "manifest.json"
BASEROM = "baserom.bin"
COMMENT = {"m68k": ";", "z80": ";"}
BYTES = {"m68k": "dc.b", "z80": "db"}


class RebuildError(RuntimeError):
    """The rebuild gate could not run or did not hold."""


def verify_space(space, assembler_directory=None):
    """{address: True/False}: whether each instruction with text reassembles."""
    addresses = [address for address in sorted(space.instructions)
                 if space.instructions[address]["text"] is not None]
    items = [(address, space.data[address - space.base:
                                  address - space.base + space.instructions[address]["length"]],
              space.instructions[address]["text"]) for address in addresses]
    verdicts = asmverify.verify(space.cpu, items, assembler_directory)
    return dict(zip(addresses, verdicts))


def _raw_reason(record, verified, conflicted):
    if conflicted:
        return "overlapping interpretations"
    if record["text"] is None:
        return "no assembler form"
    if not verified:
        return "noncanonical encoding"
    return None


def emit(space, verified, includes=(), raw_only=False):
    """(assembly text, per-byte states) for one space. `includes` are
    (offset, length, file name) ranges taken from separately built programs.
    States: 'code' (assembled from text), 'raw-code', 'include', 'baserom'."""
    comment = COMMENT[space.cpu]
    lines = ["%s space %s: %s, %d bytes at $%X, sha256 %s"
             % (comment, space.id, space.cpu, len(space.data), space.base, space.sha256()),
             "\torg\t$%X" % space.base]
    states = ["baserom"] * len(space.data)
    conflicted = space.conflicted_bytes()
    include_at = {offset: (length, name) for offset, length, name in includes}
    covered = set()
    for offset, length, _ in includes:
        covered.update(range(offset, offset + length))
    source_base = 0 if space.kind == "rom" else space.source_rom_offset

    def raw(start, length, note):
        if source_base is not None:
            lines.append('\tincbin\t"%s",%d,%d\t%s %s'
                         % (BASEROM, source_base + start, length, comment, note))
        else:
            for chunk in range(start, start + length, 16):
                data = space.data[chunk:min(chunk + 16, start + length)]
                lines.append("\t%s\t%s\t%s %s" % (BYTES[space.cpu],
                                                  ",".join("$%02X" % value for value in data),
                                                  comment, note))

    offset = 0
    size = len(space.data)
    while offset < size:
        address = space.base + offset
        if raw_only:
            raw(0, size, "raw preservation only")
            break
        if offset in include_at:
            length, name = include_at[offset]
            lines.append('\tincbin\t"%s"\t%s uploaded program, %d bytes' % (name, comment, length))
            for index in range(offset, offset + length):
                states[index] = "include"
            offset += length
            continue
        record = space.instructions.get(address)
        if record is not None and offset not in covered:
            reason = _raw_reason(record, verified.get(address, False), offset in conflicted)
            if address in space.entries:
                lines.append("loc_%06X:" % address)
            if reason is None:
                lines.append("\t" + record["text"])
                state = "code"
            else:
                raw(offset, record["length"], "raw code: " + reason)
                state = "raw-code"
            for index in range(offset, offset + record["length"]):
                states[index] = state
            offset += record["length"]
            continue
        code_class = space.classes[offset] >= codemap.OPERAND
        end = offset + 1
        while end < size and end not in include_at and (
                end in covered or space.base + end not in space.instructions) and (
                    (space.classes[end] >= codemap.OPERAND) == code_class):
            end += 1
        raw(offset, end - offset,
            "raw code: overlapping interpretations" if code_class else "not code")
        if code_class:
            for index in range(offset, end):
                states[index] = "raw-code"
        offset = end
    return "\n".join(lines) + "\n", states


def write(directory, rom, spaces, neutral_id=None, raw_only=False, assembler_directory=None):
    """Write the skeleton files and manifest for `rom`; return the manifest."""
    directory = pathlib.Path(directory)
    asm = directory / "asm"
    asm.mkdir(parents=True, exist_ok=True)
    rom_space = spaces[0]
    includes = []
    programs = []
    per_space = {}
    for space in spaces[1:]:
        verified = verify_space(space, assembler_directory)
        text, states = emit(space, verified)
        name = space.id + ".s"
        (asm / name).write_text(text)
        entry = {"id": space.id, "cpu": space.cpu, "source": name, "base": space.base,
                 "size": len(space.data), "sha256": space.sha256(),
                 "source_rom_offset": space.source_rom_offset, "included_in_image": False}
        offset = space.source_rom_offset
        if offset is not None and not raw_only and not any(
                rom_space.classes[index] >= codemap.OPERAND
                for index in range(offset, offset + len(space.data))) and not any(
                    offset < other + length and other < offset + len(space.data)
                    for other, length, _ in includes):
            includes.append((offset, len(space.data), space.id + ".bin"))
            entry["included_in_image"] = True
        programs.append(entry)
        per_space[space.id] = states
    verified = {} if raw_only else verify_space(rom_space, assembler_directory)
    text, states = emit(rom_space, verified, includes, raw_only)
    (asm / "rom.s").write_text(text)
    per_space["rom"] = states
    manifest = {
        "schema": "nmd-skeleton", "version": 1, "game": neutral_id,
        "mode": "raw preservation only" if raw_only else "decoded skeleton",
        "rom": {"sha256": codemap.sha256(rom), "size": len(rom)},
        "image_source": "rom.s", "programs": programs,
    }
    (directory / MANIFEST).write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")
    return manifest, per_space


def _assemble(cpu, source, output, cwd, assembler_directory):
    executable, options = asmverify.ASSEMBLERS[cpu]
    result = subprocess.run(
        [str(asmverify.assembler_path(cpu, assembler_directory)), "-Fbin", *options, "-quiet",
         "-o", output, source], cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True, text=True)
    if result.returncode or not (pathlib.Path(cwd) / output).is_file():
        raise RebuildError("%s did not assemble: %s" % (source, result.stderr.strip()[:400]))
    return (pathlib.Path(cwd) / output).read_bytes()


def rebuild(directory, baserom_path, assembler_directory=None):
    """Assemble the skeleton in `directory` against the baserom and compare.
    Returns the report; raises RebuildError when any gate does not hold."""
    directory = pathlib.Path(directory)
    try:
        manifest = json.loads((directory / MANIFEST).read_text())
    except (OSError, ValueError) as exc:
        raise RebuildError("manifest unreadable: %s" % exc)
    if manifest.get("schema") != "nmd-skeleton" or manifest.get("version") != 1:
        raise RebuildError("manifest is not an nmd-skeleton version 1")
    try:
        original = pathlib.Path(baserom_path).read_bytes()
    except OSError as exc:
        raise RebuildError("baserom unreadable: %s" % exc.strerror)
    if codemap.sha256(original) != manifest["rom"]["sha256"]:
        raise RebuildError("baserom is not the image this skeleton was made from")
    work = directory / "build"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    (work / BASEROM).write_bytes(original)
    try:
        for name in sorted(path.name for path in (directory / "asm").iterdir()):
            shutil.copyfile(directory / "asm" / name, work / name)
        for program in manifest["programs"]:
            built = _assemble(program["cpu"], program["source"], program["id"] + ".bin",
                              work, assembler_directory)
            if codemap.sha256(built) != program["sha256"] or len(built) != program["size"]:
                raise RebuildError("program %s does not rebuild to its captured bytes"
                                   % program["id"])
        image = _assemble("m68k", manifest["image_source"], "image.bin", work,
                          assembler_directory)
    finally:
        shutil.rmtree(work, ignore_errors=True)   # no ROM bytes are left in the workspace
    differing = [index for index in range(min(len(image), len(original)))
                 if image[index] != original[index]]
    report = {
        "mode": manifest["mode"], "rom_sha256": manifest["rom"]["sha256"],
        "length": len(image), "original_length": len(original),
        "sha256": codemap.sha256(image), "differing_bytes": len(differing),
        "first_difference": differing[0] if differing else None,
        "programs_rebuilt": len(manifest["programs"]),
        "identical": not differing and len(image) == len(original),
    }
    if not report["identical"]:
        raise RebuildError("image differs: %d bytes, first at $%X; length %d of %d" % (
            len(differing), differing[0] if differing else min(len(image), len(original)),
            len(image), len(original)))
    return report
