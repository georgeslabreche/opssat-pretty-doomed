# PRETTY DOOMed

Voice-command-to-DOOM pipeline for OPS-SAT PRETTY. Processes audio from amateur radio captures, applies signal conditioning, transcribes speech, detects commands, and launches DOOM.

## Pipeline

```
WAV ──> Lowpass ──> Bandpass ──> Resample ──> STT ──> Match ──> DOOM
```

Two input modes:
- **File input** (`-i`): reads a pre-recorded WAV file
- **SDR capture** (`-s`): N sequential RF captures from the AD9361 SDR, FM demodulated, then each WAV processed through the pipeline. STT model loaded once and reused across captures.

Two processing modes for SDR capture (configurable via `process_mode`):
- **sequential** (default): capture all N, then process all N
- **background**: process previous capture on a background thread while the next capture runs

In file input mode, all processing happens in memory with no intermediate files. In SDR capture mode, the captured audio is written to WAV and sc16 files for diagnostics before being fed into the pipeline.

## Quick Start

```bash
# Build (local x86_64)
docker-compose run --rm pretty-doomed make all doom

# Run full test schedule (file input + SDR phases)
docker-compose run --rm pretty-doomed ./run

# Run single file
docker-compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -i input/georges_01.wav -c config.cfg -f variants.cfg \
    -o toGround/test -d demos -e doom-build/local/opssat-doom
```

Download models before running, see [`models/README.md`](models/README.md).

## CLI Options

| Option | Description | Required |
|--------|-------------|----------|
| `-i <file>` | Input WAV file (48 kHz mono) | Yes (unless `-s`) |
| `-s` | SDR capture mode (capture RF audio from AD9361) | No |
| `-c <file>` | Pipeline config file | Yes |
| `-f <file>` | Fuzzy match variants file | Yes |
| `-o <dir>` | Output directory | Yes |
| `-d <dir>` | DOOM demo files directory | Yes |
| `-e <file>` | DOOM binary path | Yes |
| `-v` | Verbose output | No |

## Test

```bash
# Unit tests (pure C++17, no external deps)
docker-compose run --rm pretty-doomed make test

# DSP integration tests (requires GNU Radio)
docker-compose run --rm pretty-doomed make test-dsp
```

See [docs/TESTING.md](docs/TESTING.md) for local, emulator, and EM testing instructions.

## Voice Command Format

```
PRETTY, THIS IS <CALL_SIGN>, PLAY DOOM.
```

Example: "PRETTY, THIS IS GEORGES, PLAY DOOM."

## Documentation

- [docs/BUILDING.md](docs/BUILDING.md) -- Build instructions (local, SEPP, models, audio prep)
- [docs/DESIGN.md](docs/DESIGN.md) -- Architecture, modules, configuration, output structure
- [docs/TESTING.md](docs/TESTING.md) -- Local, emulator, and EM testing guide

## References

- [DOOM for OPS-SAT](../doom/README.md) -- Headless DOOM engine (built automatically by `make doom`)
- [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx)
- [GNU Radio](https://wiki.gnuradio.org/)
- [OPS-SAT](https://opssat.esa.int/)
