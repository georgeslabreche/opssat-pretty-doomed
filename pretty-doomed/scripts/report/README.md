# Ground-side report generator

Post-downlink analysis of SDR capture artifacts. Produces SVG plots, signal statistics, self-contained HTML reports, and PSD evolution animations with audio.

## Dependencies

```
pip install numpy matplotlib scipy
```

For PSD animations: `ffmpeg` and `libsndfile` (via system package manager).

## Scripts

| Script | Purpose |
|--------|---------|
| `report.py` | Batch HTML report with embedded SVGs, signal stats, and tabbed multi-run views |
| `plot_iq.py` | I/Q file visualization (spectrogram, PSD, constellation, waveform) |
| `plot_audio.py` | WAV audio spectrogram and waveform |
| `plot_loopback.py` | Loopback TX/RX comparison with cross-correlation |
| `compare_psd.py` | Multi-capture PSD overlay |
| `animate_psd.py` | PSD evolution MP4 animation with audio |

Shared modules: `iq.py` (I/Q I/O + signal stats), `plots.py` (matplotlib utilities), `animation.py` (MP4 rendering), `output_dir.py` (output path derivation).

## Usage

### Generate report from EM data in docs/data

The committed data in `docs/data/` contains logs and resource CSVs. The report tool processes whatever it finds (logs, sc16, wav):

```bash
cd scripts/report
python3 report.py ../../docs/data/em-v4/pack-4023_1774937270 \
  --output-dir output --no-video --sample-rate 200000
```

### Generate report with full signal analysis

For full I/Q signal analysis (spectrograms, PSD, constellation plots), point at a directory containing `.sc16` and `.wav` files:

```bash
python3 report.py /path/to/toGround --output-dir output --sample-rate 200000
```

### Individual plot scripts

```bash
# I/Q analysis from sc16 file
python3 plot_iq.py /path/to/capture.sc16 --sample-rate 200000

# Audio spectrogram from WAV
python3 plot_audio.py /path/to/capture.wav

# PSD comparison across captures
python3 compare_psd.py /path/to/run-*/capture-*/capture.sc16 --sample-rate 200000

# PSD evolution animation
python3 animate_psd.py /path/to/capture.sc16 --sample-rate 200000
```

## Docker

For environments without Python/ffmpeg, use the Docker setup:

```bash
cd scripts/report
docker-compose run --rm tools report.py /data/em-v4/pack-4023_1774937270 \
  --output-dir /output --no-video --sample-rate 200000
```

The `docker-compose.yml` mounts `docs/data/` as `/data/` (read-only) and `./output/` for results.

## Output structure

Each tool creates a deterministic subfolder derived from the input path, so outputs from different runs never overwrite each other:

```
output/
└── pack-4023_1774447112/
    ├── index.html              # self-contained HTML report (tabbed by run)
    ├── run-00001/
    │   ├── capture-001/
    │   │   ├── spectrogram.svg
    │   │   ├── psd.svg
    │   │   ├── constellation.svg
    │   │   ├── iq_amplitude.svg
    │   │   ├── audio_spectrogram.svg
    │   │   ├── audio_waveform.svg
    │   │   ├── iq_stats.json
    │   │   └── psd_evolution.mp4
    │   └── psd_comparison.svg
    ├── run-00002/
    │   └── ...
    └── run-00003/
        └── ...
```
