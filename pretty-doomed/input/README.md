# Test Inputs

- `samples/` -- human sample recordings of the voice command for file-input (`-i`) testing. georges and vlad recordings plus oli 01-04 are clean; oli 05-10 are noised at the source.
- `keyer/` -- the four shortlisted keyer voice candidates ([Piper](https://github.com/rhasspy/piper) TTS, dataset-named voices), the inputs for the FM channel simulator screening. See [docs/KEYER_SCREENING.md](../docs/KEYER_SCREENING.md).
- `replay.cs16` -- the v7 EM validation replay input (Run 5 pass 2 recording in flight `capture.sc16` form), for `-r` testing. Archived byte-identical in [docs/changelog/data/em-v7/](../docs/changelog/data/em-v7/). On the SEPP, a file at this path switches the `run` script from live capture to replay.

Generated `.sc16`/`.cs16` files (e.g. from `tools/src/make_fm_iq.py`) are gitignored anywhere under `input/`.
