# Lossless intake, code map and census (D03)

The shared tooling that turns a Mega Drive image and reference captures into
an auditable byte map and an assembly skeleton that rebuilds the image
exactly. Its gates are held by authored synthetic cartridges; the first title
has been run through it at 250, 900 and 3,000 frames, with the [results and findings below](#first-title).

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
separate fields everywhere) or one RAM **program**. Generations of RAM code
are rebuilt by replaying a capture's bus writes: a write that changes a byte
of code the current generation executed closes it, and the next executed
instruction opens another. A generation's extent is the first to the last
executed byte.

Two generations are the same program, one **family**, only when their extents
overlap and, over the union of the extents, every byte that differs lies in an
instruction that both decode to the same length, flow, transfer targets and
text apart from its numeric literals. So a program whose immediate operands
another CPU rewrites, or of which one run reached further than another, is one
space with **variants**. A differing opcode, register, branch target or byte
outside any instruction keeps generations apart (data bytes inside the extent
that differ still make a false split, never a false merge). A generation is
tried against the families in the order they first ran and joins the first
that takes it. The space holds one variant's bytes (the one occurring exactly
once in the ROM when there is one, recorded as `source_rom_offset`; otherwise
the first to run), is named by CPU, load address and that variant's SHA-256,
and lists every variant's SHA-256 (`variant_sha256`), the byte runs that vary
(`patched`) and the number of generations (`occurrences`). Its instruction
evidence is the union of what its variants ran. The census adds `variants` and
`patched_operand_bytes`; the skeleton marks each rewritten instruction.

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

`mise run test:md-decomp` (exit 0, 180 s) now also runs
`m68k_decoder_check.py`, `z80_decoder_check.py`, `census_check.py` and
`identity_check.py` (an authored cartridge whose 68000 rewrites a Z80
immediate, then an opcode: the first is merged as a variant, the second kept
apart; five refusals of the rule on bytes; two planted rule errors). The
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
524,288 bytes, run on 2026-11-10 and 2026-11-11 with the ROM path given at runtime. All output
is under ignored `build/decomp/md-f68818ae1a61/`, whose `WORKSPACE.md` holds
the exact commands; nothing from the image is tracked.

Evidence: three complete captures on the pinned reference core (region U, model
`md1va3`, two three-button pads), each taken through its own intake:

- 250 frames, START on frames 180-187 and 240-247; scenario SHA-256
  `c70855a3…fa79e`; 16,202,726 events, 5.4 GB; caps raised from the 256 MB
  default to 16 GiB and 560 s. The intake read it in 294 s with a 120 MB peak.
- 900 frames, an eight-frame START press every 60 frames from 180 to 660 and
  pad inputs from frame 420; scenario SHA-256 `4a434d8b…d4d690`; 61,716,550
  events (8,991,561 68000 and 4,843,565 Z80 instructions), 20.6 GB, 939 s;
  caps 64 GiB and 3,000 s. It needs the core with patch 0003 (below). Its
  intake took 1,111 s with a 1.2 GB peak.
- 3,000 frames, written with the picture in view (below): the options
  screen, then play with no START press; scenario SHA-256
  `12859186…d713c0`; 209,186,292 events (30,790,627 68000 and 17,707,113 Z80
  instructions), 73.5 GB, 2,988 s; caps 320 GiB and 12,000 s. Its intake took
  3,728 s with a 6.2 GB peak.

| Result | 250 frames | 900 frames | 3,000 frames |
|---|---|---|---|
| Rebuild from `asm/` and the external baserom | identical: 524,288 bytes, same SHA-256, 0 differing bytes; also identical with no capture | identical, same three measures | identical, same three measures |
| ROM bytes assembled from verified text | 5,408 traced, 100 vector, 14,060 static | 12,060 traced, 100 vector, 8,148 static | 14,616 traced, 100 vector, 5,988 static |
| ROM code kept raw (noncanonical encoding) | 20 bytes | 20 bytes | 20 bytes |
| ROM bytes from separately assembled uploaded programs | 340 (two Z80 programs with a verbatim source) | 340 | 340 |
| ROM bytes from the baserom | 504,360, of which 512 structure and the rest `read` | 503,620, same split | 503,224, same split |
| ROM entries | 168: 71 traced (T1), 4 vector, 93 static | 222: 189 traced (T1), 4 vector, 29 static | 240: 218 traced (T1), 4 vector, 18 static |
| Computed transfers in ROM | 14, of which 9 have no observed target | 14, of which 5 have none | 14, of which 4 have none |
| Conflicts, undecodable entries | 0, 0 | 0, 0 | 0, 0 |
| Orphan candidates | 18 | 19 | 17 |
| Z80 RAM programs | 3, all at `$0000` (one with 504 variants over 4 rewritten bytes); no 68000 RAM code ran | 3, all at `$0000` (one with 34,776 variants in 45,193 generations over 12 rewritten bytes); none on the 68000 | 3, all at `$0000` (one with 262,271 variants in 287,973 generations over the same 12 rewritten bytes); none on the 68000 |

Findings, none of them fixed here:

- **Fifteen seconds found little new code, and about 100 of its frames were
  play.** The 900-frame run decodes 20,308 of 524,288 ROM bytes against
  19,568 at 250 frames: 740 more. Most of what it traced was already reached
  statically. The scenario was written blind; its picture was checked
  afterwards (2026-11-11) by running the same inputs through
  `consoles/md/tests/oracle/run.py`, which writes every frame. The 900 frames
  it wrote hash, as XRGB rows, to the capture's `video_sha256`
  (`a4b7a934…341fe`), so they are the capture's picture. They show: black
  until frame 64; the publisher logo to 182; the title animation from 198;
  the START/OPTION menu after the press at 240; the game started by the press
  at 300; the first stage's opening sequence from about frame 308 to 799; and
  first-stage play, with the score rising, from about frame 800 to 900. The
  presses at 360, 480 and 600 paused the game and those at 420, 540 and 660
  resumed it, so 180 frames show a still paused picture. The capture
  therefore holds boot, logo, title, menu, one opening sequence, pause and
  about 100 frames of one stage. It holds no options screen, demonstration,
  later stage, loss of a life, game over or ending, and the census does not
  say how much code those would add.
- **Fifty seconds, most of them play, found 396 more bytes.** The 3,000-frame
  scenario was written against the picture: its inputs were first run
  through `consoles/md/tests/oracle/run.py`, and those 3,000 frames hash to
  the capture's `video_sha256` (`30901ce7…0367db`). They show the options
  screen from about frame 306 to 630, with every row changed and its
  pad-control screen entered and left; the game started by the press at 750;
  the opening sequence to about frame 1090; then about 1,900 frames of play,
  still in the first stage at frame 3,000 with 11 hits scored. It decodes
  20,704 ROM bytes against 20,308 at 900 frames. Traced bytes rose by 2,556,
  but 2,160 of those the static walk had already reached; 5,988 statically
  reached bytes have still never executed. Decoded code is 3.9% of the image.
  This run holds no later stage, demonstration, game over or ending either,
  so it does not show whether the rest of the image is data or unreached
  code; it does show that more first-stage play is not where new code is.
- **The capture cap is split in half, and the preflight does not say so.**
  `run.py` gives the NMDH and NMDT streams half of `--max-bytes` each. A first
  3,000-frame attempt with a 128 GiB cap passed its preflight (83.7 GB
  estimated in total) and ended `incomplete_storage_limit` after 48 minutes
  when the NMDH stream reached 64 GiB; the completed capture's NMDH stream is
  71.1 GB of its 73.5 GB.
- **Intake memory grows with the variant count.** The 3,000-frame intake
  peaked at 6.2 GB against 1.2 GB at 900 frames, with 262,271 variants of the
  one patched Z80 program against 34,776.
- **The first 900-frame run failed in the reference core, since patched.** It
  ended `incomplete_core_error` near frame 258. BlastEm's Z80 bank-window
  handlers set the Z80 clock to the 68000's clock whenever the 68000 was
  stalled on a VDP transfer, even when the Z80 was already ahead, so a Z80 bus
  event ran three master clocks backward and the NMDT stream latched its
  per-device ordering error. Patch
  `tools/toolchains/patches/blastem/0003-z80-bank-wait-never-rewinds.patch`
  only ever delays the Z80; `bank_stall_check.py` is its authored control.
  This is a deliberate timing difference from upstream, and which of the two
  real hardware matches is not verified.
- **The Z80 runs three programs, one of them in 34,776 variants.** Before the
  family rule the 900-frame census held 30,121 Z80 identities, 30,118 of them
  in five lengths (254, 240, 227, 209 and 241 bytes), and 30,171 Z80 T1
  entries. They are one 303-byte program with a verbatim ROM source: the
  68000 rewrites six 16-bit immediates in it (12 bytes; two of the six within
  the first 250 frames), and its runs ended at five different last
  instructions. The census now holds 3 Z80 programs and 3 Z80 T1 entries;
  the 900-frame intake fell from about 56 minutes to 1,111 s (1.2 GB peak) and
  its code map from 452 MB to 3 MB. ROM counts are unchanged.
- **Every non-code ROM byte is `read`** because the boot checksum reads the
  whole image; the class separates nothing for this title.
- One 40-byte Z80 generation has 4 traced bytes, 36 unknown and no ROM source.

## Not done

- Any capture past the first stage: the demonstration, later stages, game
  over and the ending are untraced, and whether a life was lost in the
  3,000-frame run was not checked. A scenario should not press START after
  the game starts, since each press pauses or resumes it.
- `run.py`'s preflight compares its estimate with the whole `--max-bytes`,
  not with the half each stream gets.
- Checking patch 0003's bank-wait timing against hardware.
- Generations that differ in data bytes inside their extent are still split;
  no title or control has needed that yet.
- What the rewritten operands are (they are listed by offset, not interpreted).
- Compressed program sources are not reconstructed, only identified as absent.
- No static discovery of jump tables; computed targets come from traces only.
- 68000 RAM generations are implemented but exercised only by D01's fixture
  shape, not by a dedicated control here.
- Captures are streamed from disk (one validation pass, one read pass), but an
  event costs about 330 bytes on disk: 21.6 MB per frame for this title.
