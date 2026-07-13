# tools/ - ground-only utilities

Ground-side tooling for the pretty-doomed app. This is **not flight software**: nothing here runs on the spacecraft. It lives outside `src/` on purpose. These tools reuse the flight code (`src/config.cpp`, `src/capture.cpp`'s DSP chain) but are built and run on the ground.

## Contents

- **`preview_onboard.cpp`** - offline preview of the on-board audio pipeline. Feeds a downlinked raw sc16 I/Q file through the same GNU Radio DSP chain the flight app runs in `capture.cpp` (channel low-pass + decimation, quadrature FM demod, resample, voice band-pass, RMS normalize), so you can hear what the on-board pipeline would have produced from a pass that was instead downlinked as raw I/Q. An optional `narrow_bw_hz` inserts the issue #107 narrowing stage (peak find, shift to DC, low-pass) before the discriminator; `0` (default) keeps the flight chain exactly as flown. Built with `make preview-onboard`.

  ```
  preview_onboard <in.sc16> <out.wav> [config.cfg] [rec_center_hz] [rec_rate_hz] [narrow_bw_hz]
  ```

- **`preview_onboard_e2e.sh`** - end-to-end wrapper: runs `preview_onboard` to get the audio, then the real flight app in single-file mode (`pretty-doomed -i`) so the audio goes through the genuine speech-to-text and keyword matcher.

  ```
  tools/preview_onboard_e2e.sh <in.sc16> <out_dir> [rec_center_hz] [rec_rate_hz] [narrow_bw_hz]
  ```

## Build and run

Both need GNU Radio (and, for the e2e wrapper, the full app and STT models). Full instructions, including the front-end emulation caveats (LO tuning and the AD9361 hardware FIR) and both the slim-container and full-image recipes, are in [`../docs/ONBOARD_PREVIEW.md`](../docs/ONBOARD_PREVIEW.md).

The compose service mounts this folder at `/app/tools`, so `make preview-onboard` and the wrapper work inside the container.
