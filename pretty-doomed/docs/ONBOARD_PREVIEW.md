# On-board audio preview from a downlinked raw sc16

`preview_onboard` runs a downlinked raw I/Q recording through the **same GNU Radio DSP chain the flight app uses** (`src/capture.cpp`), so you can hear what the on-board doom pipeline would have produced from a pass that was instead captured as wideband raw I/Q (as in the RF-link test, Run 5).

The flight `capture.cpp` is wired to the AD9361 `device_source` and cannot read a file, so `tools/preview_onboard.cpp` builds its audio chain from the same shared module `capture.cpp` uses (`src/chain.{h,cpp}`, #112), driven by the same `PipelineConfig` - preview and flight execute the same DSP by construction. It is a ground-only tool and lives in `tools/`, not in `src/` where the flight software is:

```
fir_filter_ccf(decimation, firdes::low_pass 85 kHz)   # channel filter + decimate
  -> quadrature_demod_cf(effective_rate / 2*pi*deviation)   # FM discriminator
  -> rational_resampler_fff(-> 16 kHz)
  -> fir_filter_fff(firdes::band_pass 300-3400 Hz)     # voice band
  -> wavfile_sink ; then rms_normalize(-20 dBFS)
```

All rates, taps, and the FM gain come from the shared `src/chain.cpp`, the module `capture.cpp` itself builds from, so the DSP is the flight code, not a copy of it.

## What it emulates (front end only)

A raw recording made for the RF-link test bypasses two pieces of SDR hardware that the operational path uses. `preview_onboard` emulates them before the flight chain, and this is the only part that is not flight C code:

1. **LO tuning.** The recording is centered off the uplink (1295.5 MHz) so the carrier lands clear of the DC spike; the flight config tunes to the uplink (1296.0 MHz). The tool rotates the spectrum by that difference so the uplink sits near DC, inside the 85 kHz channel filter, where the on-board receiver would place it.
2. **AD9361 decimating HW FIR.** The recording is raw 2.5 MSPS; the flight path feeds GNU Radio at 600 kSPS post-FIR. The tool rational-resamples to that rate.

The center frequency and sample rate of the recording are passed on the command line (they are also in the capture filename, `sdr_<date>_<time>_<center>_<rate>_*`).

## Usage

```
preview_onboard <in.sc16> <out.wav> [config.cfg] [rec_center_hz] [rec_rate_hz] [narrow_bw_hz]
```

- `in.sc16` -- downlinked raw interleaved int16 I/Q
- `out.wav` -- output audio (16 kHz mono, normalized to -20 dBFS)
- `config.cfg` -- flight config to read the SDR/DSP parameters from (default `config.cfg`)
- `rec_center_hz` -- SDR center of the recording (default `1295500000`)
- `rec_rate_hz` -- sample rate of the recording (default `2500000`)
- `narrow_bw_hz` -- optional narrowing stage (issue #107): find the strongest peak within ±100 kHz of the uplink, tune it to DC, low-pass to ±`narrow_bw_hz`/2 and decimate to ~25 kSPS before the FM discriminator. `0` (default) keeps the flight chain exactly as flown; `20000` reproduces the validated ±10 kHz narrowing.

## Build and run

### Option A: quick, with a slim GNU Radio container

This needs only GNU Radio and libsndfile, not the full flight image (no sherpa-onnx build). Run from the repo root; the whole repo is mounted at `/work`.

```bash
# One-time: start a container with the toolchain
docker run -d --name pd-preview \
  -v "$PWD":/work -w /work/pretty-doomed debian:bookworm-slim sleep infinity
docker exec pd-preview bash -lc 'apt-get update && \
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential pkg-config gnuradio-dev libsndfile1-dev libfftw3-dev libfmt-dev'

# Compile (GNU Radio only; the tool does not use iio/ad9361)
docker exec pd-preview bash -lc '
  cd /work/pretty-doomed && mkdir -p build/local
  GR="gnuradio-runtime gnuradio-blocks gnuradio-filter gnuradio-fft gnuradio-analog"
  g++ -Wall -O3 -std=c++17 -Isrc -I../common/include $(pkg-config --cflags $GR) \
      tools/preview_onboard.cpp src/config.cpp src/chain.cpp -o build/local/preview_onboard \
      $(pkg-config --libs $GR) -lfmt -lsndfile -lfftw3f -lpthread -lm'

# Run over a set of downlinked clips (put them under input/, for example)
docker exec pd-preview bash -lc '
  cd /work/pretty-doomed
  OUT=toGround/onboard-preview; mkdir -p $OUT
  for f in input/*.cs16; do
    id=$(basename "$f" | sed -E "s/sdr_[0-9]+_([0-9]+)_.*/\1/")
    ./build/local/preview_onboard "$f" "$OUT/onboard_$id.wav" \
        config.cfg 1295500000 2500000
  done'

# Tidy up when done
docker rm -f pd-preview
```

### Option B: inside the full flight image

The Makefile has a `preview-onboard` target that links against the same GNU Radio libs as the flight build. Point it at a file under a mounted path such as `input/`.

```bash
docker-compose run --rm pretty-doomed make preview-onboard
docker-compose run --rm pretty-doomed \
  ./build/local/preview_onboard input/capture.sc16 toGround/onboard.wav \
      config.cfg 1295500000 2500000
```

## End to end, through speech recognition

`preview_onboard` stops at the audio, on purpose: it is a small ground tool with no STT dependency. To take a clip all the way through the on-board experience (audio -> transcript -> wake word / call sign / command detection), chain it into the flight app's single-file mode, which runs the real `process_wav` (the same resample + sherpa-onnx STT + keyword matcher as flight):

```
raw sc16 --preview_onboard--> onboard.wav --pretty-doomed -i--> transcription.txt + summary.txt
```

`tools/preview_onboard_e2e.sh` wraps both stages. Stage 2 is the unmodified flight binary, so the recognition result is exactly what would run on-board. It needs the full build (STT models under `models/`), so run it in the image:

```bash
# Build the full image once (compiles sherpa-onnx from source)
docker compose build

# Build the app + the preview tool, then run a clip end to end
# (the downlinked sc16 placed under the mounted input/ folder).
docker compose run --rm pretty-doomed bash -lc '
  make all preview-onboard
  tools/preview_onboard_e2e.sh \
    input/sdr_20260703_205825_1295500000_2500000_1.cs16 \
    toGround/e2e/205825 1295500000 2500000'
```

The wrapper writes `onboard.wav`, `transcription.txt`, `scores.txt`, and `summary.txt` into the output directory and prints the transcript and detection summary. Overridable via env: `CONFIG`, `VARIANTS`, `DEMOS`, `DOOM`, `PREVIEW`, `APP`.

## Interpreting the output

For the Run 5 clips the previews are mostly broadband noise with only faint syllabic structure during the strongest voice bursts. The transmission is FM voice (confirmed by the operators, sent wide), so the FM discriminator is the correct demodulator, but the pipeline feeds it the full wide band (about 170 kHz of noise, far more than the signal occupies), which pushes it below FM threshold and turns the voice to static. The link itself is fine (the carrier and recoverable voice are in the raw I/Q); the gap is bandwidth. Narrowing to the signal band before the discriminator recovers the voice; hear the difference in the narrowed renderings inside `docs/flight/debriefings/run-05-2026-07-03/run-05-audio-processing.zip`.
