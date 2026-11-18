# Session Record, Replay and Three-Way Compare (Plan)

**Status:** P1 and P2 done, 2026-11-17 (nativekit 1.2.0: `--record`, `--frame-log`, `nk_console.held`; `tools/session/session.py` record, marks, verify, extract). The owner settled the open decisions: the proposed keys, a Python-only tool, sound by replay. Gates: `session_check.py` in `kit-check` and `tools/session/session_check.py` (`mise run test:session`), each with its planted errors. On the first title (null host, 1,500 passes with presses, a pause and steps): 1,482 frames recorded and replayed with every picture and state the same, and frames 795-855 extracted, each matching the play. One deviation: no audio hash in the frame log (a windowed run's sound goes to the device from another thread, so a frame's samples cannot be taken without taking them from the device); so a sound-only difference that never reaches the picture or the hashed memories (on the Mega Drive: work, Z80 and VDP memory, not the YM2612/PSG registers) is not caught by verify; extract's WAV comes from the replay itself. P3-P6 not started.

**Goal:** When the owner sees something wrong while playing a native build, they can name the exact frame or frame range, replay that same play deterministically, and get a side-by-side of the same moment from three runs: the native game, the decomp C running inside the reference emulator, and the original on the reference emulator. Each difference then gets a verdict saying where it comes from: the recovered C, nativemd's console implementation, or no fault at all because the native run matches the original.

**First user:** the first Mega Drive title (`md-f68818ae1a61`), whose reported attract-mode graphics and sound glitches could not be reproduced from still frames (title repo `STATUS.md`, "Native, in a window"). The design is console-neutral so that the NES effort (Dragon Warrior, MesenCE captures) and later consoles can use it.

## Global constraints

- Session files hold input, bookmarks, hashes and build identity only, so they carry no ROM bytes. Anything rendered from the cartridge (frames, audio, memory dumps, the viewer page) stays under ignored `build/decomp/<game-id>/sessions/` or the private title repo, and is never committed to nativekit or uploaded. That includes not publishing it as a hosted artifact.
- Default behaviour does not change: a windowed run without `--record` and a headless `--script` run behave as they do today.
- Every new gate catches a planted error (AGENTS.md, `kit/docs/contract-discipline.md`).
- Recording and replay must not change emulated timing or perform extra game bus accesses (D02's observation rule).
- Console-specific concepts stay out of `kit/`: the kit owns the session format, recorder, replay and range extraction, and each console supplies an adapter.

## The core problem: native and reference frames don't line up

Replaying the same per-frame input on the reference does not reproduce the native play. Native timing differs from the reference: native boot runs 14-19 frames ahead, and a scripted Start press already landed in a different place natively and paused the game (`STATUS.md`). So **frame N native is not frame N reference**, and a frame number alone can't identify "the moment where the glitch happened" in all three runs.

The fix is to key input to the moments the game itself reads the pad, rather than to video frames:

- **Poll-indexed input.** The game only sees input when it reads the pad (on this title, `read_pads` ran 1,700 times in 3,000 gameplay frames, so not once per frame). The recorder logs input as "the k-th pad read returned these buttons". The reference replay feeds the k-th read the same value whenever it happens, so both runs see the same input sequence in game-logic order, despite the timing differences. (TAS tools use this idea to handle "lag frames".)
- **Logic ticks for alignment.** A console adapter names an anchor that the game passes through once per game-logic step (for this title, a call to `read_pads` or entry to the per-frame loop `$6870`). Each video frame in each run is stamped with the tick count at that point. "Native frames 1830-1860" then becomes "ticks 1,120-1,138", which each run translates back to its own frame numbers.
- **State check on top.** At each tick, each run records a hash of work RAM. Matching hashes at the same tick confirm the alignment, and the first tick where they differ is where the runs diverged. This repeats the "matched by game state" comparison done so far, but automatically and for every tick.

Per-frame input is still recorded too, because that is what makes the native replay exact.

## Lanes and verdicts

| Lane | What runs | Built from |
|---|---|---|
| **N** native | `md_game`, all C, nativemd's console | the recorded per-frame input, headless replay |
| **H** hybrid | the reference emulator with all C substituted (lockstep build) | poll-indexed input |
| **R** reference | the original code on the reference emulator (patched BlastEm libretro) | poll-indexed input |

For a bookmarked range, compare the lanes tick by tick:

| N vs R | H vs R | Verdict |
|---|---|---|
| same | same | Not a decomp fault: the original does this too (as with the period-0 PSG tone). |
| differs | same | Native platform fault: nativemd's VDP, renderer, sound, input or timing, not the C. |
| differs | differs | C fault: run per-call checks over the range to find the routine. |
| same | differs | Lockstep harness fault: should not happen; it's a bug in the checking tools. |

If game state (the work RAM hash) diverged at an earlier tick, the report says so and names that first tick. The visible glitch may only be a later symptom.

## Phases

Each phase ends with a gate and a planted-error control.

### P1. Record in a windowed run (kit)

- `--record DIR` on a windowed run of any kit console. It writes:
  - `session.json`: build identity (nativekit commit, contract version, title repo commit, cost-profile hash), ROM SHA-256, console options and start time.
  - `input.nks`: per-frame pad masks in the existing headless script format, so `--script` replays it unchanged. Long runs of the same mask are written once with a frame count.
  - `frames.bin`: one record per frame with frame number, video hash, audio hash and work-RAM hash, used later to confirm that replay is exact.
- A frame counter overlay toggled by a key (proposed: F1), shown in the corner and in the window title, so the owner can read frame numbers while playing.
- Bookmarks without leaving the game:
  - F5 marks a point.
  - F6 starts a range and F7 ends it.
  - Each mark is saved with its frame number and wall time to `marks.json`.
  - Notes are added afterwards in the viewer (P5), so typing doesn't interrupt play.
- Pause (F9) and frame step (F10) while recording, with the paused frames recorded as such, so the owner can stop on a glitch and step through it.
- **Gate:** a headless replay of `input.nks` reproduces `frames.bin` exactly for every frame of a recorded session (synthetic cartridge plus the first title).
- **Planted error:** a recorder that drops one input change, or reads a host-clock value into emulated state, fails the gate at the first affected frame.

### P2. Extract a frame range (kit)

- Script steps over a range: `shots A-B`, `sound A-B` (WAV) and `dumps A-B NAMES`. The console adapter names the regions; for MD: work RAM, VRAM, CRAM, VSRAM, VDP registers and the sprite table.
- `nk-session extract DIR --range A-B [--pad K]` replays the session headless and writes PNG frames, a WAV clip and per-frame dumps for the range plus K frames of context on each side.
- **Gate:** extracting the same range twice gives identical files, and an extracted frame's hash equals the recorded `frames.bin` entry.

### P3. Poll-indexed input and logic ticks (console adapter)

- Kit interface: the console reports pad reads (`nk_session_poll(player, mask)`) and anchor ticks (`nk_session_tick()`) to the recorder. Native records `polls.bin` (k → mask) and `ticks.bin` (frame → tick count, work-RAM hash at each tick).
- MD adapter:
  - The pad read is reported at the controller port, when the game reads the data port (the TH-select read sequence). Reporting at the port, rather than in `read_pads`, means titles without recovered C work too.
  - The anchor is a per-title setting in the title repo (an address, e.g. `read_pads` or `$6870`), with a port-read fallback when no address is set.
- Reference side (`consoles/md/tools/decomp`): the scenario gets an `input_by_poll` form. `run.py` and `lockstep.py` answer the k-th port read from `polls.bin` (through the libretro input callbacks plus a port-read hook on the patched core) and stamp ticks with the same anchor.
- **Gate:** on a synthetic cartridge whose pad reads drift in time against frames, frame-indexed replay on the reference diverges, while poll-indexed replay has matching work-RAM hashes at every tick.
- **Planted error:** shifting the poll index by one is caught at the first affected tick.
- **Known limit:** if native and reference differ in how many times the game reads the pad between ticks (a timing-dependent read loop), the poll counts drift apart. The report states the first tick where that happens and doesn't guess past it.

### P4. Three-way compare and triage (MD first)

- `session.py compare DIR --range A-B` runs these and writes a report under `build/decomp/<id>/sessions/<name>/`:
  1. Map native frames A-B to ticks.
  2. Run R and H for those ticks plus context. Old ticks are replayed rather than stored: the reference is deterministic, and frames can be re-rendered from the session file at any time.
  3. For each tick in the range, compare the three lanes:
     - picture: pixel diff and diff image
     - VDP state: CRAM, VRAM tiles touched, sprite table rows, scroll
     - work RAM, with variable names from the title repo's `names.json`
     - audio over the matching time span
  4. Assign the verdict from the table above.
  5. On "C fault", run `check_calls.py` per call over the range's frames (`run.py --window FIRST:LAST` already limits the capture) and list the DIVERGENT routines with their first differing call.
  6. Name the first tick where any lane's state diverged before the range starts.
- **Gate:** three planted controls on the first title's attract run each produce their own verdict:
  - a one-instruction change in a drafted routine → C fault, and that routine is named
  - a planted nativemd VDP error (one CRAM entry) → native platform fault
  - an unmodified run → not a decomp fault

### P5. Viewer (local only)

- A static HTML page written next to the report and opened from disk. It is never published, because it contains cartridge-derived images.
- It shows the three lanes side by side with a frame/tick scrubber, a diff overlay toggle, an audio waveform for the range, the bookmarks with editable notes (saved back to `marks.json` through a small local command, or copied out as text), and the verdict and divergence list for each mark.
- A "make issue note" button writes a Markdown summary with no images (range, ticks, verdict, routines, variable diffs) that can go into the title repo's notes or a nativekit issue.

### P6. Other consoles

- The kit pieces (P1, P2, the P3 interface and the compare core in `tools/session/`) are console-neutral. A console adds:
  - a pad-read report
  - a tick anchor
  - named dump regions
  - a reference-runner adapter
- NES next: the reference runner is the existing MesenCE headless capture (`mise run capture:nes`), the pad read is the `$4016`/`$4017` strobe-and-shift sequence, and the private `../dragon-warrior-nes` repo supplies the anchor. SNES can adapt its existing reference comparisons after that.
- **Gate:** the NES synthetic cartridge passes the P3 gate through the same `tools/session` code with only the adapter added.

## Order and size

| Phase | Depends on | Rough size | Useful on its own |
|---|---|---|---|
| P1 record | — | small | yes: exact replays and frame numbers for bug reports |
| P2 extract | P1 | small | yes: PNG and WAV of a named range, native only |
| P3 poll input and ticks | P1 | medium (patched-core hook) | enables the reference lanes |
| P4 compare and triage | P2, P3 | medium | the verdicts |
| P5 viewer | P4 | small to medium | review comfort |
| P6 NES | P1-P4 | medium | cross-console |

P1 and P2 alone already replace screen recording: the owner plays with the overlay on, marks glitches, and gets exact native frames and audio for any range. P3 and P4 add the reference comparison.

## Decisions for the owner

1. Bookmark keys: F1 overlay, F5 mark, F6/F7 range, F9 pause, F10 step. These are not used by the MD keyboard mapping; any objection?
2. Where the session tool lives: the kit CLI (`nk-session`, C plus Python), or Python only under `tools/session/` with the native binary used only for headless replay. Proposed: Python only, because replay is just `md_game --headless --script`.
3. Should the recorder record audio to disk directly as well (larger files, but no replay needed just to listen), or rely on P2 replay? Proposed: replay only, since the session is exact by the P1 gate.

## Ties to the decomp milestones

- D06/D10: gives the "human gameplay review" verdict a reproducible form. A review note names ticks and comes with a replayable session.
- D07: recorded sessions become new scenarios for coverage, because a session's poll-indexed input is a valid reference scenario.
- D12: the workflow is part of the reusable ROM-to-decomp playbook for the second title (`md-48b280520c4f`).
