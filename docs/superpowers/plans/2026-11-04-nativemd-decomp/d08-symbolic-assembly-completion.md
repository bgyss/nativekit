# D08: Finish the symbolic, byte-exact assembly record

**Status:** Proposed; not executed. Inherits the [shared execution contract](README.md#shared-execution-contract) and [full goal](GOAL.md).

## Objective

Reach the SNES playbook's T4 preservation standard for the selected ROM.

## Prerequisites

D03 lossless skeleton and D07 behavior/census closure; symbolic filing can progress alongside D07. Refresh the exact source/tool baseline and read the [index and source references](README.md) before execution.

## Work

1. Replace raw code ranges with named symbolic 68000 and Z80 assembly, comments and shared RAM/hardware/data symbols organized by game responsibility.

2. Keep routine identities and WHAT/HOW/entry contracts aligned between assembly and C. Track aliases, inner entries, jump tables, overlays and code/data overlaps with explicit ownership.

3. Retain external baserom references for legitimate data/unknown preservation spans, but forbid raw code incbins from receiving symbolic-code credit. Document unusual/undecodable executable encodings rather than silently excluding them.

4. Rebuild exact code placement, branch form, endian layout, headers, compressed upload sources, padding and checksums without rewriting the original input.

5. Run a final independent census reconciliation and symbol/address audit; explain all remaining opaque data regions and their classification evidence.

6. Add clean-room build instructions that require the exact user-supplied ROM hash and preserve attribution/license boundaries for any external reference material.

## Required artifacts

Complete symbolic assembly tree, symbol maps, whole-ROM rebuild recipe and final T4/census report.

## Acceptance criteria

- Every code byte is filed symbolically in a named routine that is T3-certified; raw code, unnamed code and orphan counts are zero.
- Rebuilding the original input yields identical size, SHA-256 and zero byte differences.
- A changed branch encoding, address symbol, compressed-code source or data placement fails the image gate.
- Public source/artifact policy is reviewed separately; the rebuild contains no checked-in commercial ROM bytes/assets.

## Validation

Run whole-image comparison from a clean game build directory using the bound exact ROM. Check symbol references, ownership spans and the shared decoder controls; run behavior gates when semantic source changes, not for proven object-identical renames.

Existing tasks are listed in the [command contract](README.md#planned-locations-and-commands). New decomp commands must first be implemented and documented; their names in this plan are not existing tool capabilities. Record commands actually run, exit status and source/input identities.

## Stop conditions and evidence limits

A bit-identical all-incbin image is raw preservation; it earns neither T1 recovered-code credit nor T4 certification. Unresolved executable bytes or census ambiguities prevent full symbolic completion even if the image gate passes. Preserve partial progress and report the first unmet gate; do not mark this milestone complete until all acceptance criteria hold.

## Copy-ready goal prompt

```text
Complete D08: Finish the symbolic, byte-exact assembly record, following
docs/superpowers/plans/2026-11-04-nativemd-decomp/d08-symbolic-assembly-completion.md.
Read AGENTS.md, that contract, its README.md shared rules and source references,
the nativemd contract and SNES decomp playbook. Verify prerequisites against
the current checkout. Implement only this milestone and necessary prerequisite
work already authorized by the shared contract; do not silently expand scope.

Reach the SNES playbook's T4 preservation standard for the selected ROM.

Produce every required artifact and satisfy each observable acceptance gate.
Keep user ROM inputs external, native delivery free of guest CPU execution,
candidate evidence separate from hardware qualification, and all missing or
unverified evidence explicit. Use meaningful negative controls for new gates,
run the prescribed affected checks, preserve unrelated changes and record
focused verified commits. Finish with artifact links, actual commands/results,
source/input identities, measurable progress and remaining gates. Do not claim
completion from scaffolding, generated files or a narrower passing subset.
```
