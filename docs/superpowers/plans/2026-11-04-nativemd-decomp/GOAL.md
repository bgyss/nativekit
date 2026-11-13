# Goal prompt: Mega Drive / Genesis ROM to verified native decomp

Copy the block below into a new implementation task. This document is a proposed goal, not evidence that its work has run.

```text
Implement the assisted Mega Drive / Genesis ROM-to-decomp workflow defined in
docs/superpowers/plans/2026-11-04-nativemd-decomp/README.md and its D00-D12
contracts. Carry the complete objective through the first game's verified
delivery and the second-title reuse pilot. Do not redefine success around
synthetic tools, a raw assembly skeleton, draft C or a title screen.

Read AGENTS.md, the milestone index, the SNES decompilation playbook,
the nativemd 1.0 contract, contract discipline and the Stage 4 handoff first.
Refresh the exact checkout and tool identities. Treat historical gate results
as prior evidence, not current passes.

The required first-title outputs are:
1. Named symbolic 68000 and Z80 assembly whose whole ROM rebuild matches
   the exact user's original image with zero differing bytes.
2. One maintainable plain-C companion tree, with ABI wrappers only where
   needed, all recovered code certified through the adapted A5/A7 gates.
3. A standalone native game linking nmd and entering through nmd_main,
   including genuine reset/main execution, IRQs, Z80 sound, video and input.
4. Source-bound coverage/census, behavior, timing and delivery reports;
   clean reproducible build/extraction using external user-owned ROM data.
5. Reusable tooling and a distinct second-title bounded pilot demonstrating
   reuse without claiming arbitrary-ROM automatic decompilation.

Start at D00. Audit actual SNES tooling, rank eligible user-owned candidates
and bind one exact ROM hash/model/region plus a game workspace. If a ROM path
or external workspace authorization is missing, ask concisely using the
available text-input tool while continuing independent synthetic tooling.
Never infer permission to acquire ROMs or write into other repositories.

Implement D01's execution/state hooks and D02's missing native reset/main
path; current nmd_run_frame only advances hardware and dispatches IRQs.
Prove each new verification gate can detect a planted defect. Establish the
lossless code map/rebuild, A5 per-call checker and A7 combined lockstep for
both CPUs before scaling translation. Integrate a real native playable slice
in D06 before whole-game batches. Then close the census, complete symbolic
assembly and typed C, validate full native gameplay/timing, package a clean
first-title result and prove reuse on a second title.

Preserve the shared execution contract and milestone prerequisites.
Keep game source separate from nativekit tooling, use authored synthetic
fixtures in nativekit, keep commercial ROM data/captures/private paths out
of version control, and keep one C tree for hybrid and native builds.
Test-only reference fallback/replay must never appear in the delivered game.
Do not introduce a guest interpreter/JIT or instruction-by-instruction
execution engine. Preserve the frozen nmd/kit compatibility rules, SNES
behavior, existing golden baselines and individually reasoned KNOWN entries.

Track every milestone as unstarted, partial, blocked or complete.
For each batch, record the exact source/ROM/core/tool/scenario identities,
unique code-byte ownership per CPU and code generation, unresolved targets,
actual calls/frames/samples compared, and validation failures. Missing,
uncovered, divergent, stale or timed-out evidence must fail required gates.
Use small focused checks during work and the release matrix at the specified
boundaries; record host limitations and never replace a golden to hide drift.

Continue autonomously through all authorized local work. Use focused commits
and preserve unrelated edits. Do not push, merge, publish, obtain ROMs or
perform an unbounded library campaign. Keep enhancements separate from
preserved behavior. If the chosen title requires unsupported hardware,
identify the evidence and scope decision rather than adding an approximation.

Finish with links to the maintained game and shared tooling, exact clean
build/rebuild/verification commands, code-byte and T3/T4 totals, native
scenario/timing results, dependency audit and second-title pilot evidence.
Keep incomplete human, host or independent-hardware qualification explicit.
Do not mark the full goal complete while any required milestone remains open.
```

For a smaller execution scope, use the copy-ready prompt at the end of an individual milestone. That authorizes only that contract and its stated prerequisite work.
