"""Hold decoded text to the assembler: a line counts only if the pinned vasm
encodes it back to the original bytes at the original address.

One assembler run takes every line, each at its own `org`. The listing gives
each source line's bytes (and its error, if the assembler refused it), so a
bad line never hides the others.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
ASSEMBLERS = {
    "m68k": ("vasmm68k_mot", ["-m68000", "-no-opt"]),
    "z80": ("vasmz80_oldstyle", []),
}
_START = re.compile(r"^[0-9A-F]{2,}:[0-9A-F]{4,8} ([0-9A-F]+)\s*\t\s*(\d+):")
_MORE = re.compile(r"^[0-9A-F]{2,}:[0-9A-F]{4,8} ([0-9A-F]+)\s*$")
_BLANK = re.compile(r"^\s+\t\s*(\d+):")


class AssemblerError(RuntimeError):
    """The assembler is missing or produced no usable listing."""


def assembler_path(cpu, directory=None):
    name = ASSEMBLERS[cpu][0]
    path = pathlib.Path(directory) / name if directory else ROOT / "build/toolchains/vasm" / name
    if not path.is_file():
        raise AssemblerError("missing assembler " + name)
    return path


def listing_bytes(listing_text):
    """{source line number: (bytes, errored)} from a vasm listing."""
    lines = {}
    current = None
    for row in listing_text.splitlines():
        if row.startswith("Symbols by name") or row.startswith("Symbols by value"):
            break
        match = _START.match(row)
        if match:
            current = int(match.group(2))
            lines[current] = [bytearray.fromhex(match.group(1)), False]
            continue
        match = _MORE.match(row)
        if match and current is not None:
            lines[current][0] += bytes.fromhex(match.group(1))
            continue
        match = _BLANK.match(row)
        if match:
            current = int(match.group(1))
            lines.setdefault(current, [bytearray(), False])
            continue
        if "^-ERROR" in row and current is not None:
            lines[current][1] = True
    return {number: (bytes(data), errored) for number, (data, errored) in lines.items()}


def verify(cpu, items, assembler_directory=None):
    """items: (address, original bytes, text). A list of booleans: whether the
    assembler reproduces each item's bytes from its text at its address."""
    items = list(items)
    if not items:
        return []
    assembler = assembler_path(cpu, assembler_directory)
    source = []
    for address, _, text in items:
        if "\n" in text or "\r" in text:
            raise AssemblerError("instruction text spans lines")
        source.append("\torg\t$%X" % address)
        source.append("\t" + text)
    with tempfile.TemporaryDirectory(prefix="nmd-asmverify-") as directory:
        directory = pathlib.Path(directory)
        (directory / "v.s").write_text("\n".join(source) + "\n")
        subprocess.run(
            [str(assembler), "-Fbin", *ASSEMBLERS[cpu][1], "-quiet", "-maxerrors=0",
             "-L", "v.lst", "-o", "v.bin", "v.s"],
            cwd=directory, stdin=subprocess.DEVNULL, capture_output=True, text=True)
        listing_path = directory / "v.lst"
        if not listing_path.is_file():
            raise AssemblerError("the assembler wrote no listing")
        lines = listing_bytes(listing_path.read_text(errors="replace"))
    results = []
    for index, (_, original, _) in enumerate(items):
        data, errored = lines.get(2 * index + 2, (b"", True))
        results.append(not errored and data == bytes(original))
    return results
