# nativemd Light Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A reference capture that keeps only what the code map uses, so a title's traced evidence costs megabytes and one run instead of hundreds of gigabytes and a second multi-hour read, with the same code map.

**Architecture:** `codemap.build` already reduces a capture to three sets (traced ROM addresses, ROM flow edges, ROM reads) and a sequence of RAM program generations. That reduction moves into an `Ingest` class that the capture worker runs as events arrive; its result is written as a small evidence digest (`capture.nmde`) that the intake reads in place of the event stream. A full event stream is written only for a requested frame window, and any capture can be re-run from its scenario because the core is deterministic.

**Tech Stack:** Python 3 standard library only (`ctypes`, `hashlib`, `json`, `struct`, `zlib`), the patched BlastEm libretro core (patches 0001-0004), vasm for authored fixtures. No new dependencies.

**Spec:** this file's [Design](#design) section, which settles what [intake.md](../../../consoles/md/docs/decomp/intake.md#first-title) left open under "First title" and "Not done". The milestone contract is [D03](2026-11-04-nativemd-decomp/d03-lossless-intake-and-code-census.md) and the [shared execution contract](2026-11-04-nativemd-decomp/README.md#shared-execution-contract).

## Global Constraints

- No ROM bytes, recovered code, assets, snapshots or private ROM paths are committed. Title work stays under ignored `build/decomp/<game-id>/`; public text uses hashes and neutral IDs.
- Fixtures are authored cartridges assembled at test time. Every new gate has a planted error that it catches.
- Default behaviour does not change: `run.py` without `--evidence` writes the same `capture.nmdh` and `capture.nmdt` as today, and NMDH v1 files written before this plan still read.
- A code map built from a digest is byte-identical (`codemap.json`) to the one built from the full event stream of the same run. This is the acceptance measure everywhere below.
- Python follows the existing files: four-space indentation, snake_case, no formatter. Checks are `*_check.py` scripts registered in `consoles/md/tests/decomp_check.py` and run by `mise run test:md-decomp`.
- Commits use `nativemd: ...` or `Docs: ...` subjects. Nothing is pushed without the user's say.

## Design

### What the first title showed

Four full captures (402 GB of events, 4.5 hours to take, 5.6 hours to read twice) decode 22,648 ROM bytes; the last 142 GB added 110. An event costs about 330 bytes as canonical JSON and the code map uses three or four integers of it, and only the first time it sees them.

### What the code map uses

`codemap.build` reads NMDH kinds 1 (instruction), 2 (flow) and 3 (bus) and nothing else:

| Event | Fields read | Effect |
|---|---|---|
| instruction, 68000, `pc` below the ROM size | `pc` | added to a set |
| instruction in work RAM (68000) or Z80 RAM | `pc` | marks the open RAM generation |
| flow | `previous_pc`, `current_pc` | added to a set (ROM) or to the open generation |
| bus write into work RAM or Z80 RAM | `address`, `width`, `value` | changes RAM; closes the generation when it overwrites executed code |
| bus read of ROM by the 68000, `provenance` not 1 | `address`, `width` | added to a set |

So a whole capture reduces, exactly, to: the three sets, and for each CPU the generations in the order they closed, each one a RAM image, the instructions that ran in it and the flow edges seen in it.

### Decisions

1. **The digest is the reduction's output, not a filtered event stream.** A filtered NMDH stream would still need every RAM write. The digest stores a generation as the bytes that differ from the previous generation of the same CPU, so the first title's 1.4 million Z80 generations cost their rewritten operands, not 1.4 million RAM images.
2. **One reduction, two callers.** `Ingest` is the only code that turns events into sets and generations. The intake feeds it from an NMDH file; the capture worker feeds it from the core's callback. Equality of the two code maps then rests on the digest round trip alone, and a check holds that.
3. **Light is opt-in.** `--evidence full` stays the default. D04 and later milestones compare instruction state, which the digest does not hold; they get it from a window.
4. **A window is a full NMDH stream for frames FIRST..LAST of a light run**, with the core's own sequence numbers. It is never evidence for the code map: its RAM does not start from power-on.
5. **A run is identified by its event records, not its file.** Every capture's manifest records `nmdh_records_sha256`: the SHA-256 of every record as NMDH frames it (length prefix and canonical JSON), without the header and footer, which hold the core and runner identities. Two runs with equal record digests saw the same events whichever core build or runner revision made them. A light run computes it only when asked (`--records-digest`), because serialising every event is most of a capture's time.
6. **Out of scope: static discovery of jump tables.** It is the other route to code that play does not reach, it shares no code with this plan, and its design depends on the shapes of the first title's 14 computed transfers. It gets its own plan.

### First-title baseline

The four full captures are deleted once this plan is written (they are 376 GB on a nearly full disk). What the plan's last task compares against is kept in the private workspace `build/decomp/md-f68818ae1a61/baseline/`:

- `manifests/<capture>.json`: each capture's manifest (frame count, event counts, video and audio hashes, scenario hash).
- `nmdh-records.json`: each capture's event count and record digest, computed from the files before deletion.
- `streams.sha256`: whole-file hashes and sizes.
- `intake-all4/`: `codemap.json`, `census.json` and `catalog.json` of the four-capture intake, and `codemap.sha256`.

The scenarios (`scenarios/{play-3000,idle-3100,over-4700,long-5800}.json`) stay where they are. Three of the four were captured on the core before patch 0004; they end below frame 4,783, where that patch first acts, so the patched core must reproduce their record digests. Task 6 tests that claim instead of assuming it.

## File Structure

| File | Responsibility |
|---|---|
| `consoles/md/tools/decomp/codemap.py` (modify) | `Ingest`: events to sets and generations. `build`: evidence sets to spaces. `load_evidence`: picks the stream or the digest. |
| `consoles/md/tools/decomp/evidence.py` (create) | NMDE v1: `DigestWriter`, `read_digest`, `Digest`. Framing, canonical JSON, footer digest, fail-closed reading. Knows nothing about decoding. |
| `consoles/md/tools/decomp/trace.py` (modify) | `records_digest` support in `NMDHWriter`; a window's first sequence number in `_scan`. |
| `consoles/md/tools/decomp/intake.py` (modify) | `digest` subcommand: a full capture to a digest, offline. |
| `consoles/md/tools/decomp/run.py` (modify) | `--evidence`, `--records-digest`, `--window`; the light worker path. |
| `consoles/md/tests/decomp/evidence_check.py` (create) | Digest format mutations, round trip, stream-against-digest code map equality, planted errors. |
| `consoles/md/tests/decomp/light_check.py` (create) | Actual-core light capture against a full capture of the same fixture; window; refusals. |
| `consoles/md/tests/decomp/runner_check.py`, `format_check.py`, `consoles/md/tests/decomp_check.py` (modify) | New refusals; window framing; registration. |
| `consoles/md/docs/decomp/intake.md`, `consoles/md/tests/README.md` (modify) | Digest schema, credit rules, first-title light results. |

## Review Focus

1. **A light capture that stops early** (time cap, byte cap, core error): no `capture.nmde` exists, the manifest status is not `complete`, and `load_evidence` refuses the directory. Pinned in Task 4.
2. **A digest that was edited, truncated or made from another ROM**: refused with a named reason before any of it reaches the code map. Pinned in Tasks 2 and 3.
3. **A window given to the intake as evidence**: refused; a window's RAM does not start from power-on. Pinned in Task 5.
4. **68000 code in work RAM**: a 64 KB image whose generations differ in a few bytes round-trips through the digest. No authored cartridge runs RAM code on the 68000, so Task 2 pins it with constructed events.
5. **The same run given twice, or a stream and its own digest together**: the code map equals the one from giving it once, except each RAM program's `occurrences`, which doubles as it does today for a repeated stream. Pinned in Task 3.

---

### Task 1: `Ingest` carries the reduction out of `build`

A refactor with no change in output.

**Files:**
- Modify: `consoles/md/tools/decomp/codemap.py` (`_Ram`, `_Evidence`, `build`, lines 341-505)
- Test: `consoles/md/tests/decomp/census_check.py`, `consoles/md/tests/decomp/identity_check.py` (unchanged, must still pass); `consoles/md/tests/decomp/evidence_check.py` (create, first case)

**Interfaces:**
- Produces:
  ```python
  class Ingest:
      def __init__(self, rom_size, on_generation=None): ...
      traced_rom: set      # 68000 pcs below rom_size
      rom_edges: set       # (previous_pc, current_pc), both masked to 24 bits
      reads: set           # (address, width)
      def begin(self): ...     # a trace starts from power-on: close open generations, zero both RAMs
      def instruction(self, cpu, pc): ...
      def flow(self, cpu, previous_pc, current_pc): ...
      def bus(self, cpu, address, width, value, operation, provenance): ...
      def feed(self, event): ...   # an NMDH event dict; kinds other than 1, 2, 3 are ignored
      def end(self): ...       # close open generations
      def merge(self, traced_rom, rom_edges, reads): ...   # union into the three sets
  ```
  `cpu` is `"m68k"` or `"z80"`. `on_generation(cpu, snapshot, executed, edges)` is called once per closed generation, in closing order: `snapshot` is `bytes` of the whole RAM, `executed` a dict of offset to length, `edges` a set of pairs.
- `_Ram` loses `families` and takes the callback; `build` owns `families = {"m68k": [], "z80": []}` and admits each generation as it arrives, exactly as `_Ram.close` does now.
- Every evidence set gains `replay(ingest)`. `_Evidence.replay` calls `begin`, `feed` per event, `end`, and keeps its "holds no instruction events" refusal. `build(rom, evidence_sets=())` keeps its signature and calls `replay` on each set.

- [ ] **Step 1: Write the failing test.** In `evidence_check.py`, `ingest_matches_build(temp)`: build the census fixture and its capture (`census_fixture.build`, `census_fixture.capture`), run `codemap.build` on it, then feed the same events to a bare `codemap.Ingest(len(rom), collect)` and require `ingest.traced_rom`, `rom_edges` and `reads` to be non-empty and the collected generations to number the sum of `occurrences` over the build's RAM spaces.
- [ ] **Step 2: Run it.** `python3 consoles/md/tests/decomp/evidence_check.py`. Expected: fails with `module 'codemap' has no attribute 'Ingest'`.
- [ ] **Step 3: Move the body of `build`'s event loop into `Ingest`**, one method per event kind, with `feed` dispatching on `event["kind"]`. Keep every mask and bound as written.
- [ ] **Step 4: Run** `python3 consoles/md/tests/decomp/evidence_check.py`, `census_check.py` and `identity_check.py`. Expected: all three print `ok`.
- [ ] **Step 5: Commit** `nativemd: the code map's event reduction is its own class`.

### Task 2: NMDE v1, the evidence digest

**Files:**
- Create: `consoles/md/tools/decomp/evidence.py`
- Test: `consoles/md/tests/decomp/evidence_check.py`

**Interfaces:**
- Consumes: `trace.canonical_json`, `trace._validate_metadata`, `trace.STATUS_CODES`.
- Produces:
  ```python
  MAGIC = b"NMDE"; VERSION = 1; FOOTER_MAGIC = b"NEEN"
  class DigestError(ValueError): ...
  class DigestWriter:
      def __init__(self, path, metadata, max_bytes): ...   # writes path + ".partial"
      def generation(self, cpu, snapshot, executed, edges): ...
      def finish(self, traced_rom, rom_edges, reads): ...  # final record, footer, rename; returns path
      def abort(self, reason): ...                          # footer with that status, no rename
  class Digest:
      metadata: dict
      traced_rom: set; rom_edges: set; reads: set
      def generations(self): ...   # yields (cpu, snapshot, executed, edges) in file order
  def read_digest(path): ...       # validates the whole file first; returns Digest or raises DigestError
  ```
- File layout, little-endian: the NMDH header struct (`<4sHHI`: magic, version, flags 0, metadata length) and canonical-JSON metadata under NMDH's metadata rules; then records, each a `<I` length and a zlib stream (level 6) of canonical JSON; then the NMDH footer struct (`<4sQB3s32s`: magic, record count, status, padding, SHA-256 of header and records).
- Generation record: `{"cpu": "z80", "delta": [[offset, "hex"], ...], "executed": [[offset, length], ...], "edges": [[from, to], ...]}`, every list sorted. `delta` holds the maximal runs of bytes that differ from the previous generation record of the same CPU, or from all zeros for the first.
- The last record, and only the last: `{"traced_rom": [...], "rom_edges": [[from, to], ...], "reads": [[address, width], ...]}`, sorted. RAM sizes are `codemap.WORK_RAM_SIZE` and `codemap.Z80_RAM_SIZE`.
- Limits: a record's compressed length is at most 1 MiB; `max_bytes` bounds the file as `NMDHWriter` does, with the same `CaptureLimitError`.

- [ ] **Step 1: Write the failing tests** in `evidence_check.py`:
  - `digest_round_trip(temp)`: write two Z80 generations that differ in 2 bytes and three work-RAM generations of a 64 KB image that differ in 5 bytes (Review Focus 4), plus sets; read back; require equal snapshots, `executed`, `edges` and sets, and a file under 4 KB.
  - `digest_mutations(temp)`: each of these is refused with a message naming it: one flipped byte in a record; a record removed; the footer's count changed; bytes after the footer; a record that is valid zlib but not canonical JSON; a generation record after the sets record; a `delta` run past the RAM size; a file with status `timeout`; metadata holding a local path.
  - `digest_byte_cap(temp)`: a writer with `max_bytes` 200 raises `CaptureLimitError` and leaves no file at `path`.
- [ ] **Step 2: Run.** Expected: fails with `No module named 'evidence'`.
- [ ] **Step 3: Implement `evidence.py`** to the layout above. `read_digest` makes one validating pass that also checks every record's shape, then `generations()` re-reads lazily.
- [ ] **Step 4: Run** `python3 consoles/md/tests/decomp/evidence_check.py`. Expected: `ok`. Then break the footer-digest comparison by hand and confirm `digest_mutations` fails; restore it.
- [ ] **Step 5: Commit** `nativemd: NMDE v1 evidence digest, writer and fail-closed reader`.

### Task 3: The intake reads digests and makes them from full captures

**Files:**
- Modify: `consoles/md/tools/decomp/codemap.py` (`load_evidence`), `consoles/md/tools/decomp/intake.py` (`main`, new `run_digest`)
- Test: `consoles/md/tests/decomp/evidence_check.py`; register it in `consoles/md/tests/decomp_check.py` after `identity_check.py` as "evidence digest framing, round trip and code map equality"

**Interfaces:**
- Consumes: `Ingest` (Task 1), `DigestWriter`, `read_digest` (Task 2).
- Produces:
  - `codemap._DigestEvidence(path)` with `replay(ingest)`: `ingest.begin()`, `ingest.merge(...)`, then the ingest's `on_generation` for each generation in order.
  - `load_evidence(capture_directory, rom_sha256)`: when the manifest has `"evidence": "light"` it returns `_DigestEvidence(directory / "capture.nmde")` after requiring the digest's own `rom_sha256` to match; a manifest with a `"window"` key and no digest is refused with "a window is not evidence for a code map"; otherwise as now.
  - `intake.py digest --rom ROM --capture DIR --out DIR`: reads `DIR/capture.nmdh` once through `Ingest`, writes `OUT/capture.nmde` and `OUT/manifest.json`: the source manifest with `"evidence": "light"`, `"outputs": ["capture.nmde"]`, `"nmde_bytes"`, and `"digest_of": {"nmdh_footer_sha256": ...}`. Prints one line with the event count and both sizes.

- [ ] **Step 1: Write the failing tests** in `evidence_check.py`:
  - `stream_and_digest_agree(temp)`: for the census fixture and for the `identity_check` fixture (import its builder), run `intake.py digest`, then `intake.run_intake` once on the full capture and once on the digest directory; require the two `codemap.json` files to be byte-identical and both rebuilds identical.
  - `planted_digest_errors(temp)`: removing one address from the digest's `traced_rom` (rewrite the file through `DigestWriter`) changes `codemap.json`; a digest directory given with a ROM of another hash is refused with "capture was made from a different ROM".
  - `repeats(temp)`: the intake over [stream, digest of the same stream] equals the intake over [stream, stream] (Review Focus 5).
- [ ] **Step 2: Run.** Expected: fails with `invalid choice: 'digest'`.
- [ ] **Step 3: Implement** `_DigestEvidence`, the `load_evidence` branch and the `digest` subcommand.
- [ ] **Step 4: Run** `python3 consoles/md/tests/decomp/evidence_check.py` and `mise run test:md-decomp`. Expected: `ok`; `decomp integration checks: ok`.
- [ ] **Step 5: Commit** `nativemd: the intake reads an evidence digest in place of a capture's events`.

### Task 4: Light capture

**Files:**
- Modify: `consoles/md/tools/decomp/trace.py` (`NMDHWriter`), `consoles/md/tools/decomp/run.py` (`_worker` lines 578-1059, `_run_parent`, `main`)
- Create: `consoles/md/tests/decomp/light_check.py`; register it after `evidence_check.py` as "actual light capture against a full capture, and its refusals"
- Test also: `consoles/md/tests/decomp/runner_check.py`, `consoles/md/tests/decomp/format_check.py`

**Interfaces:**
- Consumes: `codemap.Ingest`, `evidence.DigestWriter`.
- Produces:
  - `NMDHWriter.records_sha256` (property, hex): SHA-256 of every record written so far, length prefix included, header excluded. `NMDHWriter(path, metadata, max_bytes, store=True)`: with `store=False` nothing is written or counted against `max_bytes`, and `finish` returns `None`.
  - `run.py --evidence {full,light}` (default `full`), `--records-digest` (flag).
  - Every complete manifest gains `"evidence"` and, in full mode or with `--records-digest`, `"nmdh_records_sha256"`. A light manifest has `"format": "NMDE v1 capture"`, `"outputs": ["capture.nmde"]`, `"nmde_bytes"`, `"nmdh_bytes": 0`, `"nmdt_bytes": 0`, `"stream_bytes"` equal to `nmde_bytes`, and the same `events`, `event_counts`, `instruction_counts`, video and audio fields as a full one.
  - An estimate records `"evidence"`; a capture refuses an estimate made in the other mode with "preflight estimate evidence mode mismatch". A light estimate's `estimated_stream_bytes` is `{"nmde": N}` and the half-cap rule does not apply to it.
- Light worker path: the callback reads `pc`, the flow pair or the bus fields straight from the ctypes event and calls the matching `Ingest` method; it builds `_event_dict` only when `--records-digest` is set. NMDT records are counted, not written. `DigestWriter` gets the whole `--max-bytes`.
- Refused before the core loads, in `_run_parent`: `--evidence light` with a stop criterion other than `frame_limit` ("light evidence needs a frame_limit stop"), and with a scenario holding `checkpoint_test`.

- [ ] **Step 1: Write the failing tests.**
  - `light_check.py`, `light_equals_full(temp)`: capture the census fixture full, and light with `--records-digest`. Require equal `events`, `event_counts`, `instruction_counts`, `video_sha256`, `audio_sha256` and `nmdh_records_sha256`; no `capture.nmdh` or `capture.nmdt` in the light directory; `capture.nmde` under 64 KB; the two intakes' `codemap.json` byte-identical. `census_fixture.capture` gains a keyword `extra=()` passed to both runner calls.
  - `light_check.py`, `light_without_digest(temp)`: a light capture without `--records-digest` has no `nmdh_records_sha256` key and the same `codemap.json`.
  - `light_check.py`, `light_stops_early(temp)` (Review Focus 1): a light capture with `--max-bytes 600` ends `incomplete_storage_limit`, leaves no `capture.nmde`, and `codemap.load_evidence` refuses the directory.
  - `runner_check.py`: the two refusals above, each before the output directory exists; the estimate-mode mismatch.
  - `format_check.py`: `NMDHWriter(..., store=False)` over the format fixture's events gives the `records_sha256` of a stored writer and creates no file.
- [ ] **Step 2: Run** `python3 consoles/md/tests/decomp/light_check.py`. Expected: fails with `unrecognized arguments: --evidence light`.
- [ ] **Step 3: Implement** the writer change, then the worker branch, then the parent's checks.
- [ ] **Step 4: Run** `mise run test:md-decomp`. Expected: `decomp integration checks: ok`. Then make the light callback skip flow events by hand and confirm `light_equals_full` fails on `codemap.json`; restore it.
- [ ] **Step 5: Commit** `nativemd: a light capture writes the evidence digest instead of the event streams`.

### Task 5: A full window inside a light run

**Files:**
- Modify: `consoles/md/tools/decomp/trace.py` (`_validate_metadata`, `_scan`, `NMDHWriter.write_event`), `consoles/md/tools/decomp/run.py`
- Test: `consoles/md/tests/decomp/light_check.py`, `consoles/md/tests/decomp/format_check.py`, `consoles/md/tests/decomp/runner_check.py`

**Interfaces:**
- Produces:
  - `run.py --window FIRST:LAST`: 1-based inclusive frames, requires `--evidence light`, `1 <= FIRST <= LAST <= --frames`. Writes `window.nmdh` holding every event the core emits during `retro_run` calls FIRST to LAST, with the core's sequence numbers, capped at half of `--max-bytes`. The manifest gains `"window": {"first_frame", "last_frame", "first_sequence", "events", "nmdh_bytes"}` and lists `window.nmdh` in `outputs`.
  - NMDH metadata may hold `"window": {"first_frame": int, "last_frame": int, "first_sequence": int}`. `_scan` and `NMDHWriter` then expect the first record's sequence to be `first_sequence` instead of 1. Any other shape of `window` is "invalid NMDH window metadata". Files without the key read as before.
  - `first_sequence` is known only when the window opens, so the writer for `window.nmdh` is created at the first event of frame FIRST.

- [ ] **Step 1: Write the failing tests.**
  - `light_check.py`, `window_matches_full(temp)`: a full capture of the census fixture for 3 frames and a light one with `--window 2:2`. Require the window's records to equal, record for record, the full capture's records with sequence numbers from `first_sequence` to `first_sequence + events - 1`, and its last record to precede the full capture's first frame-3 record. Frame boundaries in the full capture come from a second light run with `--window 3:3`: its `first_sequence` is where frame 3 starts.
  - `light_check.py`, `window_is_not_evidence(temp)` (Review Focus 3): a directory holding only `window.nmdh` and a manifest with the `window` key is refused by `codemap.load_evidence` with "a window is not evidence for a code map"; the light capture directory itself (digest and window together) is accepted and gives the digest's code map.
  - `format_check.py`: a window file whose first sequence is not `first_sequence` is refused; `first_sequence` 0 is refused; a v1 file without the key still reads.
  - `runner_check.py`: `--window` without `--evidence light`, `--window 0:2`, `--window 3:2` and `--window 2:9` with `--frames 3` are each refused before the output exists.
- [ ] **Step 2: Run.** Expected: fails with `unrecognized arguments: --window`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `mise run test:md-decomp`. Expected: `decomp integration checks: ok`.
- [ ] **Step 5: Commit** `nativemd: a light capture can keep the full events of a frame window`.

### Task 6: First-title baseline check, results and docs

Private workspace work plus tracked docs. The ROM path comes from the environment and is not written down.

**Files:**
- Create (ignored): `build/decomp/md-f68818ae1a61/run-light.sh`, `light/<scenario>/`, `intake-light/`
- Modify: `consoles/md/docs/decomp/intake.md`, `consoles/md/tests/README.md`, `docs/superpowers/plans/2026-11-04-nativemd-decomp/d03-lossless-intake-and-code-census.md`, `build/decomp/md-f68818ae1a61/WORKSPACE.md`

**Interfaces:**
- Consumes: `run.py --evidence light --records-digest` (Task 4), `intake.py intake` over digest directories (Task 3), the baseline files listed under [First-title baseline](#first-title-baseline).

- [ ] **Step 1: Write `run-light.sh`**: for each of `play-3000:3000`, `idle-3100:3100`, `over-4700:4700`, `long-5800:5800`, an estimate and a capture with `--evidence light --records-digest --max-bytes 4000000000 --max-seconds 12000` into `light/<name>/`; then `/usr/bin/time -l intake.py intake` over the four directories in that order into `intake-light/`, then `rebuild` and `census`.
- [ ] **Step 2: Run it** in the background. Expected: four `decomp-run: capture complete` lines and `rebuild: identical`.
- [ ] **Step 3: Compare with the baseline.** For each capture: `events`, `video_sha256`, `audio_sha256` equal `baseline/manifests/<name>.json`, and `nmdh_records_sha256` equals `baseline/nmdh-records.json`. Then `shasum -a 256 intake-light/codemap.json` equals `baseline/intake-all4/codemap.sha256`. Expected: every comparison equal. A record digest that differs for one of the three pre-0004 captures is a finding about patch 0004, not a failure of this plan: record it in intake.md and compare that capture's code map separately by re-running it with `--evidence full` (about 70-110 GB and an hour each; check free space first).
- [ ] **Step 4: Time a light run without the record digest**: `long-5800` again with `--evidence light` only, into `light/long-5800-fast/`. Record its seconds and `nmde_bytes`, and that its code map equals the one from Step 2.
- [ ] **Step 5: Update the docs.** intake.md: an "Evidence digest" section (the layout from Task 2, the equality rule, what a window is and is not), a results table of full against light for the four captures (bytes, capture seconds, intake seconds, peak memory, code map equal), and the "Not done" entry replaced by what remains (static jump-table discovery; a window's use by D04). `tests/README.md`: one line each for `evidence_check.py` and `light_check.py`. D03: one clause in "Open" with the measured figures. WORKSPACE.md: the commands and where the outputs are.
- [ ] **Step 6: Run** `mise run test:md-decomp`. Expected: `decomp integration checks: ok`.
- [ ] **Step 7: Commit** `Docs: first-title light capture results against the full-capture baseline`.

## Self-review notes

- Coverage: decisions 1-2 are Tasks 1-3, decision 3 and 5 Task 4, decision 4 Task 5, the baseline Task 6; decision 6 is deliberately no task.
- The estimate is kept in light mode because the time cap still needs it; its byte figure is the digest's.
- `Ingest`'s method names and `on_generation`'s argument order are the same in Tasks 1-4; `DigestWriter.generation` takes the same four arguments so it can be passed as the callback directly.
