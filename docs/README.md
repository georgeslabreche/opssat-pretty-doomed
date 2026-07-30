# Documentation map

This is a monorepo: the top level holds the project, and each component (`pretty-doomed/`, `doom/`, `common/`, `tools/`, `sandbox/`) owns its own docs next to its code. Documentation therefore lives at two levels by design:

- **This `docs/` (repo root)** holds project-, mission-, and platform-level references that are not specific to one component.
- **`pretty-doomed/docs/`** holds the docs for the flight application (the voice-command-to-DOOM pipeline), including its flight records and version changelog.

Use the map below to find things.

## Flight results and the milestone

The on-orbit runs, including the first voice command of a satellite (2026-07-27), are the flight application's record:

- [`pretty-doomed/docs/flight/`](../pretty-doomed/docs/flight/) — index of all flight runs, with `data/` (the downlinked record per run) and `debriefings/` (the per-run analysis).
- The [root README](../README.md#flight-results) summarizes the milestone and links the two milestone runs.

## The flight application (`pretty-doomed/docs/`)

- [BUILDING.md](../pretty-doomed/docs/BUILDING.md) — build, the SEPP ARM32 package, models, audio prep.
- [CONFIG.md](../pretty-doomed/docs/CONFIG.md) — every config key.
- [DESIGN.md](../pretty-doomed/docs/DESIGN.md) — architecture, pipeline, and design decisions.
- [TESTING.md](../pretty-doomed/docs/TESTING.md) — local, SDR-emulator, EM, and sc16-replay testing.
- [ONBOARD_PREVIEW.md](../pretty-doomed/docs/ONBOARD_PREVIEW.md) — running the onboard audio pipeline on the ground.
- [KEYER_SCREENING.md](../pretty-doomed/docs/KEYER_SCREENING.md) — screening keyer voice candidates through the flight pipeline.
- [changelog/](../pretty-doomed/docs/changelog/) — per-version changes (`V1_TO_V2.md` ...) and EM verification results (`RESULT.md`).

## Project, mission, and platform (this `docs/`)

- [PROPOSAL.md](PROPOSAL.md) — the experiment proposal.
- [PROPOSAL_DIVERGENCES.md](PROPOSAL_DIVERGENCES.md) — how the implementation diverged from the proposal.
- [OPSSAT_PRETTY_DEV_GUIDE.md](OPSSAT_PRETTY_DEV_GUIDE.md) — development guide for the spacecraft.
- [SEPP.md](SEPP.md) — the SEPP (onboard ARM32 processor) reference.
- [SDR_RECORDING_REQUEST.md](SDR_RECORDING_REQUEST.md) — the raw-IQ recording request used for RF-link tests.

## Other components

- [`common/`](../common/README.md) — shared C++ headers (logging, config, IIO, audio, spectrogram, constellation).
- [`doom/`](../doom/README.md) — the headless DOOM engine with frame capture.
- [`tools/`](../tools/README.md) — ground-side Python analysis tools (I/Q and audio visualization, HTML reports, the FM channel simulator, spectrogram video). Note: separate from `pretty-doomed/tools/`, which is the C++ on-board audio preview ([its README](../pretty-doomed/tools/README.md)).
- [`pretty-doomed/scripts/`](../pretty-doomed/scripts/README.md) — plotting and attitude/pointing scripts.
- [`sandbox/`](../sandbox/README.md) — standalone experiments that informed the design (SDR capture/loopback, signal processing, denoising, STT evaluation).
