# SNES workflow parity inventory

**D00 scope:** identify what the existing SNES method actually provides, what can be reused as a method, and what nativemd still needs. This is an inventory, not a port claim.

The [SNES playbook](../../../snes/docs/snes-decomp-learnings.md) sets two linked outcomes: symbolic assembly that rebuilds the whole original image byte-for-byte, and companion C whose behavior is verified against the running original. The assembly remains the preservation record; native execution uses C through the console API.

## Existing SNES evidence

The nativekit checkout contains SNES test infrastructure for synthetic hardware cases, but the decompilation workflow is game-specific. A separate local SNES game-tool checkout was clean at source revision `d881c8f57591fd78cf32f41e1798a79aa0246377`. Its source inventory included a scripted runner, catalog/census, disassembly and assembly filing, image rebuild, hybrid build, per-call and lockstep gates, and a combined gate. The built `sfxbox` runner and hybrid library were absent from the associated local build area during this audit, so availability here is source-level; no SNES game gate ran and no code was copied or ported.

| SNES capability | Existing method/tool source | nativemd status at D00 | Owning milestone |
|---|---|---|---|
| Scripted original execution and traces | Headless `sfxbox` scripts; trace output includes per-byte execution maps and control-flow edges. | The existing MD runner is for synthetic/reference comparisons and feature survey; it is not yet a callable 68000/Z80 original-code harness with full state hooks. | D01 |
| Catalog, widths, and census | `codemap.py`, `catalog.py`, `spccat.py`, `widths.py`, `worklist.py`, and static `census.py` combine dynamic code evidence with target discovery. | No lossless title code catalog or complete multi-CPU census exists. | D03, D07 |
| Symbolic assembly and image gate | `disasm.py`, `asmcontrol.py`, `asmdraft.py`, `asmfile.py`, `build.py`, and `image.py`; pinned asar; rebuild against an external baserom and require zero differing bytes. | vasm has small synthetic assembler controls. No title code map, symbolic skeleton, or whole-image zero-difference rebuild is implemented. | D03, D08 |
| Per-call behavior gate (A5) | `sfxbox --check`, orchestrated by `t3.py --live`; all calls must be identical on registers, flags, memory, and ordered hardware effects. | Reference instruction/state hooks and a per-call checker are absent. | D01, D04 |
| Combined lockstep gate (A7) | `sfxbox --lockstep`, orchestrated by `t3.py --image`; run C substitutions with recorded access-cycle replay and compare full snapshots every frame. | Concurrent original/C execution, cycle replay, snapshots, and lockstep checks are absent. | D01, D05 |
| Typed plain-C gate | Playbook section 18 requires a separate old-versus-new file and whole-tree harness before converting register-shaped C into typed C. | No distinct typed-C verification gate was found in the inspected tool sources. The playbook's required conversion gate is additional work; A5/A7 alone do not prove typed source quality. | D09 |
| Native game startup | SNES method runs the companion C through the native console. | At D00 the MD frame adapter advanced hardware and IRQs only. D02 added opt-in [native execution](native-execution.md): the reset vector's C routine on a resumable 68000 coroutine, held by authored controls. No retail game has run through it. | D02 (done), D06 |

The SNES oracle notes document meaningful negative controls: a dropped bank step failed A5 for all 12 calls crossing that bank and A7 for every script; a wrong-byte read failed 1,054,844 of 1,078,556 calls. These are historical SNES controls, not MD validation. The playbook requires equivalent planted-error controls before trusting new MD gates.

D00 used Ghidra 12.1.2 through a locally available DComp adapter for an initial 68000 reset-reachable disassembly of three shortlisted images. This helped screen hardware references, but it is not a reusable nativekit decomp tool, a lossless code map, a full-image census, or a proof that copied code and indirect targets are closed. The private facts and profiles remain under each ignored `build/decomp/<game-id>/` workspace.

## Porting boundary

The SNES tools target 65816, Super FX, and SPC700 execution with a patched bsnes core. Their evidence format and implementation are not directly reusable for 68000/Z80. D00 records the runner/catalog/assembly/A5/A7/typed-C gates as the parity target; D01-D05 own new MD-specific interfaces and controls. The MD contract remains native C over nmd's hardware API, with no guest CPU execution in a delivered game.

Source references: [SNES decompilation playbook](../../../snes/docs/snes-decomp-learnings.md), [SNES test runner and oracle](../../../snes/tests/README.md), [nativemd contract](../contract.md), and the [D00 contract](../../../../docs/superpowers/plans/2026-11-04-nativemd-decomp/d00-baseline-and-first-title.md).
