#!/usr/bin/env python3
"""Lossless intake of a Mega Drive image: code map, skeleton, rebuild gate,
routine catalog and census.

  intake.py intake  --rom ROM --out DIR [--capture DIR]... [--id NAME] [--raw]
  intake.py rebuild --rom ROM --dir DIR
  intake.py census  --dir DIR

`intake` writes DIR/codemap.json, DIR/manifest.json, DIR/asm/*.s,
DIR/catalog.json and DIR/census.json, then runs the rebuild gate; it exits
non-zero unless the rebuilt image equals ROM byte for byte. The ROM's path
and bytes are never written to DIR: files name it by SHA-256 and the neutral
--id only. DIR/asm of a decoded skeleton holds recovered code text, so DIR
belongs under ignored build/decomp/<game-id>/, not in nativekit.
"""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import asmverify  # noqa: E402
import codemap  # noqa: E402
import skeleton  # noqa: E402


def census(rom, spaces, states, manifest, rebuilt):
    """Byte ownership per space. Recovered-code credit is only bytes
    assembled from verified instruction text in a skeleton whose image
    rebuilt identically; everything else is reported, not credited."""
    report = {"schema": "nmd-census", "version": 1, "game": manifest["game"],
              "mode": manifest["mode"], "rom": manifest["rom"],
              "rebuild_identical": bool(rebuilt), "spaces": []}
    for space in spaces:
        classes, by_evidence = space.counts()
        state = states[space.id]
        decoded = dict.fromkeys(codemap.EVIDENCE_RANK, 0)
        for address, record in space.instructions.items():
            offset = address - space.base
            if state[offset] == "code":
                decoded[record["evidence"]] += record["length"]
        program = next((p for p in manifest["programs"] if p["id"] == space.id), None)
        report["spaces"].append({
            "id": space.id, "cpu": space.cpu, "kind": space.kind, "base": space.base,
            "size": len(space.data), "sha256": space.sha256(),
            "source_rom_offset": space.source_rom_offset,
            "included_in_image": bool(program and program["included_in_image"]),
            "occurrences": space.occurrences,
            "variants": len(space.variant_sha256),
            "patched_operand_bytes": len(space.patched),
            "class_bytes": classes,
            "instruction_bytes_by_evidence": by_evidence,
            "decoded_code_bytes": decoded if rebuilt else dict.fromkeys(decoded, 0),
            "raw_code_bytes": state.count("raw-code"),
            "included_program_bytes": state.count("include"),
            "baserom_bytes": state.count("baserom"),
            "unknown_bytes": classes["unknown"],
            "entries": len(space.entries),
            "unresolved_indirect": len(space.unresolved),
            "unresolved_without_observed_target": sum(
                1 for targets in space.unresolved.values() if not targets),
            "conflicts": len(set(space.conflicts)),
            "undecodable_entries": len(space.undecodable),
            "orphan_candidates": len(space.orphan_candidates()),
            "unexecuted_candidate_bytes": by_evidence["static"] + by_evidence["vector"],
        })
    return report


def catalog(spaces, rebuilt):
    """Routine entries. T1 needs a trace that ran the entry, an address
    label in the skeleton, and an identical rebuild; anything else is a
    candidate with no tier."""
    routines = []
    for space in spaces:
        for address, entry in sorted(space.entries.items()):
            record = space.instructions.get(address)
            traced = record is not None and record["evidence"] == "traced"
            routines.append({
                "space": space.id, "cpu": space.cpu, "at": address,
                "label": "loc_%06X" % address, "kinds": sorted(entry["kinds"]),
                "evidence": record["evidence"] if record else "undecodable",
                "tier": "T1" if traced and rebuilt else None,
            })
    return {"schema": "nmd-catalog", "version": 1, "routines": routines}


def run_intake(rom_path, out, captures=(), neutral_id=None, raw_only=False,
               assembler_directory=None):
    """The whole intake; returns (census, catalog, rebuild report)."""
    rom = pathlib.Path(rom_path).read_bytes()
    digest = codemap.sha256(rom)
    evidence = [codemap.load_evidence(capture, digest) for capture in captures]
    spaces = codemap.build(rom, evidence)
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "codemap.json").write_text(
        json.dumps(codemap.document(rom, spaces, neutral_id), separators=(",", ":")) + "\n")
    manifest, states = skeleton.write(out, rom, spaces, neutral_id, raw_only,
                                      assembler_directory)
    error = None
    try:
        rebuilt = skeleton.rebuild(out, rom_path, assembler_directory)
    except skeleton.RebuildError as exc:
        rebuilt, error = None, exc
    counts = census(rom, spaces, states, manifest, rebuilt)
    routines = catalog(spaces, rebuilt)
    (out / "census.json").write_text(json.dumps(counts, indent=1, sort_keys=True) + "\n")
    (out / "catalog.json").write_text(json.dumps(routines, indent=1, sort_keys=True) + "\n")
    if error is not None:
        raise error
    return counts, routines, rebuilt


def summary(counts):
    lines = ["%s: %s, image %d bytes sha256 %s, rebuild %s" % (
        counts["game"] or "unnamed", counts["mode"], counts["rom"]["size"],
        counts["rom"]["sha256"], "identical" if counts["rebuild_identical"] else "NOT identical")]
    for space in counts["spaces"]:
        decoded = space["decoded_code_bytes"]
        lines.append(
            "  %s (%s, %d bytes): decoded code %d traced + %d vector + %d static; raw code %d; "
            "structure %d; read %d; unknown %d; entries %d; unresolved indirect %d; "
            "conflicts %d; orphan candidates %d" % (
                space["id"], space["cpu"], space["size"], decoded["traced"], decoded["vector"],
                decoded["static"], space["raw_code_bytes"], space["class_bytes"]["structure"],
                space["class_bytes"]["read"], space["unknown_bytes"], space["entries"],
                space["unresolved_indirect"], space["conflicts"], space["orphan_candidates"]))
        if space.get("variants", 1) > 1:
            lines[-1] += "; %d variants over %d generations, %d operand bytes rewritten" % (
                space["variants"], space["occurrences"], space["patched_operand_bytes"])
    if counts["mode"] != "decoded skeleton":
        lines.append("  raw preservation only: no recovered-code credit")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    intake = commands.add_parser("intake")
    intake.add_argument("--rom", required=True)
    intake.add_argument("--out", required=True)
    intake.add_argument("--capture", action="append", default=[])
    intake.add_argument("--id")
    intake.add_argument("--raw", action="store_true")
    rebuild = commands.add_parser("rebuild")
    rebuild.add_argument("--rom", required=True)
    rebuild.add_argument("--dir", required=True)
    show = commands.add_parser("census")
    show.add_argument("--dir", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "intake":
            counts, _, _ = run_intake(args.rom, args.out, args.capture, args.id, args.raw)
            print(summary(counts))
        elif args.command == "rebuild":
            report = skeleton.rebuild(args.dir, args.rom)
            print("rebuild: identical, %d bytes, sha256 %s, %d programs (%s)" % (
                report["length"], report["sha256"], report["programs_rebuilt"], report["mode"]))
        else:
            print(summary(json.loads((pathlib.Path(args.dir) / "census.json").read_text())))
    except (OSError, ValueError, skeleton.RebuildError, asmverify.AssemblerError) as exc:
        print("intake: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
