# D03: Recover code identities and a lossless assembly skeleton

**Status:** Acceptance gates met 2026-11-10 ([results](#results)): shared tooling, synthetic controls and the first title's byte-identical rebuild. The first title's traced evidence covers 3,000 frames, about 1,900 of them first-stage play and none past the first stage, and its findings are open. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Create an auditable map from original ROM bytes to instructions, data, copied code and routine entries, and rebuild the first ROM exactly.

## Prerequisites

D01 execution/provenance hooks; D00 title binding for the real-title portion. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Build or adapt a pinned 68000 and Z80 decoder/assembler path using the existing vasm pins as candidates. Expand beyond today's two tiny assembler smoke examples: test all declared legal encoding families, addressing modes, operand widths, branch encodings and Z80 prefixes needed by the claimed scope.

2. Handle unrecognized or noncanonical encodings losslessly and explicitly. Raw bytes may preserve an intake image, but are not evidence of decoded or symbolically recovered code. Track assembler/provenance limitations.

3. Build per-byte code/operand/data/unknown classification from unfiltered execution traces plus static walks from vectors, calls, branches, computed tables and discovered code references. Keep overlapping/conflicting interpretations unresolved until evidenced.

4. Model ROM offsets separately from CPU addresses. Record code copied or decompressed into work RAM/Z80 RAM, upload source identities, bank-window mappings, aliases, and replacement generations; one RAM address can refer to different code over time.

5. Emit a whole-image raw assembly skeleton with explicit placement and external baserom data references. Reconstruct uploaded/compressed programs without assuming decoded bytes can replace their original compressed representation.

6. Produce a routine catalog and static census that includes unexecuted candidates, unresolved indirect targets, unnamed ranges and orphan candidates. Compare rebuilt length and every byte against the exact original input.

## Required artifacts

Decoder/assembler controls, map and catalog schemas, relocation/upload-generation records, census report, and the first-title lossless assembly skeleton in its bound game workspace. Mark traced address-labeled entries T1; all-incbin preservation and unclassified ranges receive no recovered-code credit.

## Acceptance criteria

- The full original image, including headers, padding and data, rebuilds with identical length, SHA-256 and zero differing bytes.
- An all-incbin rebuild is labeled raw preservation only; decoded code bytes and remaining unknown bytes are separately reported.
- Known synthetic dead code, computed targets and two different programs uploaded to the same RAM address are represented without false identity merges.
- A changed instruction, data byte, placement or source-ROM identity causes the corresponding gate to fail.

## Validation

Run mise run test:md-toolchain for existing controls and add meaningful decoder/rebuild/census checks to the new shared decomp check entrypoint. Exercise the real title's rebuild command in its workspace; publish the exact command in its manifest.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

Do not count unknown bytes as code or data by guess, claim all ISA encodings from a few controls, or call a raw incbin skeleton a finished symbolic decomp. If source normalization is necessary, retain a reversible mapping to the original file. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Results

2026-11-10, branch `nativemd`, by the
[implementation plan](../2026-11-10-nmd-decomp-d03-implementation.md). Tools:
vasm and BlastEm at the pins in `tools/toolchains/pins.env`. Checks use
authored fixtures assembled at test time. The first title's ROM (SHA-256
`f68818ae…f334`) was read from a path given at runtime; its outputs are under
ignored `build/decomp/md-f68818ae1a61/` and only hashes and counts are tracked.

| Acceptance line | State |
|---|---|
| Full original image rebuilds with identical length, SHA-256, zero differing bytes | Met for the first title (524,288 bytes, 0 differing) and the authored control images. |
| All-incbin rebuild labelled raw preservation; decoded and unknown bytes reported separately | Met in tooling (`--raw`, `census.json`), on controls; the first title's census reports 20,704 decoded and 20 raw code bytes at 3,000 frames. |
| Synthetic dead code, computed targets, two programs at one RAM address without false merges | Met (`census_check.py`). |
| Changed instruction, data byte, placement or source-ROM identity fails its gate | Met (seven planted errors). |

Artifacts: `consoles/md/tools/decomp/{m68k_dis,z80_dis,asmverify,codemap,skeleton,intake}.py`,
`consoles/md/tests/decomp/{m68k_decoder_check,z80_decoder_check,census_fixture,census_check,identity_check}.py`,
[intake.md](../../../../consoles/md/docs/decomp/intake.md) (schemas, credit
rules, decoder control counts and limits).

Commands run: `mise run test:md-decomp`, exit 0 (all D01 gates plus the three
new ones); for the first title, `run.py --mode estimate|capture` (250, 900 and 3,000
frames, complete) and `intake.py intake|rebuild`, exit 0, exact lines in the
workspace's `WORKSPACE.md`.

Open, in [intake.md](../../../../consoles/md/docs/decomp/intake.md#first-title):
the 900-frame capture added only 740 decoded ROM bytes; its frames, checked
2026-11-11 against the capture's video hash, show it reached first-stage play
for about its last 100 frames and spent 180 paused, so everything past that is
untraced; a 3,000-frame capture (2026-11-11) written against the picture adds
the options screen and about 1,900 frames of first-stage play and decodes
20,704 ROM bytes, 396 more, so later stages, the demonstration, game over and
the ending are where any further code is; the captures complete only with reference-core patch
0003, whose Z80 bank-wait timing is unverified against hardware; uploaded or
compressed programs without a verbatim source are not reconstructed (work item
5); the skeleton lives in the ignored workspace because no game repository is
bound. Operand-patched Z80 generations are now one program with variants
(30,121 identities became 3). Whether D06 needs evidence
past the first stage depends on the slice it declares.

## Copy-ready goal prompt

```text
Complete D03: Recover code identities and a lossless assembly skeleton, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d03-lossless-intake-and-code-census.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Create an auditable map from original ROM bytes to instructions, data, copied code and routine entries, and rebuild the first ROM exactly.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
