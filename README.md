# PRETTY DOOMed

First voice command sent to a spacecraft — playing DOOM from orbit via amateur radio.

## What

A radio amateur transmits a voice command to ESA's OPS-SAT PRETTY spacecraft on 1296 MHz. The onboard SDR captures the RF signal, FM demodulates, filters, transcribes speech, detects the command, and launches a DOOM demo playback. Frame captures, level stats, I/Q diagnostics, and transcription are downlinked.

```
RF (1296 MHz) --> SDR Capture --> FM Demod --> Filter --> Resample --> STT --> Match --> DOOM --> Downlink
```

## Repository

| Directory | Description |
|-----------|-------------|
| [`pretty-doomed/`](pretty-doomed/) | Voice-command-to-DOOM pipeline (C++17, GNU Radio, Sherpa-ONNX) |
| [`doom/`](doom/) | Headless DOOM engine with JPEG/GIF frame capture |
| [`common/`](common/) | Shared C++ headers (logging, config, IIO, audio, spectrogram, constellation) |
| [`tools/`](tools/) | Ground-side visualization: HTML reports, spectrograms, I/Q analysis |
| [`sandbox/`](sandbox/) | Experiments: SDR capture/loopback, signal processing, denoising, STT evaluation |
| [`docs/`](docs/) | Project proposal and reference material |

## Quick Start

```bash
cd pretty-doomed

# Build
docker-compose build
docker-compose run --rm pretty-doomed make all doom

# Download models (see models/README.md)
# ...

# Run (file input + SDR captures)
docker-compose run --rm pretty-doomed ./run

# Test
docker-compose run --rm pretty-doomed make test
```

See [`pretty-doomed/README.md`](pretty-doomed/README.md) for full build, run, and deployment docs.

## Voice Command Format

```
PRETTY, THIS IS <CALL_SIGN>, PLAY DOOM.
```

## Output

Each run produces a numbered directory downlinked to ground with transcription, detection scores, filtered audio, and DOOM artifacts (if command detected). SDR capture runs also include I/Q data, spectrograms, and constellation plots per capture. See [`pretty-doomed/docs/DESIGN.md`](pretty-doomed/docs/DESIGN.md) for the full output structure.

## Sandbox

Earlier experiments that informed the final pipeline design:

- **Denoising** — [`sandbox/denoising/`](sandbox/denoising/) — DTLN, spectral subtraction, adaptive filtering, Wiener, ALE
- **STT evaluation** — [`sandbox/speech-to-text/`](sandbox/speech-to-text/) — Vosk, Sherpa-ONNX, PocketSphinx comparison
- **Signal processing** — [`sandbox/gnuradio/`](sandbox/gnuradio/) — GNU Radio lowpass/bandpass/squelch for ARM32
- **SDR apps** — [`sandbox/gnuradio/sdr-capture/`](sandbox/gnuradio/sdr-capture/) and [`sdr-loopback/`](sandbox/gnuradio/sdr-loopback/) — standalone SDR experiments (validated on EM, loopback shelved due to DMA contention)
- **Integration** — [`sandbox/integrations/`](sandbox/integrations/) — GNU Radio + Whisper end-to-end prototype

**Finding:** Classical and neural denoising methods all struggle with the non-stationary radio interference in OPS-SAT samples. The pipeline instead relies on bandpass filtering + a robust STT model with fuzzy matching.

## References

- [OPS-SAT](https://opssat.esa.int/)
- [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx)
- [GNU Radio](https://wiki.gnuradio.org/)
- [Project Proposal](docs/PROPOSAL.md)
- [SEPP Reference](docs/SEPP.md)
