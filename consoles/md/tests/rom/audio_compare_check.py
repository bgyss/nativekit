#!/usr/bin/env python3
"""Prove the ROM runner spots a changed stereo PCM sample."""
import pathlib
import struct
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run as rom_run  # noqa: E402


def main():
    frames = 64
    samples = [value for frame in range(frames)
               for value in (frame * 13 - 400, 500 - frame * 7)]
    native_bytes = struct.pack("=" + "h" * len(samples), *samples)
    changed = list(samples)
    changed[17 * 2 + 1] = 4000
    changed_bytes = struct.pack("=" + "h" * len(changed), *changed)

    with tempfile.TemporaryDirectory(prefix="nmd-audio-compare-") as temp:
        native_path = pathlib.Path(temp) / "native.raw"
        candidate_path = pathlib.Path(temp) / "candidate.raw"
        native_path.write_bytes(native_bytes)
        candidate_path.write_bytes(native_bytes)
        assert rom_run._compare_audio(native_path, candidate_path, 44100) is None
        candidate_path.write_bytes(changed_bytes)
        old_limit = rom_run.AUDIO_PHASE_LIMIT
        rom_run.AUDIO_PHASE_LIMIT = 0
        try:
            difference = rom_run._compare_audio(native_path, candidate_path, 44100)
        finally:
            rom_run.AUDIO_PHASE_LIMIT = old_limit
        assert difference and "sample 17 channel 1" in difference, difference

    print("audio_compare_check: ok")


if __name__ == "__main__":
    main()
