# GNU Radio + Whisper Integration for OPS-SAT

Single binary pipeline using GNU Radio for signal processing and Whisper for speech-to-text. Designed for the OPS-SAT SEPP to process SDR-captured audio.

## Pipeline

```
┌─────────────┐     ┌────────────────────────────────────────────────────┐     ┌──────┐
│  Audio File │ ──▶ │              gnuradio_pipeline                     │ ──▶ │ Text │
│    (WAV)    │     │  Lowpass → Bandpass → Noise Red. → Whisper         │     │      │
└─────────────┘     └────────────────────────────────────────────────────┘     └──────┘
```

### Processing Stages

| Stage | Component | Purpose |
|-------|-----------|---------|
| 1 | **Lowpass filter** | Remove high-frequency noise (cutoff: 4000 Hz) |
| 2 | **Bandpass filter** | Isolate voice frequencies (300-3400 Hz) |
| 3 | **Noise reduction** | Attenuate background noise (configurable) |
| 4 | **Resample** | Convert to 16kHz for Whisper |
| 5 | **Whisper** | Speech-to-text transcription |

## Build

```bash
docker-compose build
docker-compose run gnuradio-whisper make
```

## Usage

```bash
# Basic test
docker-compose run gnuradio-whisper make test

# Compare clean vs noisy vs very noisy
docker-compose run gnuradio-whisper make test-compare

# Full comparison with summary
docker-compose run gnuradio-whisper make test-georges-full

# Manual usage with custom options
docker-compose run gnuradio-whisper ./build/gnuradio_pipeline \
    -i samples/georges/georges_opssat_noisy.wav \
    -o output/transcription.txt \
    -m models/ggml-tiny.bin \
    -n 0.7

# Save processed audio for inspection
docker-compose run gnuradio-whisper ./build/gnuradio_pipeline \
    -i samples/georges/georges_opssat_clean.wav \
    -o output/georges_opssat_clean.txt \
    -p output/georges_opssat_clean_processed.wav \
    -m models/ggml-tiny.bin
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-i <file>` | Input WAV file | Required |
| `-o <file>` | Output transcription file (.txt) | Required |
| `-m <file>` | Whisper model path | models/ggml-tiny.bin |
| `-p <file>` | Save processed audio to file | - |
| `-l <freq>` | Lowpass cutoff frequency | 4000 |
| `-b <freq>` | Bandpass low frequency | 300 |
| `-B <freq>` | Bandpass high frequency | 3400 |
| `-n <strength>` | Noise reduction strength (0.0-1.0) | 0.5 |
| `-L <lang>` | Language code | en |
| `-v` | Verbose output | Off |

### Make Targets

| Target | Description |
|--------|-------------|
| `make` | Build pipeline |
| `make test` | Run basic test |
| `make test-compare` | Compare clean vs noisy vs very noisy |
| `make test-georges-full` | Full comparison with summary |
| `make test-opssat` | Test with OPS-SAT sample |
| `make test-with-audio` | Test and save processed audio |
| `make package` | Create SEPP deployment package |

## Samples

| Sample | Description |
|--------|-------------|
| `samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav` | Original OPS-SAT capture |
| `samples/georges/georges_opssat_clean.wav` | Clean audio (OPS-SAT format) |
| `samples/georges/georges_opssat_noisy.wav` | With simulated RF noise (a=0.03) |
| `samples/georges/georges_opssat_very_noisy.wav` | With heavy RF noise (a=0.1) |

### Converting Audio to OPS-SAT Format

```bash
# Clean
ffmpeg -i input.mp3 -ar 46875 -ac 2 -sample_fmt s16 output_clean.wav

# With noise
ffmpeg -i input.mp3 \
    -filter_complex "anoisesrc=d=23:c=white:a=0.03[noise];[0:a][noise]amix=inputs=2:duration=shortest" \
    -ar 46875 -ac 2 -sample_fmt s16 output_noisy.wav
```

## SEPP Deployment

```bash
# Create package
docker-compose run gnuradio-whisper make package
```

Creates `package/exp4023-gnuradio-whisper-v1.tar.gz`.

On the SEPP:

```bash
tar -xzf exp4023-gnuradio-whisper-v1.tar.gz
./run input.wav output.txt
```

## Project Structure

```
gnuradio-whisper/
├── Dockerfile              # Alpine ARM with GNU Radio + Whisper
├── docker-compose.yml
├── Makefile
├── README.md
├── run                     # SEPP deployment runner
├── src/
│   └── gnuradio_pipeline.cpp   # Single binary: GNU Radio + Whisper
├── build/
├── models/
│   └── ggml-tiny.bin
└── output/
```

## Model Selection

| Model | Size   | Speed   | Quality |
|-------|--------|---------|---------|
| tiny  | 75 MB  | Fastest | Basic   |
| base  | 142 MB | Fast    | Good    |
| small | 466 MB | Medium  | Better  |

### Download Models

```bash
cd models
curl -L -o ggml-tiny.bin "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"
```

## References

- [GNU Radio](https://wiki.gnuradio.org/)
- [whisper.cpp](https://github.com/ggerganov/whisper.cpp)
- [OPS-SAT Payloads](https://opssat.esa.int/pretty/payloads/)
