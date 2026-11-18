# Sessions: record a play, replay it, pull out frames

`session.py` records a play of any nativekit game, checks that it replays
exactly, and pulls out a range of frames as pictures, sound and memory. It
covers phases P1 and P2 of
[the plan](../../docs/superpowers/plans/2026-11-17-session-record-replay.md).
The comparison with the reference emulator (P3, P4) comes next.

Python standard library only. The kit side is `--record` and `--frame-log`
([kit.md, Additions in 1.2](../../kit/docs/kit.md#additions-in-12)).

## Record

```sh
MD_ROM=/path/to/game.gen python3 tools/session/session.py record \
    --out build/decomp/md-f68818ae1a61/sessions/2026-11-17-attract \
    --env MD_ROM --source ~/src/md-f68818ae1a61 \
    -- ~/src/md-f68818ae1a61/build-macos/md_game
```

This copies the binary into the session, notes its SHA-256, the commits of
nativekit and each `--source` repository, and each `--env` variable (with the
SHA-256 of the file it names), then runs the copy in a window. Play normally.

| Key | While recording |
|---|---|
| F1 | show or hide the frame counter (top left, shown at first) |
| F5 | mark the frame shown |
| F6, then F7 | a range from the frame shown at F6 to the one shown at F7 |
| F9 | pause or go on (no frame runs while paused) |
| F10 | while paused, run one frame |

On a Mac keyboard the F keys may need fn. Close the window to end the play.

## Look at the marks

```sh
python3 tools/session/session.py marks SESSION
```

This lists each mark and range with the command that extracts it.

## Verify

```sh
python3 tools/session/session.py verify SESSION
```

This replays the whole play headless and compares every frame's picture and
memory hashes with the play's. A session that does not replay exactly is
reported at its first differing frame. Replay runs at about the game's own
speed, so verifying an hour of play takes about an hour.

## Extract

```sh
python3 tools/session/session.py extract SESSION --range 800-850 --context 10
```

This replays to the range's end and writes `SESSION/extract/800-850/`:

| File | What |
|---|---|
| `frames/fNNNNNN.png` | each frame, the console's own picture (one pixel a pixel) |
| `sound.wav` | the sound of the range with its context |
| `dumps/fNNNNNN.<ext>` | each frame's memories; on the Mega Drive `.wram`, `.zram`, `.vram`, `.cram`, `.vsram`, `.vdpreg` |
| `index.json` | each frame's buttons, hashes and files, and the marks in the range |
| `index.html` | the frames side by side with their numbers and buttons, and the sound |

Each extracted frame is checked against the play's hashes. `--no-dumps`
leaves out the memories.

## What a session is

| File | Made by |
|---|---|
| `session.json` | `session.py record`: the identity above, frame count, exit status |
| `bin/<binary>` | the copy that was played; verify and extract run it |
| `input.nks` | the recorder: the buttons of each frame as a headless script |
| `frames.log` | the recorder: each frame's picture and memory hashes |
| `marks.log` | the recorder: `mark N SECONDS` and `range A B SECONDS` |

Sessions and extracts hold the game's binary, pictures, sound and memory.
Keep them under ignored `build/decomp/<game-id>/sessions/` (or the private
title repository), never in nativekit's history, and never upload them.

## Checks

`mise run test:session` runs `session_check.py` on the kit's session console.
It covers record, marks, verify (a true play, and a dropped input change found
at its frame), extract (each PNG decoded and hashed back to the play's picture
hash, the WAV's exact samples, the dumps), and the refusal of a changed binary
or environment file.

## Controller reads

On a poll-aware native console, recording also writes `polls.bin`. Each
record describes the buttons available when the game read a controller port.
To replay those reads directly, use `--headless --frames N --replay-polls
SESSION/polls.bin` on the game. For a scripted capture add `--poll-log FILE`
to the original headless command. A missing poll or unexpected player stops
replay. Replaying a prefix consumes just the records needed for that run.
Older binaries can record frame input without producing this stream.
