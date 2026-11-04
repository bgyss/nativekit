# D00 first-title selection

**Status: partial.** D00 now has a ranked three-image shortlist and a provisional exact first-title binding. The full eligibility gate remains open because reset-reachable analysis is not a complete code census, indirect targets remain unresolved, and the scripted start flow has not reached gameplay.

## Input screen and candidate IDs

The supplied `titles` subdirectory contained title metadata and SNES patch material. Its parent game-material store had 381 raw `.gen` images across adjacent collections. I screened headers only, then hashed and analyzed three 512 KiB raw images. No archive was extracted and no full-library survey ran. The user ROM path and original filenames are not recorded here.

The following fresh evidence used the currently available pinned BlastEm binary, SHA-256 `af61f42b79bc45f8a26ed2ff10c57afcf798ef05bc6000eb11da1246e2c4d1f0`. Each image had the `SEGA` signature at offset `0x100`, a reset vector inside the ROM, and a matching header/computed checksum. None declared an SRAM range. `sram_access` remains **unavailable** in the feature survey without an SRAM window; that result does not prove the game never accesses persistent memory.

| Rank | Neutral ID and SHA-256 | Reset-reachable static screen | 120-frame features | Reference and hardware screen |
|---|---|---|---|---|
| 1 | `md-f68818ae1a61` — `f68818ae1a616562e4c7dc77932bbc9f6bc4e42229f5b5d5796d4b2c2cc3f334` | 6,312 instruction facts, 163 functions, 5 indirect calls and 4 indirect jumps. One `0x200000` value is stored in a work-RAM table; no `0xA130F1`, `0xA130F3`, or `0xA14000` literal appears in this reset-reachable set. | No mapper, six-button, direct 68000 YM, or Z80 DAC event observed in either scenario. | Exact first-party manual found for the U/E family; 1P/3-button and volatile-score behavior are documented. U header, 512 KiB, no SRAM declaration. Best current scope/reference fit. |
| 2 | `md-db285e94aa98` — `db285e94aa9835f957f58a8df97f3d5a43416dd0e8bb78966b670d5573290ead` | 10,488 instruction facts, 281 functions, 6 indirect calls and 1 indirect jump. Many `0x200000` address-arithmetic references need semantic review. | No mapper, six-button, direct 68000 YM, or Z80 DAC event observed. | UE header, 512 KiB, no SRAM declaration. The 360-frame scene reached a stage/title label. No exact first-party manual was found in the Sega manual catalog during this pass. |
| 3 | `md-dd44f1204466` — `dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0` | 7,372 instruction facts, 257 functions, 4 indirect calls and 3 indirect jumps. A reset-reachable instruction references `0xA14000`; the access role and model requirement need review. | Direct 68000 YM writes observed; no mapper, six-button, or Z80 DAC event observed. | JUE header, 512 KiB, no SRAM declaration. The 360-frame scene reached a city scene. It adds sound and possible TMSS/model work. No exact first-party manual for this revision was found in the Sega manual catalog during this pass. |

The instruction/function counts are reset-reachable Ghidra facts, not complete code-byte totals. All three have unresolved indirect transfers, and none is cleared for RAM execution or unreachable code. The 120-frame observations are feature evidence only. The longer 360-frame captures used untraced input snapshots; they show candidate-core screens, not nmd compatibility or hardware accuracy.

## Selection decision

Select `md-f68818ae1a61`, SHA-256 `f68818ae1a616562e4c7dc77932bbc9f6bc4e42229f5b5d5796d4b2c2cc3f334`, as the **provisional** first-title revision. Bind D00 to `md1va3`, NTSC (`U`), and one active three-button player on port 1; port 2 is unused. The raw `.gen` image is 524,288 bytes, has no SMD header/interleave conversion, uses offset zero, and has a matching `0xbad0` checksum.

The rank favors the smallest reset-reachable instruction/function inventory among the three, no `0xA130F1/F3` mapper or `0xA14000` TMSS literal in that static screen, no mapper event in the bounded survey, and an official manual for the U/E family. The manual documents one-player controls, two title screens, the Start/Option selection, continued play, and high-score loss at power-off. It is gameplay and scenario reference material; it does not identify code or prove the selected ROM's complete hardware behavior. [Official original manual, EU edition](https://www.sega.jp/mdmini2/en/assets/manual/pdf/EU_After-Burner-%E2%85%A1.pdf), [SEGA title page](https://www.sega.jp/mdmini2/soft/after-burner2.html), [SEGA manual catalog](https://manuals.sega.com/genesismini/).

The manual-guided capture reached the Start/Option menu, but a later D-pad-plus-Start probe still left the menu visible, so gameplay entry is unverified. A 600-frame feature trace stopped with the candidate core's `core trace error`; it is inconclusive, not a title failure. A revised Start-to-gameplay scenario remains open. The static screen found a `0x200000` table value, and SRAM access is not measured because the image has no declared SRAM window. These points prevent a completed hardware eligibility claim.

## Versioned scenario inventory

The private versioned title manifest and scenario inventory are under ignored `build/decomp/md-f68818ae1a61/`. Their statuses are explicit:

| Scenario family | D00 requirement for this title | State |
|---|---|---|
| Cold boot | Power-on to title screen and input-ready state; record startup video/audio. | Partial: title screen captured; stable input-ready condition not certified. |
| Menus | First title, Start/Option menu, Start entry, and Option entry/exit. | Partial: Start/Option screen reached; entry into gameplay not yet confirmed. |
| Gameplay | Stage 1 input and representative audio over a declared window. | Unstarted. |
| Failure and retry | Lose a fighter; cover continue choice and documented timeout. | Unstarted. |
| Completion | Finish the run and cover the high-score entry screen. | Unstarted. |
| Audio | Title, menu, option-selected music, stage music, and sound effects. | Unstarted. |
| Controller | Port 1 three-button inputs; port 2 remains unused unless later evidence says otherwise. | Partial: this setup was used in candidate captures; Stage 1 input is not yet confirmed. |
| Save and region | Verify NTSC `U`; confirm no persistent-memory behavior. | Partial: no SRAM header and manual says score is volatile; complete code audit remains open. |

The inventory keeps the milestone tiers separate: D06 first playable, D07 full-game behavior and census, D04/D05 routine certification, and D03/D07 unreachable-code closure are all unstarted. The title screen or a candidate-core run does not satisfy any of those gates.

## Workspace boundary and remaining D00 work

The private workspace uses neutral ID `md-f68818ae1a61` under ignored `build/decomp/`. No separate maintained game repository was supplied or authorized for writes. No ROM path is persisted; all candidate hashes and captures stay private or are represented by neutral IDs in tracked reports.

D00 is partial until the indirect targets, RAM execution, mapper/SRAM behavior, and title-specific Start-to-gameplay scenario are resolved and the scenario inventory is complete. The Ghidra pass was limited to code reachable from each reset vector; its results do not close the full image census.
