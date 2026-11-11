# nativemd ROM-to-decomp milestones

**Current status: D00 complete; D01 complete; D02 complete; D03 acceptance gates met (first title rebuilds byte-identically; its traced evidence covers 900 frames of unconfirmed content and its findings are open); D04-D12 unstarted.** D01 completion covers the pinned candidate tooling and authored synthetic acceptance gates; it does not qualify retail-title behavior or hardware accuracy. D02 completion covers the opt-in native execution path (contract 1.1.0) against authored synthetic controls only; no native run has been compared with the reference or made with a retail title. This suite was written 2026-11-04 from a source review of nativekit `21525cc`; at authoring time, no retail-ROM run or verification gate had been executed.

## What must be added

nativemd is the native hardware/runtime library. The missing project is the assisted game-recovery and verification workflow that produces its cartridge's C. Matching the repository's SNES method means **two linked deliverables**: symbolic assembly that rebuilds the exact original ROM, and readable C whose behavior is checked against original execution and then runs through nmd. It is not a promise of unattended arbitrary-ROM conversion.

| Area | Present evidence or implementation | Work still required |
|---|---|---|
| SNES method | [SNES playbook](../../../../consoles/snes/docs/snes-decomp-learnings.md), especially sections 1, 4, 5, 9 and 15-18 | Actual local game-tool sources are inventoried in [workflow parity](../../../../consoles/md/docs/decomp/workflow-parity.md); reusable 68000/Z80 equivalents remain absent |
| MD hardware | [1.0 contract](../../../../consoles/md/docs/contract.md), synthetic comparison, profiles and consumer | Real-game integration and title-exposed defect controls |
| Native startup | [frame adapter](../../../../consoles/md/src/front/md_console.c), [reset state](../../../../consoles/md/src/console/bus.c), [IRQ dispatch and 68000 run](../../../../consoles/md/src/console/dispatch.c), [native execution contract](../../../../consoles/md/docs/decomp/native-execution.md) | D02 added reset-vector entry and a resumable 68000 run behind `NMD_EXECUTION_NATIVE`; a real title through it, and VDP/sound state observation, remain |
| Reference hooks | [NMDT v2](../../../../consoles/md/tests/oracle/TRACE_FORMAT.md), [hook ABI](../../../../consoles/md/tests/oracle/trace_abi.h) | Instruction/state/control hooks, callable original execution, full comparison snapshots and replay capabilities |
| Assembly recovery | Pinned vasm tools; two small [assembler smoke controls](../../../../consoles/md/tests/toolchain_check.py) | Decoder controls, code/data census, RAM-code identities, exact whole-image rebuild and symbolic filing |
| C recovery | Public 68000/Z80 state and bus APIs; synthetic C programs | Routine catalog, ABI adapters, semantic controls, A5, A7, complete title C and typed refactoring |
| Title selection | [416-candidate bounded survey](../../../../consoles/md/docs/survey.md) | D00 selected an exact revision for the current base-console scope; D03/D06 must close the full hardware/RAM census and native-playable scenarios. Short traces do not prove absent features |
| Delivery | Nativekit host/input/audio/build infrastructure | Full native game, measured timing on held-out scenarios, source/data separation and clean reproducible delivery |

The [Stage 4 handoff](../../handoff/2026-11-01-nativemd-handoff.md) records prior MD/SNES gates and Windows/host-render limitations. Those results are historical baseline evidence, not checks rerun for this proposal. No retail game is currently certified by this suite.

## Recommended approach and scope

Use an **assisted preservation-first workflow**, proving a small native vertical slice early and then closing the whole title. This follows the existing SNES method and yields readable source plus auditable evidence. A generic generated static translation could be a separately justified future strategy, but does not itself meet this suite's plain-C and symbolic-preservation goals. Adding an opcode interpreter would allow ROM playback without accomplishing the requested decomp result.

Choose one base-console title within current hardware scope, including its Z80 sound programs. SRAM/EEPROM, mappers, expansion hardware and new platforms require explicit scope extensions if needed. Do not choose a title merely because a short survey completed. Keep the selected md1va3/TMSS model and separate 68000/Z80 profiles from the approved Stage 4 design.

## Milestones and dependencies

| ID | Deliverable | Prerequisite | Status |
|---|---|---|---|
| [D00](d00-baseline-and-first-title.md) | Establish the SNES parity target and select the first title | None. Shared preparation can proceed without a commercial ROM | complete |
| [D01](d01-reference-execution-hooks.md) | Build the decomp verification ABI for both reference CPUs | D00 workflow and baseline inventory | complete |
| [D02](d02-native-reset-and-execution.md) | Make the native game reset and execution path real | D00 contract inventory | complete |
| [D03](d03-lossless-intake-and-code-census.md) | Recover code identities and a lossless assembly skeleton | D01 execution/provenance hooks | gates met; findings open |
| [D04](d04-per-routine-c-verification.md) | Verify C routines against original execution on both CPUs | D01 hooks, D03 shared catalog/decoder controls | unstarted |
| [D05](d05-whole-image-lockstep.md) | Verify combined C substitution under concurrent hardware execution | D01, D04 | unstarted |
| [D06](d06-first-native-playable-slice.md) | Deliver the first game slice through the actual native launch path | D00 selected title, D02 execution path, D03 title skeleton, D04 and D05 verified checking infrastructure. | unstarted |
| [D07](d07-whole-game-behavior-recovery.md) | Recover and certify the complete game's C behavior | D06 first playable and stable A5/A7 infrastructure. | unstarted |
| [D08](d08-symbolic-assembly-completion.md) | Finish the symbolic, byte-exact assembly record | D03 lossless skeleton and D07 behavior/census closure | unstarted |
| [D09](d09-plain-typed-c.md) | Turn certified translations into maintainable plain C | D07 certified behavior | unstarted |
| [D10](d10-full-native-game-and-timing.md) | Qualify complete native gameplay and measured timing | D02, D07 and D09 | unstarted |
| [D11](d11-reproducible-first-game-delivery.md) | Package the first completed decomp and native game | D07-D10 complete at their declared gates | unstarted |
| [D12](d12-reusable-rom-to-decomp-workflow.md) | Prove reuse on a second ROM and document the operating workflow | D11 full first-title delivery | unstarted |

D01-D05 shared synthetic tooling can progress independently of title input. D00 has selected an exact first-title revision and defined its scenario inventory; title-dependent D03/D06 work must use that binding and close the remaining code-census and native-playable gates. Mark partially completed milestones partial; do not convert missing retail evidence into synthetic-only completion. D08 symbolic filing and D09 refactoring may proceed in verified batches during D07; full completion still requires their listed gates.

## Shared execution contract

Every milestone inherits these rules. Its copy-ready prompt links here so it can be executed independently.

- Re-read AGENTS.md, this index, the selected milestone, the [MD contract](../../../../consoles/md/docs/contract.md), [contract discipline](../../../../kit/docs/contract-discipline.md) and the [SNES playbook](../../../../consoles/snes/docs/snes-decomp-learnings.md). Refresh checkout/tool identities; do not assume the planning baseline is still current.
- Preserve native delivery: C implements game behavior; nmd implements hardware. Candidate execution, opcode helpers, original-code fallback and exact-cycle replay belong only to verification tooling. No emulator, guest interpreter/JIT, or instruction execution fallback in the delivered game.
- Keep one C source tree for hybrid verification and native builds. ABI/register wrappers are allowed at real boundaries; a game made of instruction helpers or carried register files is an intermediate result.
- Reuse existing kit hosts and input paths. Keep kit/ console-neutral. Do not impose a new host framework or import another project's policy through analogy.
- Follow the 1.0 contract rules. Do not repurpose existing fields or silently change zero-default behavior; document additions, fixes and version changes.
- Never commit ROM bytes, executable game code recovered from the commercial ROM, graphics, audio, extracted assets, snapshots or private ROM paths to nativekit. Maintained recovered source belongs in the separately authorized game workspace with its own provenance and distribution decision. Shareable nativekit docs/tool fixtures use authored synthetic examples.
- Use ROM paths only at runtime. Keep local captures/extracted data under ignored build/decomp/<game-id>/. Public reports use hashes and neutral game identifiers. A ROM hash identifies input, not ownership or redistribution rights.
- Keep BlastEm-derived equivalence distinct from hardware accuracy. A second tool sharing ancestry is not independent hardware evidence. Preserve the existing six candidate KNOWN differences; add only individually investigated hardware divergences, never translation-error waivers.
- Use meaningful planted-error controls for new evidence gates, fail on missing/empty/stale data, and report timeouts as inconclusive. Record actual calls, code bytes, frames and samples compared.
- Use focused commits for verified implementation batches. Default to a single working session; additional agents require an explicit applicable delegation instruction. Do not push, merge, publish or change external services without authorization.
- This task writes plans only. Copying an execution prompt into a later task authorizes its described local work; it does not authorize public release, ROM acquisition or unbounded processing of a library.

## Progress and completion claims

Track unique code-byte ownership per CPU **and code generation**, plus unknown/conflicting bytes and unresolved indirect targets. Dynamic coverage never supplies the complete denominator by itself.

| State | Required evidence |
|---|---|
| Raw preserved, unclassified | External baserom references can rebuild the image; no recovered-code credit |
| T1 | Traced, address-labeled code entries in the lossless intake, reassembling exactly; whole image still rebuilds exactly |
| T2 | Named/filed assembly and C draft; not certified |
| T3 | Complete routine contracts, all routine code covered by valid checker scenarios, A5 identical, combined A7 identical on the declared suite |
| T4 | T3 plus symbolic assembly in its original image with zero differing ROM bytes |
| Native first playable | D06's bounded scenario runs through the actual game launch/input/audio path |
| Full first-title delivery | D07-D11 all complete, all census code T3/T4, typed C and native scenarios/timing meet declared gates |
| Reusable workflow | D12 second-title pilot complete; this is not arbitrary-ROM support |

Report harness-injected routine coverage separately from ordinary gameplay coverage. Invalid or unreachable entry states do not grant certification. An untestable routine stays an explicit full-certification blocker. Booting, showing a title screen, reaching an ending and finishing a full code census are different claims.

Exact A5/A7 harness comparisons use the original reference's recorded scheduling. Native cost profiles are estimates: native timing gets predeclared numerical bounds and held-out measurements, never a false cycle-exact claim.

## Planned locations and commands

These are proposed interfaces to implement, **not commands that exist today**:

- Reusable tooling: `consoles/md/tools/decomp/`.
- Shared synthetic controls: `consoles/md/tests/decomp/`.
- Reusable method/ABI docs: `consoles/md/docs/decomp/`.
- Private/generated title work: `build/decomp/<game-id>/`; move maintained game source only to a user-authorized repository.
- Shared test entrypoint: `mise run test:md-decomp` is implemented, depends on `tools:md`, and runs the D01 ABI, format, negative-control, actual-core and checkpoint-replay suite. Keep it non-empty as capabilities land.
- CLI operations: `intake`, `rebuild` and `census` exist in `consoles/md/tools/decomp/intake.py` ([syntax](../../../../consoles/md/docs/decomp/intake.md)). trace (beyond `run.py`), check-calls, check-lockstep, native-check and report are still to define. Do not publish pretend invocations in results.
- Each title owns reproducible CMake/native-scenario and assembly verification commands. Put the exact working commands in its manifest and README as soon as implemented.

Existing tasks can be invoked today from the repo root inside `nix develop`. Relevant focused tasks include `test:md-console`, `test:md-io`, `test:md-z80`, `test:md-sound`, `test:md-bus`, `test:md-interrupt`, `test:md-vdp`, `test:md-dma`, `test:md-render`, `test:md-execution`, `test:md-toolchain`, `test:md-oracle-smoke`, `test:md-trace`, `test:md-features`, `test:md-survey`, `test:md-profile`, `test:md-consumer`, `test:md-golden-check`, `test:md-rom`, and `test:kit`; use `mise run <task>`.

At a platform release boundary, run `mise run build`, `mise run test`, `mise run test:consumer`, `mise run test:kit`, `mise run golden:compare`, `mise run test:rom`, `mise exec -- python3 consoles/snes/tests/golden.py time`, the MD tasks above, `mise run golden:md-compare`, and `git diff --check`. Preserve the existing SNES timing threshold in the handoff (23.07 s CPU) and frozen baselines. Record actual host/toolchain failures separately; no re-recording a golden to clear unexplained drift.

## How to start

Use [GOAL.md](GOAL.md) for the complete objective, or paste one milestone's final prompt to run only that milestone. D00-D02 are complete, D03 has met its acceptance gates with open findings and D04-D12 are unstarted. The first-title hash is bound; a separate maintained game repository has not been supplied or authorized, so private generated work remains under ignored `build/decomp/<game-id>/`.
