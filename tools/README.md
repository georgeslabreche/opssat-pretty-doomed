# tools/ — Ground-side visualization for OPS-SAT PRETTY

Python tools for post-downlink analysis of SDR capture and loopback artifacts. Produces SVG plots, signal statistics, self-contained HTML reports, and realtime PSD evolution animations with audio.

## Setup

```bash
docker-compose build
```

## Project layout

```
tools/
├── Dockerfile
├── docker-compose.yml
├── requirements.txt
├── README.md
└── src/
    ├── plot_iq.py        # I/Q file visualization
    ├── detect_carrier.py # Carrier detection in a wideband I/Q file
    ├── summarize_carriers.py # Compare carrier detections across clips
    ├── demod_audio.py    # Demodulate raw I/Q to audio (FM / CW / SSB)
    ├── make_emu_replay.py # Build an SDR-emulator replay file from flight captures
    ├── make_fm_iq.py     # Simulate the radio trip: voice WAV -> flight-form baseband sc16
    ├── plot_audio.py     # WAV audio visualization
    ├── plot_loopback.py  # Loopback TX/RX comparison
    ├── compare_psd.py    # Multi-capture PSD overlay
    ├── report.py         # Batch HTML report generator
    ├── animate_psd.py    # Realtime PSD evolution animation
    ├── audio_spectrogram_video.py # WAV -> scrolling-spectrogram video with sound
    └── output_dir.py     # Shared output directory helper
```

## Output directory structure

Each tool automatically creates a deterministic subfolder derived from the input path, so outputs from different runs never overwrite each other. The subfolder name is built from distinctive path components (`pack-*`, `run-*`, `capture-*`):

```
output/
├── pack-4023_1772797685/                 # capture report (multi-run → tabbed)
│   ├── index.html                        # self-contained HTML report
│   └── run-000001/
│       ├── psd_comparison.svg            # PSD overlay across captures
│       ├── capture-001/                  # captures are tabbed within the run
│       │   ├── spectrogram.svg
│       │   ├── psd.svg
│       │   ├── constellation.svg
│       │   ├── iq_amplitude.svg
│       │   ├── audio_spectrogram.svg
│       │   ├── audio_waveform.svg
│       │   ├── psd_evolution.mp4
│       │   └── iq_stats.json
│       ├── capture-002/
│       │   └── ...
│       └── capture-003/
│           └── ...
└── pack-4023_1773212635/                 # loopback report (multi-run → tabbed)
    ├── index.html
    ├── run-000001/
    │   ├── spectrogram.svg
    │   ├── psd.svg
    │   ├── constellation.svg
    │   ├── iq_amplitude.svg
    │   ├── audio_spectrogram.svg
    │   ├── audio_waveform.svg
    │   ├── psd_evolution.mp4
    │   ├── loopback_overlay.svg
    │   ├── loopback_correlation.svg
    │   └── iq_stats.json
    ├── run-000002/
    │   └── ...
    └── run-000003/
        └── ...
```

## Scripts

### report.py — Batch HTML report

Processes a run directory (or parent directory containing multiple `run-*` subdirectories) and generates a self-contained `index.html` with embedded SVGs, signal statistics, log files, and quality badges. When multiple runs are detected, each run is rendered as a tab. Multiple captures within a run are also tabbed. Individual SVG files and `iq_stats.json` are also written alongside the report.

```bash
# Capture — single run
docker-compose run --rm tools src/report.py \
  /data/capture-artifacts/pack-4023_.../chg/toGround/run-000001 \
  --sample-rate 200000 --output-dir /output

# Capture — parent directory (auto-detects run-* subdirs, creates tabs)
docker-compose run --rm tools src/report.py \
  /data/capture-artifacts/pack-4023_.../chg/toGround \
  --sample-rate 200000 --output-dir /output

# Loopback — parent directory with tabs, skip video generation
docker-compose run --rm tools src/report.py \
  /data/loopback-artifacts/pack-4023_.../chg/toGround \
  --sample-rate 200000 --loopback --input-wav /data/samples/input.wav \
  --output-dir /output --no-video
```

Options:
- `--sample-rate` — I/Q sample rate in Hz (default: 200000)
- `--loopback` — Treat as loopback run (flat file structure with input WAV comparison)
- `--input-wav` — Input WAV for loopback comparison
- `--no-video` — Skip PSD evolution video generation (much faster)

| Output | Description |
|--------|-------------|
| `index.html` | Self-contained HTML with embedded SVGs, stat cards, quality badges, and logs |
| `psd_comparison.svg` | PSD overlay across all captures in the run (capture mode only) |
| `spectrogram.svg` | I/Q spectrogram waterfall |
| `psd.svg` | Power spectral density |
| `constellation.svg` | I/Q scatter plot |
| `iq_amplitude.svg` | Per-channel I/Q RMS amplitude over time (100 ms windows) |
| `psd_evolution.mp4` | Realtime PSD animation with audio soundtrack (unless `--no-video`) |
| `iq_stats.json` | Signal statistics (machine-readable) |

Quality badges:
- **SNR**: PASS (>10 dB), WARN (3–10 dB), FAIL (<3 dB)
- **Loopback correlation**: PASS (>=0.7), WARN (0.3–0.7), FAIL (<0.3)
- **Zero fraction**: OK (<1%), WARN (1–10%), FAIL / Q dropout (>10%)

### plot_iq.py — I/Q file visualization

Generates plots and signal statistics from sc16 I/Q files.

```bash
docker-compose run --rm tools src/plot_iq.py <file.sc16> --sample-rate 200000 --output-dir /output
```

| Output | Description |
|--------|-------------|
| `spectrogram.svg` | Time-frequency waterfall |
| `psd.svg` | Power spectral density with noise floor, peak frequency, and occupied bandwidth |
| `constellation.svg` | I vs Q scatter plot |
| `waveform.svg` | First 10 ms of I and Q amplitude |
| `iq_amplitude.svg` | Per-channel I/Q RMS amplitude over time (100 ms windows) |
| `iq_stats.txt` | Signal statistics (human-readable) |
| `iq_stats.json` | Signal statistics (machine-readable) |

**Signal statistics include:**
- Signal level: RMS, peak, crest factor (all in dBFS)
- Per-channel: RMS I, RMS Q (dBFS), zero fraction I, zero fraction Q
- DC offset: I, Q, and magnitude
- I/Q imbalance: gain imbalance (dB) and phase imbalance (degrees)
- Spectral: frequency offset, noise floor, SNR estimate, occupied bandwidth

Options:
- `--sample-rate` — Sample rate in Hz (default: 200000, the effective rate after 12x decimation)
- `--output-dir` — Output directory (default: same as input file)
- `--fft-size` — FFT size for spectrogram (default: 1024)

### detect_carrier.py — Carrier detection

Finds and characterizes the strongest narrowband carrier in a wideband sc16/cs16 I/Q file. Built for the RF-link test, where the spacecraft records a wide band with the SDR center offset from the expected uplink so a ground carrier lands clear of the DC spike. Reports how far the carrier stands above the noise floor, its width, whether it keys on and off, and its offset from a target uplink frequency.

```bash
docker-compose run --rm tools src/detect_carrier.py <file.cs16> \
    --sample-rate 2500000 --center-freq 1295.5e6 --target-freq 1296.0e6 --output-dir /output
```

| Output | Description |
|--------|-------------|
| `spectrogram.svg` | Full-band time-frequency waterfall |
| `carrier.svg` | Spectrogram with the detected carrier marked, plus its on/off envelope |
| `psd.svg` | Power spectral density |
| `carrier.txt` | Carrier detection summary (human-readable) |
| `carrier.json` | Carrier detection summary (machine-readable) |

**Carrier report includes:**
- Carrier offset from band center, absolute frequency, and offset from the target uplink
- SNR over the noise floor, and -3 dB / -20 dB widths (carrier vs modulated)
- On fraction and a coarse 100 ms on/off keying timeline (transmit pauses, Morse)
- Top spectral peaks, to distinguish a real carrier from fixed internal spurs

Options:
- `--sample-rate` — Sample rate in Hz (required, e.g. 2500000)
- `--center-freq` — SDR center frequency in Hz, for absolute labeling (e.g. 1295.5e6)
- `--target-freq` — Expected uplink frequency in Hz, to report the offset (e.g. 1296.0e6)
- `--output-dir` — Output directory (default: same as input file)
- `--fft-size` — FFT size for PSD and detection (default: 8192)

### summarize_carriers.py — Compare carriers across clips

Companion to `detect_carrier.py`: reads the `carrier.json` files from several clips (for example the snapshots of one pass, or two passes of an RF-link test) and tabulates them side by side so the trend is visible: how the absolute carrier frequency walks with Doppler, how the SNR changes, and where the carrier keys on and off. Inputs may be `carrier.json` files or directories searched recursively for them; when a file sits in a capture-named directory (`sdr_YYYYMMDD_HHMMSS_...`) the timestamp is parsed to order and label the clips.

```bash
docker-compose run --rm tools src/summarize_carriers.py /output/analysis --target-freq 1296.0e6
```

| Output | Description |
|--------|-------------|
| `carriers_summary.txt` | Human-readable comparison table |
| `carriers_summary.json` | Machine-readable per-clip metrics |
| `carriers_summary.svg` | Offset-vs-time and SNR-vs-time across the clips |

Options:
- `inputs` — one or more `carrier.json` files or directories to search (required)
- `--target-freq` — expected uplink frequency in Hz, overrides per-file target
- `--output-dir` — output directory (default: common parent of the inputs)
- `--title` — plot title

### make_emu_replay.py — SDR-emulator replay from flight captures

Converts raw flight recordings into a file the iio-emu based test (`pretty-doomed/docker-compose.emu-test.yml`) can replay: locates the uplink in each capture, shifts it to a chosen offset near DC (default -8 kHz so a peak search is genuinely exercised), resamples to the configured radio rate, rescales to the AD9361 12-bit range, and concatenates the inputs. Note: generate with `--out-rate` at twice the configured `sdr_rate` (the emulator consumes two complex samples per delivered sample). See `pretty-doomed/docs/TESTING.md` for the replay workflow.

```bash
python3 src/make_emu_replay.py <raw1.cs16> <raw2.cs16> ... \
    --output ../sandbox/gnuradio/sdr-capture/emu-samples/run05_replay.cs16 \
    --out-rate 4800000
```

| Output | Description |
|--------|-------------|
| `<output>` | Emulator-ready cs16 (interleaved little-endian int16), inputs concatenated |

Options:
- `inputs` — raw `.sc16`/`.cs16` recordings, concatenated in the order given (required)
- `--output` — output replay file (required)
- `--sample-rate` — input sample rate in Hz (default: 2500000)
- `--center-freq` / `--target-freq` — input SDR center and expected uplink in Hz, to locate the carrier
- `--out-rate` — replay sample rate in Hz (default: 2400000; use 2x `sdr_rate` for iio-emu)
- `--offset-hz` — where to place the uplink relative to DC (default: -8000)
- `--peak` — peak int16 amplitude (default: 2000)

### make_fm_iq.py — Simulated radio trip for keyer screening

Simulates the radio link: FM-modulates a clean voice WAV onto complex baseband at the flight effective rate, places the carrier at a chosen offset with optional Doppler drift, and adds noise to a chosen carrier-to-noise ratio. The output is `capture.sc16`-form, so it runs through the actual flight binary with `pretty-doomed -r`, exercising the narrowing, peak search, and FM discriminator. CNR is defined within `--cnr-bw` (default 20 kHz, the narrowing bandwidth). Default carrier placement (-8 kHz) is the Run 5 offset measured at zenith; default deviation (5 kHz) is the flight demodulator's configured `sdr_fm_deviation`.

```bash
python3 src/make_fm_iq.py keyer_voice.wav --output voice_cnr12.sc16 --cnr-db 12
```

Measured vs assumed: the carrier placement, CNR anchor, and the flatness of the noise floor are taken from the [Run 5 flight captures](../pretty-doomed/docs/flight/debriefings/run-05-2026-07-03/); the deviation is the flight demodulator's configured value pending confirmation of the transmitter settings, and the noise itself is synthetic AWGN by default and omits the real receiver's narrowband features (spurs, DC spike), for which `--noise-file` substitutes real capture noise. Details and screening results (keyer voices, human recordings, CNR sweep): [`pretty-doomed/docs/KEYER_SCREENING.md`](../pretty-doomed/docs/KEYER_SCREENING.md).

| Output | Description |
|--------|-------------|
| `<output>` | Flight-form baseband sc16 (200 kHz, interleaved little-endian int16) |

Options:
- `input` — clean voice WAV, any rate (required)
- `--output` — output sc16 file (required)
- `--out-rate` — output sample rate in Hz (default: 200000, the flight effective rate)
- `--deviation` — peak FM deviation in Hz (default: 5000)
- `--offset-hz` / `--drift-hz-s` — carrier offset from DC and linear drift (defaults: -8000, 0)
- `--cnr-db` / `--cnr-bw` — target CNR in dB and its reference bandwidth (omit `--cnr-db` for no noise)
- `--noise-file` — noise-only sc16 at the output rate to use instead of AWGN (e.g. a keying pause from a real capture)
- `--pad-s` — noise-only padding before and after the voice (default: 0.25)
- `--peak` / `--seed` — output amplitude and RNG seed for reproducible noise

### audio_spectrogram_video.py — WAV to spectrogram video with sound

Renders audio to a scrolling-spectrogram video with the audio as the soundtrack, for the recovered capture audio in the flight debriefings. The spectrogram is limited to the voice band (default 0 to 4 kHz, by resampling to 2x that rate) so the frame is filled with the part of the spectrum that carries the voice rather than mostly-dark high frequencies. In `--capture-audio` mode it assembles several per-capture WAVs into one track: `--concat` joins them end to end (no gaps, for a video of just the voice content), or `--capture-windows` places them at their real offsets (keeping the silent inter-capture intervals). Either way `--trim-pad-s` pads the start and end. Requires ffmpeg.

```bash
# single WAV
python3 src/audio_spectrogram_video.py capture.wav --output capture-spectrogram.mp4

# whole run, all captures joined end to end, 2 s pad each side
python3 src/audio_spectrogram_video.py --output captures-spectrogram.mp4 \
  --capture-audio "c1.wav,c2.wav,c3.wav,c4.wav,c5.wav,c6.wav" --concat --trim-pad-s 2

# whole run, captures at their real time offsets (keeps inter-capture gaps)
python3 src/audio_spectrogram_video.py --output captures-spectrogram.mp4 \
  --capture-audio "c1.wav,c2.wav,c3.wav,c4.wav,c5.wav,c6.wav" \
  --capture-windows "[[9,31],[35,56],[60,82],[86,114],[119,146],[151,173]]" --trim-pad-s 2
```

Options:
- `input` — input WAV (single-file mode)
- `--output` — output `.mp4` (required)
- `--max-freq` — top of the displayed frequency band in Hz (default 4000)
- `--size` — video size WxH (default 900x360)
- `--capture-audio` — comma-separated per-capture WAVs to assemble into one track
- `--concat` — join the WAVs end to end (no inter-capture gaps); `--capture-windows` not needed
- `--capture-windows` — JSON `[start_s, end_s]` per capture, to place them at real offsets
- `--trim-pad-s` — seconds of silence before the first and after the last capture (default 2.0)

### demod_audio.py — Demodulate raw I/Q to audio

Turns a raw sc16/cs16 I/Q downlink into a WAV. The on-board pipeline demodulates during capture, so raw I/Q downlinks arrive without audio; this reproduces that step on the ground. `fm` mode mirrors the flight chain (channel low-pass, quadrature FM demod, resample, 300-3400 Hz voice band-pass) for voice broadcasts; `cw` mode mixes the carrier to an audible beat tone so a keyed transmission can be heard on/off; `ssb` mode is a single-sideband product detector for amateur SSB voice (tune to the suppressed carrier, keep one sideband).

```bash
# Voice broadcast, tuned to a known offset
docker-compose run --rm tools src/demod_audio.py <file.sc16> --sample-rate 200000 --mode fm --offset -8000
# Keyed carrier, auto-tuned to the strongest carrier
docker-compose run --rm tools src/demod_audio.py <file.cs16> --sample-rate 2500000 --mode cw --auto --center-freq 1295.5e6 --output-dir /output
# SSB voice, auto sideband
docker-compose run --rm tools src/demod_audio.py <file.cs16> --sample-rate 2500000 --mode ssb --auto --center-freq 1295.5e6 --channel-bw 8000 --output-dir /output
```

| Output | Description |
|--------|-------------|
| `audio.wav` | Demodulated mono audio (16 kHz by default) |

Options:
- `--sample-rate` — Input sample rate in Hz (required)
- `--mode` — `fm` (default, flight-matching), `cw` (beat tone for keyed carriers), or `ssb` (single-sideband voice)
- `--sideband` — `auto` (default), `lsb`, or `usb` for `--mode ssb`
- `--auto` — Auto-detect the strongest carrier and tune to it
- `--offset` / `--carrier-freq` (+`--center-freq`) — Carrier as a baseband offset or absolute frequency
- `--deviation` — FM deviation in Hz (default: 5000, flight NBFM)
- `--channel-bw` — Channel bandwidth before demod (default: 15000)
- `--audio-rate` — Output audio sample rate (default: 16000)
- `--bandpass` — Voice band-pass `low,high` Hz, or `none` (default: 300,3400)
- `--bfo` — CW beat tone in Hz for `--mode cw` (default: 800)
- `--output-dir` — Output directory (default: same as input file)

### plot_audio.py — WAV audio visualization

Generates audio spectrogram and waveform from demodulated WAV files.

```bash
docker-compose run --rm tools src/plot_audio.py <file.wav> --output-dir /output
```

| Output | Description |
|--------|-------------|
| `audio_spectrogram.svg` | Time-frequency view showing voice formants |
| `audio_waveform.svg` | Full audio amplitude over time |

### plot_loopback.py — Loopback comparison

Compares input (TX) and output (RX) WAV files from loopback tests.

```bash
docker-compose run --rm tools src/plot_loopback.py <input.wav> <output.wav> --output-dir /output
```

| Output | Description |
|--------|-------------|
| `loopback_overlay.svg` | Input vs output waveforms (aligned) |
| `loopback_correlation.svg` | Cross-correlation vs lag (±100 ms) |

### animate_psd.py — PSD evolution animation

Realtime MP4 video with two synchronized panels: a spectrogram progressively revealed left-to-right and an animated PSD curve. Auto-detects sibling WAV files and muxes the demodulated audio as a soundtrack.

```bash
docker-compose run --rm tools src/animate_psd.py <file.sc16> --sample-rate 200000 --output-dir /output

# Explicit audio file
docker-compose run --rm tools src/animate_psd.py <file.sc16> --sample-rate 200000 --audio demod.wav --output-dir /output
```

| Output | Description |
|--------|-------------|
| `psd_evolution.mp4` | Realtime video (duration matches capture) with audio |

Options:
- `--sample-rate` — Sample rate in Hz (default: 200000)
- `--output-dir` — Output directory (default: same as input file)
- `--fft-size` — FFT size for PSD computation (default: 4096)
- `--audio` — Path to WAV file for soundtrack (default: auto-detect sibling .wav)

### compare_psd.py — Multi-capture PSD overlay

Compares PSD curves from multiple sc16 files on a single plot.

```bash
docker-compose run --rm tools src/compare_psd.py \
  /data/capture-artifacts/.../capture-001/capture.sc16 \
  /data/capture-artifacts/.../capture-002/capture.sc16 \
  /data/capture-artifacts/.../capture-003/capture.sc16 \
  --sample-rate 200000 --output-dir /output
```

| Output | Description |
|--------|-------------|
| `psd_comparison.svg` | Overlaid PSD curves with legend |

## Data access

The `docker-compose.yml` mounts artifact directories as read-only volumes:

| Mount | Host path |
|-------|-----------|
| `/data/capture-artifacts` | `sandbox/gnuradio/sdr-capture/artifacts/` |
| `/data/loopback-artifacts` | `sandbox/gnuradio/sdr-loopback/toGround/` |
| `/data/samples` | `samples/` |
| `/output` | `tools/output/` |

## Example with real satellite data

```bash
# Full HTML report for a capture experiment (tabbed runs, tabbed captures)
docker-compose run --rm tools src/report.py \
  /data/capture-artifacts/pack-4023_1772797685/chg/toGround \
  --sample-rate 200000 --output-dir /output --no-video

# Individual I/Q analysis
docker-compose run --rm tools src/plot_iq.py \
  /data/capture-artifacts/pack-4023_1772797685/chg/toGround/run-000001/capture-001/capture.sc16 \
  --sample-rate 200000 --output-dir /output

# PSD comparison across captures
docker-compose run --rm tools src/compare_psd.py \
  /data/capture-artifacts/pack-4023_1772797685/chg/toGround/run-000001/capture-*/capture.sc16 \
  --sample-rate 200000 --output-dir /output

# PSD evolution animation (realtime, with audio)
docker-compose run --rm tools src/animate_psd.py \
  /data/capture-artifacts/pack-4023_1772797685/chg/toGround/run-000001/capture-001/capture.sc16 \
  --sample-rate 200000 --output-dir /output
```

## I/Q data format

Input files use the sc16 format: interleaved signed 16-bit integers (I, Q, I, Q, ...), 4 bytes per complex sample. Values are normalized to [-1, 1] by dividing by 32768.
