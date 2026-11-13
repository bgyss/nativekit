# D01 synthetic capture resource budget

**Status:** The 30-frame authored synthetic capture completed within the
current runner limits. This measures the pinned candidate test core and runner;
it is not a retail-title estimate or hardware qualification.

## Command and workload

Run from the repository root after building the pinned test tools:

```sh
mise run tools:md
python3 consoles/md/tools/decomp/benchmark.py --frames 30
```

The benchmark writes its estimate, NMDH/NMDT captures, capture manifest, and
JSON report under ignored `build/decomp/synthetic-nmdh/`. The measured ROM is
authored by `consoles/md/tests/decomp/fixtures.py`: a 68000 DIVU/DBRA delay and
loop runs for 30 frames while the Z80 runs a short DJNZ loop, then remains held
by BUSREQ. Both CPU paths execute real instructions in the same machine.

The existing bus fixture projected **663,006,820 bytes** for 30 frames, so the
benchmark uses this lower-event synthetic workload to fit the declared capture
cap. The result describes this bounded workload; it is not a worst-case title
projection.

## Measured result

| Metric | Result |
|---|---:|
| Frames | 30 |
| NMDH events | 155,472 |
| NMDH instruction events | 51,715 |
| NMDH flow events | 51,715 |
| NMDH bus intervals | 52,040 |
| NMDH reset events | 2 |
| NMDT v2 records | 52,040 |
| Event rate | 70,954.4 events/s |
| Total trace bytes | 55,403,973 (52.83 MiB) |
| NMDH / NMDT bytes | 54,363,145 / 1,040,828 |
| Bytes per frame | 1,846,799.1 |
| Capture worker elapsed | 2.191 s |
| Capture wall time | 2.356 s |
| Outer-runner overhead | 0.165 s |
| Peak capture-worker RSS | 40,992,768 bytes (39.1 MiB) |
| 420-second limit | Passed |
| 256-MiB total / 128-MiB per-stream limits | Passed |

Outer-runner overhead is `capture_wall_seconds - worker_elapsed_seconds`; it
measures process launch, supervision and report handling outside the capture
worker. It excludes event callback and stream-writing time, which is included
in the worker elapsed time. Peak RSS is the streaming worker's `ru_maxrss`,
normalized to bytes; the parent supervisor's memory is not included.

## Input identities

| Input | Identity |
|---|---|
| Synthetic ROM SHA-256 | `e8f5dad0c1c9807de4850c34b4290f346f711289a79992ff4ae61a5d1587f16d` |
| Scenario SHA-256 | `69bbaf1ae2a5db2ca4295f1d635bd07838f3267e77e4a3c4f0548f4501cc9dd2` |
| BlastEm revision | `2230addc4dd74dd094fd264987fd808abba32d0e` |
| Candidate core SHA-256 | `84fdda73cd1748c3d816c5ef6ace1233ece5c7373d8cc886ee3f682f15674868` |
| Patch-set SHA-256 | `52e2a470e58b0fda97e7ea2cd4b78773057ed62357a8277596c80196f93395f9` |
| Candidate patch/source stamps | `2eb67f47275498f3e13229d2930cf702c10c7916` / `144653790d51cb6ef8a50879c4c44f9e0e8edc15` |
| ABI SHA-256 | `0ffa98c2250aee10666824580cff994984a84912b3d946a01063f36379a7073f` |
| Runner SHA-256 | `84fed6a61aba05a577b528e0232afad9881403e9abab0eb37e09587ee650c326` |
| Fixture source SHA-256 | `8a63c7b8d1f55a6c8906518ce1b3ed7d33e13ad8726b923fc7329421bed02601` |
| Benchmark source SHA-256 | `d20ac1faaa41195d28dbc09d0bdc61a80a0e205ff8836f67022b041b6e09dbb3` |
| vasm revision | `685a87e5ed14285350ccdb6581c9771bc8df6c7d` |
| 68000 / Z80 assembler SHA-256 | `bb73cd220a7420a80598a0efae20b73486b2203b053cd01026996173360b876d` / `4dcd90ab81e81a8f436007db6331d6fd3cc8e75462bf84344cca767242004ce0` |

The capture manifest and machine-readable benchmark report remain in ignored
`build/decomp/synthetic-nmdh/`; the tracked report records hashes only.
