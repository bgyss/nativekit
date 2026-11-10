# nativemd D03 Lossless Intake and Code Census Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An auditable map from ROM bytes to instructions, data, copied code and routine entries for both CPUs, and an assembly skeleton that rebuilds the image with zero differing bytes.

**Architecture:** Python tooling under `consoles/md/tools/decomp/`. Two table-driven decoders (68000, Z80) print the pinned vasm's syntax; nothing a decoder prints is trusted until the assembler reproduces the original bytes from it, line by line, so an encoding the assembler would re-encode differently stays raw bytes and earns no recovered-code credit. A code map classifies every byte of each *space* (the ROM image by file offset; each RAM program generation by CPU, load address and content hash) from static walks and NMDH execution evidence. The skeleton, census and rebuild gate are derived from the map.

**Tech Stack:** Python 3 (standard library only), pinned vasm `685a87e5ed14285350ccdb6581c9771bc8df6c7d` (`vasmm68k_mot`, `vasmz80_oldstyle`), NMDH v1 captures from D01, `mise` task `test:md-decomp`.

**Spec:** [D03 milestone contract](2026-11-04-nativemd-decomp/d03-lossless-intake-and-code-census.md); [shared execution contract](2026-11-04-nativemd-decomp/README.md#shared-execution-contract).

**Execution method:** Native single-session execution.

**Outcome (2026-11-10):** Tasks 1-5 executed; Task 6 open. Deviations: the code map has no separate `codemap_check.py` (its controls are in `census_check.py`); `census.py` was folded into `intake.py`; checks were written with the code and rely on planted errors rather than observed red runs. Results are in the [milestone file](2026-11-04-nativemd-decomp/d03-lossless-intake-and-code-census.md#results).

## Refreshed Baseline

- Branch `nativemd`, HEAD `756696d`; D00-D02 complete.
- `build/decomp/md-f68818ae1a61/` and the first title's ROM are **absent from this checkout**. Tasks 1-5 are synthetic and proceed; Task 6 (the real title) is blocked until the user supplies the ROM path at runtime.

## Global Constraints

- No ROM bytes, recovered code or private paths in tracked files; fixtures are authored and assembled at test time.
- Unknown bytes are never counted as code or data by guess. A raw (`incbin`/`dc.b`) byte earns no recovered-code credit.
- A decoder's coverage claim is exactly what its controls exercise: the 68000 first-word sweep and the Z80 opcode sweep, with the fixed operand words they use.
- ROM file offsets and CPU addresses are distinct types everywhere in the map.
- Every new gate has a planted error that fails it; missing, empty or stale inputs fail.
- Reports name inputs by SHA-256 and neutral ID only.

## Review Focus

- A decode that reassembles to different bytes (noncanonical operand, reserved bits, short/long form): must stay raw, counted separately. Task 1/2 sweeps, Task 4 rebuild.
- Data that happens to decode as code next to real code: the static walk must not claim bytes no entry reaches. Task 5 dead-code and data-table controls.
- Two programs at one RAM address: no identity merge. Task 5.
- An instruction overlapping another interpretation (jump into the middle): recorded as a conflict, never silently resolved. Task 3.
- A stale or foreign capture (different ROM hash) offered as evidence: rejected. Task 3.

---

### Task 1: 68000 decoder and its assembler control

**Files:** create `consoles/md/tools/decomp/m68k_dis.py`, `consoles/md/tools/decomp/asmverify.py`, `consoles/md/tests/decomp/m68k_decoder_check.py`.

**Interfaces produced:**
- `m68k_dis.decode(data: bytes, offset: int, address: int) -> Insn | None`; `Insn(length, text, flow, targets, indirect, refs)`, `flow` in `seq cond jump call ret halt`; `text` is `None` when decoded but without an assembler form.
- `asmverify.verify(cpu, items, assembler) -> list[bool]`: items are `(address, original_bytes, text)`; true where the assembler reproduces the bytes.

- [x] Sweep all 65,536 first words with fixed extension words: every decode either reassembles byte-identically or is reported noncanonical by family; an authored source of labelled branches/calls checks lengths, flow and targets; planted errors (a wrong length, a wrong target, a changed mnemonic) fail.

### Task 2: Z80 decoder and its assembler control

**Files:** create `consoles/md/tools/decomp/z80_dis.py`, `consoles/md/tests/decomp/z80_decoder_check.py`.

- [x] `z80_dis.decode` with the same `Insn`; sweep every unprefixed, `CB`, `ED`, `DD`, `FD`, `DDCB`, `FDCB` opcode; undocumented forms the assembler cannot print stay raw; same planted errors.

### Task 3: Code map

**Files:** create `consoles/md/tools/decomp/codemap.py`, `consoles/md/tests/decomp/codemap_check.py`.

- [x] Spaces, per-byte classes (`opcode operand data unknown`), evidence kinds (`vector static traced`), conflicts, unresolved indirect transfers; RAM generations rebuilt from NMDH bus writes and instruction events, with the ROM offset of a verbatim upload source when one exists; JSON schema version 1; capture identity must match the ROM hash.

### Task 4: Skeleton, rebuild gate, catalog and census

**Files:** create `consoles/md/tools/decomp/skeleton.py`, `consoles/md/tools/decomp/census.py`, `consoles/md/tools/decomp/intake.py` (CLI: `intake`, `rebuild`, `census`).

- [x] Whole-image assembly with explicit placement, `incbin` of the external baserom for non-code, verified instruction lines for code; rebuild compares length, SHA-256 and every byte; report splits decoded, raw-noncanonical, data and unknown bytes; all-incbin mode labelled raw preservation; catalog with T1 only for traced, address-labelled entries.

### Task 5: Synthetic census controls and integration

**Files:** create `consoles/md/tests/decomp/census_fixture.py`, `consoles/md/tests/decomp/census_check.py`; modify `consoles/md/tests/decomp_check.py`, `consoles/md/tests/README.md`, create `consoles/md/docs/decomp/intake.md`.

- [x] Authored ROM with dead code, a computed jump table, and two different Z80 programs uploaded to the same RAM address; changed instruction, data byte, placement and ROM identity each fail their gate; wired into `mise run test:md-decomp`.

### Task 6: First title (blocked on input)

- [ ] Needs the user's ROM path at runtime and a decision on where the private workspace lives. Run intake, capture, census and rebuild under ignored `build/decomp/md-f68818ae1a61/`; publish hashes and counts only.
