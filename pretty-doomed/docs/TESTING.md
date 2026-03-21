# Testing Guide

Three test environments, each with different capabilities and limitations.

## 1. Local (no SDR hardware)

Tests the pipeline with pre-recorded WAV files. No IIO, AD9361, or emulator needed.

```bash
# Build
docker-compose run --rm pretty-doomed make clean all

# Unit tests (pure C++17, no external deps)
docker-compose run --rm pretty-doomed make test

# DSP integration tests (GNU Radio filters + resampling)
docker-compose run --rm pretty-doomed make test-dsp

# File input pipeline (single file)
docker-compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -i input/georges_01.wav -c config.cfg -f variants.cfg \
    -o toGround/test -d demos -e doom-build/local/opssat-doom

# Full run script (file input phase works, SDR phases fail gracefully)
docker-compose run --rm pretty-doomed ./run
```

**What it tests:** config parsing, DSP filters, STT transcription, fuzzy matching, DOOM execution, demo cycling, log output, multi-run orchestration.

**What it does NOT test:** IIO connection, AD9361 config write/readback, GNU Radio IIO flowgraph, SDR capture pipeline.

## 2. SDR Emulator

Tests the full SDR capture pipeline against a software IIO emulator (`iio-emu`). The emulator replays a pre-recorded sample file over TCP.

```bash
# Build (uses docker-compose.emu-test.yml which includes sdr-emu service)
docker-compose -f docker-compose.emu-test.yml run --rm pretty-doomed make clean all

# Start emulator
docker-compose -f docker-compose.emu-test.yml up -d sdr-emu
sleep 3

# Run SDR capture against emulator
docker-compose -f docker-compose.emu-test.yml run --rm pretty-doomed \
    ./build/local/pretty-doomed -s -c config.emu.cfg -f variants.cfg \
    -o toGround/emu-test -d demos -e doom-build/local/opssat-doom

# Stop emulator
docker-compose -f docker-compose.emu-test.yml down

# Check results
cat toGround/emu-test/pretty-doomed.log
cat toGround/emu-test/capture-001/run.log
ls toGround/emu-test/capture-001/
```

### Config differences (config.emu.cfg vs config.cfg)

| Parameter | Hardware (config.cfg) | Emulator (config.emu.cfg) |
|-----------|----------------------|--------------------------|
| `sdr_uri` | `local:` (default) | `ip:sdr-emu:30431` |
| `sdr_duration` | `20` (default) | `1` (emulator has limited sample data) |
| `sdr_min_readback` | `false` (default) | `true` (emulator ignores config writes) |
| `sdr_timeout_multiplier` | `5` (default) | `60` (emulator is slow under Rosetta/QEMU) |
| `sdr_captures` | `3` (default) | `2` |
| `process_mode` | `sequential` (default) | `sequential` |

### Emulator limitations

- **Finite sample data.** The emulator replays a fixed sample file. Capture duration must fit within the file (1s works, 5s+ times out). The second capture in a multi-capture run may receive less data since the emulator's connection state is not fully reset between captures.
- **No config reflection.** The emulator accepts AD9361 parameter writes but does not update readback attributes. `sdr_min_readback=true` downgrades the sample rate readback check to a warning.
- **Single connection.** The emulator only accepts one IIO connection at a time. A 5-second cleanup delay between captures allows the previous connection to close.
- **No SIGFPE.** The pretty-doomed binary runs natively on x86_64, avoiding the intermittent SIGFPE that affects ARM32 binaries under QEMU (see SEPP emulator section below).

**What it tests:** IIO connection, AD9361 config write/readback, GNU Radio IIO flowgraph (device_source, LPF, FM demod, resampler, bandpass), sc16/WAV output, spectrogram/constellation BMP generation, RMS normalization, multi-capture loop, STT on captured audio.

**What it does NOT test:** real RF reception, actual AD9361 hardware behavior, ARM32 performance (STT timing, memory pressure), satellite pass timing.

## 2b. SEPP Emulator (ARM32 under QEMU)

Tests the actual ARM32 binary under QEMU emulation against the IIO emulator. This validates the exact binary that ships to the EM but is significantly slower and subject to intermittent SIGFPE crashes.

```bash
# Prerequisites: QEMU ARM emulation, iio-emu image, pre-built SEPP binary
docker run --rm --privileged tonistiigi/binfmt --install arm
docker load -i resources/internal/sdr_emu_testing/sdr_emu.tar  # if pruned

# Start emulator + run ARM32 binary
docker-compose -f docker-compose.sepp-emu-test.yml up -d sdr-emu
sleep 3
docker-compose -f docker-compose.sepp-emu-test.yml run --rm pretty-doomed-sepp \
    sh -c 'export LD_LIBRARY_PATH="/app/libs-gnuradio/lib:$LD_LIBRARY_PATH" && \
    ./build/sepp/pretty-doomed -s -c config.emu.cfg -f variants.cfg \
    -o toGround/sepp-emu-test -d demos -e doom-build/sepp/opssat-doom'
docker-compose -f docker-compose.sepp-emu-test.yml down

# With QEMU_STRACE for debugging crashes
docker-compose -f docker-compose.sepp-emu-test.yml run --rm \
    -e QEMU_STRACE=1 pretty-doomed-sepp \
    sh -c '...' > toGround/strace.log 2>&1
```

### SEPP emulator limitations

All x86_64 emulator limitations apply, plus:

- **Very slow.** STT model loading takes ~7s under QEMU (vs 0.7s on x86_64, ~26s on native ARM). QEMU_STRACE makes it even slower.
- **Intermittent SIGFPE.** The ARM32 binary can crash with `qemu: uncaught target signal 8 (Arithmetic exception)` during GNU Radio flowgraph execution. Strace analysis shows this is a deliberate `tkill(SIGFPE)` triggered by IIO buffer read timeouts, not a hardware FPU trap. It does not occur on native ARM or x86_64.
- **LD_LIBRARY_PATH required.** The pre-built GNU Radio libs are mounted at `/app/libs-gnuradio/lib` and must be added to `LD_LIBRARY_PATH` manually.

**When to use:** only when you need to validate the exact ARM32 binary before shipping to the EM. For regular development and pipeline testing, use the x86_64 emulator test (section 2).

## 3. EM/Hardware (Engineering Model)

Tests on the OPS-SAT flatsat with real AD9361 hardware. Uses `config.cfg` with default SDR parameters.

The `run` script executes three phases:

1. **File input** (georges_01.wav): verifies DOOM command detection on known audio
2. **SDR sequential** (2 x 20s captures): capture all, then process all
3. **SDR background** (2 x 20s captures): process previous capture while next one runs

```bash
# On the SEPP (after deploying the package)
./run
```

### Expected output structure

```
toGround/
├── doom_demo_index.txt          # Demo cycling state
├── run-00001/                   # File input (georges_01.wav)
│   ├── pretty-doomed.log
│   ├── processed.wav
│   ├── transcription.txt
│   ├── scores.txt
│   ├── summary.txt
│   └── e1m7-607/               # DOOM output (expected: command detected)
├── run-00002/                   # SDR sequential (2x20s)
│   ├── pretty-doomed.log       # Dispatch: capture 1/2, capture 2/2, processing
│   ├── config.cfg              # Effective config (base + overrides)
│   ├── capture-001/
│   │   ├── run.log             # Detailed capture + processing log
│   │   ├── capture.wav
│   │   ├── capture.sc16
│   │   ├── spectrogram.bmp
│   │   ├── constellation.bmp
│   │   ├── processed.wav
│   │   ├── transcription.txt
│   │   ├── scores.txt
│   │   └── summary.txt
│   └── capture-002/
│       └── ...
└── run-00003/                   # SDR background (2x20s)
    ├── pretty-doomed.log        # Dispatch: includes background processing timing
    ├── config.cfg
    ├── capture-001/
    │   └── ...
    └── capture-002/
        └── ...
```

### What to check in EM results

- **run-00001**: `summary.txt` should show "COMMAND DETECTED". If not, STT or detection has a regression.
- **run-00002 vs run-00003**: compare `pretty-doomed.log` total times. Background mode should be faster (overlaps processing with capture).
- **capture-NNN/run.log**: check I/Q diagnostics (zero fraction should be <1% for both I and Q). If Q zeros are high, there may be a TX/RX contention issue (not expected in RX-only mode).
- **constellation.bmp**: visual I/Q health check. Should show a diffuse cloud, not a horizontal line (Q dropout).
- **transcription.txt**: STT output from captured RF. Will be noise unless someone is transmitting voice on 1296 MHz during the capture.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Could not connect to IIO at local:` | No AD9361 hardware | Expected in local testing. Use emulator or EM. |
| `Could not connect to IIO at ip:sdr-emu:30431` | Emulator not running or stale connection | Restart: `docker-compose -f docker-compose.emu-test.yml restart sdr-emu` |
| `Unable to create buffer: -12` | DMA buffer allocation failed (ENOMEM) | Reduce `iio_buffer_size` or `sdr_captures` |
| `Audio too short for STT` | Capture timed out with too few samples | Increase `sdr_timeout_multiplier` or reduce `sdr_duration` |
| `Transcription produced no output` | STT model failed silently | Check model paths in config, verify model files exist |
| `SIGFPE` / `Arithmetic exception` | Deliberate `tkill(SIGFPE)` triggered by IIO read timeouts under QEMU ARM32 | Only affects ARM32 under QEMU. Use x86_64 emulator test or native ARM (EM). Run with `QEMU_STRACE=1` to capture the crash context. |
| Background processing slower than sequential | STT is CPU-bound on single core | Expected if only one core available. Background mode helps on dual-core (SEPP). |
