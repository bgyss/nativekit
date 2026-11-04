# Mega Drive analysis tools

`features.py` derives bounded feature observations from a runner manifest and a
versioned `bus.trace`:

```sh
python3 consoles/md/tools/features.py \
  --manifest build/run/manifest.json \
  --trace build/run/bus.trace \
  --out build/run/features.json
```

Each observation is `observed`, `not_observed`, or `unavailable`. The extractor
uses `not_observed` only when the trace advertises the capability needed to see
that feature. Missing capabilities and missing cartridge metadata produce
`unavailable`. For SRAM detection, a caller may add an `sram_window` object with
integer `start` and exclusive `end` byte addresses to the manifest. The survey
reads those bounds from a valid `RA` cartridge header and passes them to the
runner; without a valid header window, SRAM use remains `unavailable`. HINT line
events report VDP pending lines; they do not prove the 68000 accepted or serviced
each interrupt.

An `observed` status means trace evidence was captured, not that a setting was
active. For boolean settings, the `enabled` value distinguishes active from
explicitly disabled.

Feature evidence contains clocks, device and port addresses, and selected
register values. It omits transferred DMA and DAC payload values, save-memory
bytes, input paths, and ROM files.

## Cost profiles

`nmd-profile` fits separate 68000 and Z80 access costs from candidate CPU
statistics. An optional JSON input span uses one-based frame numbers and the
same button names as `oracle/run.py`:

```sh
python3 consoles/md/tools/nmd-profile measure game.bin \
  --out build/game-profile --frames 3600 --input game-input.json
```

The output directory receives `profile.txt`, `profile.c`, and `report.txt`.
The C table uses the public `nmd_cost_profile` type; assign its symbol to the
cartridge's `cost_profile` field. Reports store ROM hashes, not ROM paths or
contents. `--decomp 'build/game'` also runs the native build with `--counts`;
when measuring multiple ROMs, the native count comparison uses the first ROM.
Add `--source game-src` to record a source-tree hash, then check it later with
`nmd-profile check build/game-profile/profile.txt --source game-src`.

Profiles use candidate event-clock deltas minus nominal bus times. Dynamic
device waits may remain in the fitted work, so these profiles are exploratory
candidate estimates and do not qualify hardware timing. `md1va6` runs require
`--system-dir` with the candidate's TMSS system files. Run
`mise run test:md-profile` for the fit, stale-source, privacy, and native count
checks.

## Bounded library survey

```sh
python3 consoles/md/tools/survey.py "$ROM_DIR" \
  --core build/toolchains/blastem/libblastem.dylib \
  --out consoles/md/docs/survey.md
```

The default run is 120 frames with no input, then 120 frames with Start pressed
for one frame at frame 60. Files are processed in stable order; report titles
come from sanitized cartridge headers and duplicates receive a numeric suffix.
Runs use the `survey-feature-filter-v1` trace scope, which retains device events
and the M68K/Z80 CPU address ranges used by feature extraction.
Invalid, unsupported, incomplete, and failed runs remain explicit statuses.
Only run summaries and feature evidence are written; ROM paths and bytes are
kept out of the report.
