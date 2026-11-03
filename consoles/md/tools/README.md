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

Feature evidence contains addresses, values, clocks, and register writes from
the trace. The JSON output does not include input paths or ROM contents.

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
