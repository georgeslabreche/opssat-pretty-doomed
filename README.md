# PRETTY DOOMed

First voice command sent to a spacecraft — playing DOOM from orbit via amateur radio.

## What

A radio amateur transmits a voice command to ESA's OPS-SAT PRETTY spacecraft. The onboard pipeline filters the audio signal, transcribes speech, detects the command, and launches a DOOM demo playback. Frame captures, level stats, and transcription are downlinked.

```
Voice (UHF) --> Lowpass --> Bandpass --> Resample --> STT --> Match --> DOOM --> Downlink
```

## Repository

| Directory | Description |
|-----------|-------------|
| [`pretty-doomed/`](pretty-doomed/) | Voice-command-to-DOOM pipeline (C++17, GNU Radio, Sherpa-ONNX) |
| [`doom/`](doom/) | Headless DOOM engine with JPEG/GIF frame capture |
| [`sandbox/`](sandbox/) | Experiments: signal processing, denoising, STT evaluation |
| [`docs/`](docs/) | Project proposal and reference material |

## Quick Start

```bash
cd pretty-doomed

# Build
docker-compose build
docker-compose run --rm pretty-doomed make all doom

# Download models (see models/README.md)
# ...

# Run (single file or entire directory)
docker-compose run --rm pretty-doomed sh run input/georges_01.wav
docker-compose run --rm pretty-doomed sh run input/

# Test
docker-compose run --rm pretty-doomed make test
```

See [`pretty-doomed/README.md`](pretty-doomed/README.md) for full build, run, and deployment docs.

## Voice Command Format

```
PRETTY, THIS IS <CALL_SIGN>, PLAY DOOM.
```

## Output

Each run produces a numbered directory downlinked to ground:

```
toGround/run-00001/
├── transcription.txt       # What the STT heard
├── summary.txt             # Human-readable summary with ASCII art
├── scores.txt              # Detection scores
├── processed.wav           # Filtered audio
├── impfight/               # DOOM demo output
│   ├── frame-000700.jpg    # Captured frame
│   └── stats.txt           # Level statistics
└── ...
```

## Sandbox

Earlier experiments that informed the final pipeline design:

- **Signal processing** — [`sandbox/gnuradio/`](sandbox/gnuradio/) — GNU Radio lowpass/bandpass/squelch for ARM32
- **Denoising** — [`sandbox/denoising/`](sandbox/denoising/) — DTLN, spectral subtraction, adaptive filtering, Wiener, ALE
- **STT evaluation** — [`sandbox/speech-to-text/`](sandbox/speech-to-text/) — Vosk, Sherpa-ONNX, PocketSphinx comparison
- **Integration** — [`sandbox/integrations/`](sandbox/integrations/) — GNU Radio + Whisper end-to-end prototype

**Finding:** Classical and neural denoising methods all struggle with the non-stationary radio interference in OPS-SAT samples. The pipeline instead relies on bandpass filtering + a robust STT model with fuzzy matching.

## References

- [OPS-SAT](https://opssat.esa.int/)
- [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx)
- [GNU Radio](https://wiki.gnuradio.org/)
- [Project Proposal](docs/PROPOSAL.md)
- [SEPP Reference](docs/SEPP.md)
