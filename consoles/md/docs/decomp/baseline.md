# D00 baseline

**D00 status: complete.** The checkout is refreshed; the SNES parity inventory, current tool identities, fresh synthetic check, three-candidate screen, exact first-title binding, and versioned title scenario inventory are recorded. This is a selection baseline, not a title compatibility claim.

## Checkout and host

- Planned baseline: `1777411c701c04ab91362d938b4301fb94990679` (`Plan: define nativemd ROM-to-decomp milestones`). The linked worktree was refreshed from clean ancestor `bc14c17` to this exact detached commit after `git fetch --all`; its tracked state was clean at refresh.
- The fetch updated `upstream/main` to `9d4c1d3` and `origin/main` to `6f90b5c`. D00 stays pinned to the explicitly requested plan baseline.
- Host used for the fresh check: macOS 27.2, Darwin arm64. The development tools reported Nix 3.23.2, mise 2026.11.0, AppleClang 21.0.0 (`clang-2100.4.1.1`), CMake 4.4.3, and Python 3.14.2.

## Tool identities

The source checkout revisions below match the pins in [`pins.env`](../../../../tools/toolchains/pins.env). The binary hashes were read from the existing nativekit build area in the primary checkout at the same source revision; they identify available artifacts but do not mean D00 used those tools. The fresh consumer check did not need them. This worktree had no built toolchain at start.

| Tool | Pinned source revision | Available binary SHA-256 |
|---|---|---|
| asar 1.91 | `6a6dbdaed8cb773e583052ce4f66375bd896c3a1` | `9d0e4aa4ba989c813718f59711b9a68a0077c656f43c283ab04e65e14c5df0c8` |
| Patched bsnes libretro core | `78474e5d39f58872dc21aca18cf9eb583b147fe7` | `a1d8eac1f05c25ec422d7c894d9eb44341e65b16fc67d3c66354d06d047eaccb` |
| Patched BlastEm libretro core | `2230addc4dd74dd094fd264987fd808abba32d0e` | `af61f42b79bc45f8a26ed2ff10c57afcf798ef05bc6000eb11da1246e2c4d1f0` |
| vasm 68000 assembler | `685a87e5ed14285350ccdb6581c9771bc8df6c7d` | `bb73cd220a7420a80598a0efae20b73486b2203b053cd01026996173360b876d` |
| vasm Z80 assembler | `685a87e5ed14285350ccdb6581c9771bc8df6c7d` | `4dcd90ab81e81a8f436007db6331d6fd3cc8e75462bf84344cca767242004ce0` |

D00's candidate static screen used the prebuilt DComp Ghidra adapter in its locked Nix shell, directed to the ignored nativekit title workspace. The adapter checkout is at `6a04968309714301ca67337e14e6555b1746ca3b` (binary SHA-256 `16562a7bf596fb9db78c8c2e397025e6b12c6899192e1d02ce4e38c13d7277bd`); its facts report Ghidra 12.1.2 and Azul Java 21.0.11. Its Nixpkgs lock is `a7868a727837f3c09cee2ce0ca671c76b1589fed` (`flake.lock` SHA-256 `65470c32653c99f1e505e0875fad59c1771a36453e83086658dedcf40070a036`). This was a one-off candidate audit, not a nativekit dependency or a complete code census.

The vasm source pin is a mirror commit identified as version 1.9d; equivalence to the author's 1.9d archive remains unverified in the handoff. The provisional D00 selection binds `md-f68818ae1a61` to md1va3, NTSC, one 3-button player; see [title-selection.md](title-selection.md) and the ignored private manifest.

The nmd 1.0 contract supports md1va3 and md1va6 with TMSS, NTSC and PAL, and three- or six-button pads. A zero-initialized cart defaults to NTSC, md1va3, and six-button pads on both ports. These are API defaults, not a decision about the first game. See the [nativemd contract](../contract.md).

## Fresh evidence

| Command | Result | Scope |
|---|---|---|
| `nix develop --command mise run test:md-consumer` | Passed, exit 0 | Built the separate nmd consumer and ran it headless for five frames. No commercial ROM or candidate-core comparison was involved. |
| Fresh 120-frame feature survey, three selected inputs | All six no-input/Start scenarios completed on core SHA-256 `af61f42b79bc45f8a26ed2ff10c57afcf798ef05bc6000eb11da1246e2c4d1f0` | Candidate evidence only; no mapper or six-button probe was observed. SRAM stayed unavailable because the images declare no SRAM window. |
| Fresh 360-frame scripted candidate runs | All three completed on md1va3/NTSC with 3-button pads | Captures are private under ignored `build/decomp`; they are candidate-core observations, not compatibility claims. |
| Reset-reachable Ghidra static screen | After Burner II: 6,312 instructions/163 functions; Batman: 10,488/281; Streets of Rage: 7,372/257 | All three profiles used the exact reset vectors. Indirect calls/jumps and full-image code/data classification remain open. |
| 600-frame feature trace | Inconclusive: the pinned core raised `core trace error` before the bounded survey finished | This is a trace-capacity/tool limit, not a title compatibility failure. The 360-frame longer runs used untraced captures instead. |
| `git diff --check` | Passed, exit 0 before the current D00 document edits | Re-run after these edits before commit. |

## Existing known differences

- nmd lists six individually reasoned candidate differences in [`consoles/md/tests/rom/KNOWN`](../../tests/rom/KNOWN): TAS write behavior; three DMA source cadence cases; and two PSG latch/noise cases. They remain differences from pinned BlastEm, not waivers for translation errors or hardware findings.
- SNES has 56 listed cases in [`consoles/snes/tests/rom/KNOWN`](../../../snes/tests/rom/KNOWN). They are not freshly rerun here.

## Prior evidence, not fresh passes

The [Stage 4 handoff](../../../../docs/superpowers/handoff/2026-11-01-nativemd-handoff.md) records earlier results: 59 MD synthetic cases with no unlisted differences and a 0/59 golden comparison; SNES golden 0/2393 and ROM 0/1164 unlisted differences with 56 listed differences and none newly matching; and 20.3 seconds CPU for SNES timing against a 23.07-second threshold. The same handoff records eight of nine build targets completed, a Windows link failure, stale iOS simulator warnings, and four host/render failures in the full test set. None of those results was rerun as part of this baseline.

The tracked [survey report](../survey.md) is historical: it reports 416 candidate files at 120 frames per scenario, with Start at frame 60. Its recorded core SHA-256 (`b5afaf3ab1f2d7e36735e877153a45f7effef8b563e697e59981e4f05295db53`) differs from the currently available BlastEm binary hash above. It was not used as current title-selection evidence.

## Downstream evidence limits

The supplied `titles` subdirectory contained catalog metadata and SNES patch material, not Mega Drive images. Header-only screening of its parent game-material store found 381 raw `.gen` inputs across adjacent collections; no archive extraction or full-library run was done. Three 512 KiB candidates were hashed and screened. The provisional selection is `md-f68818ae1a61`, SHA-256 `f68818ae1a616562e4c7dc77932bbc9f6bc4e42229f5b5d5796d4b2c2cc3f334`; its raw container, checksum, zero-offset policy, region, model, and pad setup are recorded in the ignored private manifest. No local ROM path is persisted in tracked files or the manifest.

D00 exits with the exact candidate, initial base-console eligibility decision, and scenario inventory bound. D03/D06 and later gates still need to close the full mapper/SRAM/RAM-execution audit, indirect targets, generated code, and the native Start-to-gameplay path. Candidate surveys and Ghidra's reset-reachable functions do not close the full-image census or certify nmd compatibility.
