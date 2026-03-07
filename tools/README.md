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
    ├── plot_audio.py     # WAV audio visualization
    ├── plot_loopback.py  # Loopback TX/RX comparison
    ├── compare_psd.py    # Multi-capture PSD overlay
    ├── report.py         # Batch HTML report generator
    ├── animate_psd.py    # Realtime PSD evolution animation
    └── output_dir.py     # Shared output directory helper
```

## Output directory structure

Each tool automatically creates a deterministic subfolder derived from the input path, so outputs from different runs never overwrite each other. The subfolder name is built from distinctive path components (`pack-*`, `run-*`, `capture-*`):

```
output/
├── pack-4023_1772797685_run-000001/     # capture report
│   ├── index.html                        # self-contained HTML report
│   ├── psd_comparison.svg                # PSD overlay across captures
│   ├── capture-001/
│   │   ├── spectrogram.svg
│   │   ├── psd.svg
│   │   ├── constellation.svg
│   │   ├── audio_spectrogram.svg
│   │   ├── audio_waveform.svg
│   │   ├── psd_evolution.mp4
│   │   └── iq_stats.json
│   ├── capture-002/
│   │   └── ...
│   └── capture-003/
│       └── ...
└── run-000001/                           # loopback report
    ├── index.html
    ├── spectrogram.svg
    ├── psd.svg
    ├── constellation.svg
    ├── audio_spectrogram.svg
    ├── audio_waveform.svg
    ├── psd_evolution.mp4
    ├── loopback_overlay.svg
    ├── loopback_correlation.svg
    └── iq_stats.json
```

## Scripts

### report.py — Batch HTML report

Processes an entire run directory and generates a self-contained `index.html` with embedded SVGs, signal statistics, log files, and quality badges. Individual SVG files and `iq_stats.json` are also written alongside the report.

```bash
# Capture run
docker-compose run --rm tools src/report.py \
  /data/capture-artifacts/pack-4023_.../chg/toGround/run-000001 \
  --sample-rate 200000 --output-dir /output

# Loopback run
docker-compose run --rm tools src/report.py \
  /data/loopback-artifacts/run-000001 \
  --sample-rate 200000 --loopback --input-wav /data/samples/input.wav \
  --output-dir /output
```

| Output | Description |
|--------|-------------|
| `index.html` | Self-contained HTML with embedded SVGs, stat cards, quality badges, and logs |
| `psd_comparison.svg` | PSD overlay across all captures in the run (capture mode only) |
| `<capture-NNN>/spectrogram.svg` | Per-capture spectrogram |
| `<capture-NNN>/psd.svg` | Per-capture PSD |
| `<capture-NNN>/constellation.svg` | Per-capture I/Q constellation |
| `<capture-NNN>/psd_evolution.mp4` | Realtime PSD animation with audio soundtrack |
| `<capture-NNN>/iq_stats.json` | Per-capture signal statistics |

Quality badges:
- **SNR**: PASS (>10 dB), WARN (3–10 dB), FAIL (<3 dB)
- **Loopback correlation**: PASS (>=0.7), WARN (0.3–0.7), FAIL (<0.3)

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
| `iq_stats.txt` | Signal statistics (human-readable) |
| `iq_stats.json` | Signal statistics (machine-readable) |

**Signal statistics include:**
- Signal level: RMS, peak, crest factor (all in dBFS)
- DC offset: I, Q, and magnitude
- I/Q imbalance: gain imbalance (dB) and phase imbalance (degrees)
- Spectral: frequency offset, noise floor, SNR estimate, occupied bandwidth

Options:
- `--sample-rate` — Sample rate in Hz (default: 200000, the effective rate after 12x decimation)
- `--output-dir` — Output directory (default: same as input file)
- `--fft-size` — FFT size for spectrogram (default: 1024)

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
# Full HTML report for a capture run (all 3 captures)
docker-compose run --rm tools src/report.py \
  /data/capture-artifacts/pack-4023_1772797685/chg/toGround/run-000001 \
  --sample-rate 200000 --output-dir /output

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
