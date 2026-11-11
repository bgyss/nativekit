# Lossless intake, code map and census (D03)

The shared tooling that turns a Mega Drive image and reference captures into
an auditable byte map and an assembly skeleton that rebuilds the image
exactly. Its gates are held by authored synthetic cartridges; the first title
has been run through it once, with the [results and findings below](#first-title).

## Commands

From the repository root, after `mise run tools:md`:

```sh
python3 consoles/md/tools/decomp/intake.py intake \
    --rom ROM --out build/decomp/<game-id> [--capture CAPTURE_DIR]... [--id <game-id>] [--raw]
python3 consoles/md/tools/decomp/intake.py rebuild --rom ROM --dir build/decomp/<game-id>
python3 consoles/md/tools/decomp/intake.py census  --dir build/decomp/<game-id>
```

`CAPTURE_DIR` is the `--out` of a completed `consoles/md/tools/decomp/run.py
--mode capture` whose scenario requires `bus_intervals` and `flow_edge`.
`intake` exits non-zero unless the rebuilt image equals `ROM` in length,
SHA-256 and every byte. The ROM's path and bytes are never written to the
output: files name it by SHA-256 and `--id`. A decoded skeleton's `asm/` holds
recovered code text, so the output belongs under ignored `build/decomp/`,
never in nativekit.

| File | Content |
|---|---|
| `codemap.json` | schema `nmd-codemap` 1: spaces, per-byte class runs, instructions with evidence, entries, conflicts, unresolved transfers, orphan candidates |
| `manifest.json` | schema `nmd-skeleton` 1: mode, image SHA-256 and size, program list with expected SHA-256 |
| `asm/rom.s`, `asm/<program>.s` | the skeleton |
| `census.json`, `catalog.json` | schemas `nmd-census` 1 and `nmd-catalog` 1 |

## Decoders and what their controls claim

`m68k_dis.py` (base 68000, no line-A/F, no 68010+) and `z80_dis.py`
(unprefixed, CB, ED, DD, FD, DDCB, FDCB) print the pinned vasm's syntax and
report length, flow kind, direct targets and whether a transfer is computed.

A decode is a proposal. `asmverify.py` assembles every proposed line at its
own address in one vasm run (68000: `-m68000 -no-opt`) and accepts a line only
when the listing shows the original bytes. Anything else stays raw.

| Control | Result at this commit |
|---|---|
| 68000: all 65,536 first words, extension set A (canonical operands) | 45,800 decode; 45,800 reassemble |
| 68000: same, set B (byte immediate with a non-zero high byte; brief extension word with a reserved bit) | 44,651 reassemble; 1,149 non-reassembling, all of those two families |
| Z80: 1,792 opcode positions, two operand fills | 1,269 have text; 801 reassemble; 468 non-reassembling, all redundant DD/FD prefixes, duplicate ED encodings or indexed BIT with a register field; 523 have no assembler spelling |
| Flow facts on an authored program per CPU | 13 and 14 instructions: length, kind, targets, computed |
| Planted decoder errors (length, target, mnemonic) | all caught, per CPU |

Limits: the sweeps fix the extension words, so they do not cover every
operand value; a word the 68000 decoder calls illegal is not checked against
hardware (it stays unknown, the safe direction; an executed address that does
not decode is reported as `undecodable`). The pinned vasm mirror's equivalence
to an author-published archive remains unverified (`tools/toolchains/README.md`).

## The code map

A **space** is bytes one CPU executes from: the ROM image (file offset *n* is
68000 address *n* in the base cartridge map; offsets and CPU addresses are
separate fields everywhere) or one **generation** of a RAM program, identified
by CPU, load address and the SHA-256 of its bytes. Generations are rebuilt by
replaying a capture's bus writes: a write that changes a byte of code the
current generation executed closes it, and the next executed instruction opens
another. Its extent is the first to the last executed byte. Two generations
merge only when address and bytes are both equal, so data bytes inside the
extent that differ make two identities (a false split, never a false merge).
A generation whose bytes occur exactly once in the ROM records that
`source_rom_offset`; otherwise it has none (generated, patched or decompressed).

Per-byte class: `opcode`, `operand`, `structure` (vector table, header), `read`
(a traced CPU data read; a checksum loop reads unexecuted code too, so this is
not "data"), `unknown`. Instruction evidence: `traced`, `vector` (reached from
a vector without running), `static` (reached only by following code).
Overlapping instructions are recorded as conflicts and kept raw. Computed
transfers are recorded as unresolved, with the targets a trace observed.
`orphan_candidates` are unclaimed bytes after a non-returning instruction that
decode to another one: a list to investigate, with no classification.

## Skeleton, rebuild gate and credit

`asm/rom.s` places every byte: verified instruction lines, `loc_XXXXXX:`
labels at entries, and `incbin "baserom.bin",offset,length` for everything
else, including code that is raw (each such line says why: noncanonical
encoding, no assembler form, overlapping interpretations). A RAM program with
a verbatim source is assembled separately and included where it was uploaded
from, so the image gate depends on its decoded code; a program without one is
held to its captured SHA-256 only and its compressed source stays baserom.
`rebuild` refuses a baserom whose hash is not the manifest's, builds in a
temporary directory it removes, and reports length, SHA-256 and differing bytes.

Credit rules in `census.json`: `decoded_code_bytes` counts only bytes
assembled from verified text in a skeleton that rebuilt identically, split by
evidence; `raw_code_bytes`, `included_program_bytes`, `baserom_bytes` and
`unknown_bytes` are reported separately and earn none. `--raw` emits one
`incbin`, is labelled `raw preservation only`, and credits nothing. A catalog
entry is **T1** only when a trace ran it, the skeleton labels it and the image
rebuilt identically; every other entry has no tier.

## Evidence

`mise run test:md-decomp` (exit 0, 165 s) now also runs
`m68k_decoder_check.py`, `z80_decoder_check.py` and `census_check.py`. The
census control captures an authored cartridge on the pinned reference core
(dead code, a three-entry computed call table with one entry taken, a
vector-only handler, decoy bytes, and two different Z80 programs uploaded to
`$0000`, one verbatim and one from an inverted copy) and requires: both
generations kept apart with the right source facts; dead code, untaken entries
and decoys not classified as code; the computed call unresolved with its one
observed target; an identical rebuild; and seven planted errors caught
(changed 68000 instruction, changed Z80 instruction, changed data byte, changed
placement, different source ROM, capture of a different ROM, missing capture).
A second control with no trace holds an overlap conflict and a noncanonical
instruction raw.

## First title

`md-f68818ae1a61`, SHA-256 `f68818ae1a616562e4c7dc77932bbc9f6bc4e42229f5b5d5796d4b2c2cc3f334`,
524,288 bytes, run on 2026-11-10 with the ROM path given at runtime. All output
is under ignored `build/decomp/md-f68818ae1a61/`, whose `WORKSPACE.md` holds
the exact commands; nothing from the image is tracked.

Evidence: one complete 250-frame capture on the pinned reference core (region
U, model `md1va3`, two three-button pads, START on frames 180-187 and
240-247; scenario SHA-256 `c70855a3…fa79e`; 16,202,726 events, 5.4 GB). Its
caps were raised from the 256 MB default to 16 GiB and 560 s. The intake read
it in 294 s with a 120 MB peak.

| Result | Value |
|---|---|
| Rebuild from `asm/` and the external baserom | identical: 524,288 bytes, same SHA-256, 0 differing bytes; also identical with no capture |
| ROM bytes assembled from verified text | 5,408 traced, 100 vector, 14,060 static |
| ROM code kept raw (noncanonical encoding) | 20 bytes |
| ROM bytes from separately assembled uploaded programs | 340 (two Z80 programs with a verbatim source) |
| ROM bytes from the baserom | 504,360, of which 512 structure and the rest `read` |
| ROM entries | 168: 71 traced (T1), 4 vector, 93 static |
| Computed transfers in ROM | 14, of which 9 have no observed target |
| Conflicts, undecodable entries | 0, 0 |
| Orphan candidates | 18 |
| Z80 RAM program identities | 506, all at `$0000`; no 68000 RAM code ran |

Findings, none of them fixed here:

- **This is one second of boot and about three seconds after it.** 19,568 of
  524,288 bytes are decoded, and 72% of those only by static reach. The
  census says nothing about the game's main code yet.
- **A longer capture did not complete.** A 900-frame run ended
  `incomplete_core_error`: the reference core's NMDT stream stopped near frame
  258, about 17 frames after the first YM2612 write. The cause is not
  diagnosed (the stream's per-device clock ordering rule is the suspect). The
  intake refuses incomplete captures, so traced evidence currently ends at
  frame 250.
- **503 of the 506 Z80 identities are one 254-byte program** whose two 16-bit
  immediate operands the 68000 rewrites between runs. Each rewrite is a new
  generation by the "address and bytes" rule, so the catalog holds 503 T1
  entries for one routine. The rule's false split is real at title scale;
  identities need an operand-patch notion before the catalog count means
  routines.
- **Every non-code ROM byte is `read`** because the boot checksum reads the
  whole image; the class separates nothing for this title.
- One 40-byte Z80 generation has 4 traced bytes, 36 unknown and no ROM source.

## Not done

- Diagnosing the reference core's NMDT failure, and any capture past frame 250.
- Collapsing operand-patched generations of one program into one identity.
- Compressed program sources are not reconstructed, only identified as absent.
- No static discovery of jump tables; computed targets come from traces only.
- 68000 RAM generations are implemented but exercised only by D01's fixture
  shape, not by a dedicated control here.
- Captures are streamed from disk (one validation pass, one read pass), but an
  event costs about 330 bytes on disk: 21.6 MB per frame for this title.
