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

# Single SDR capture against emulator
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

### Full run script test

The `run` script accepts a config file via the `PRETTY_CONFIG` environment variable (defaults to `config.cfg`). To test the full 3-run sequence against the emulator:

```bash
docker-compose -f docker-compose.emu-test.yml up -d sdr-emu
sleep 3

docker-compose -f docker-compose.emu-test.yml run --rm \
    -e PRETTY_CONFIG=/app/config.emu.cfg pretty-doomed ./run

docker-compose -f docker-compose.emu-test.yml down
```

This executes all 3 runs with resource monitoring, per-run config copies, and `doom_force_trigger=true`. The emulator has finite sample data, so captures in Runs 2-3 may time out. Restart the emulator between runs if needed. On real hardware this is not an issue.

### Config differences (config.emu.cfg vs config.cfg)

| Parameter | Hardware (config.cfg) | Emulator (config.emu.cfg) |
|-----------|----------------------|--------------------------|
| `sdr_uri` | `local:` (default) | `ip:sdr-emu:30431` |
| `sdr_duration` | `20` (default) | `1` (emulator has limited sample data) |
| `sdr_min_readback` | `false` (default) | `true` (emulator ignores config writes) |
| `sdr_timeout_multiplier` | `5` (default) | `60` (emulator is slow under Rosetta/QEMU) |

### Emulator limitations

- **Finite sample data.** The emulator replays a fixed sample file. Capture duration must fit within the file (1s works, 5s+ times out). The second capture in a multi-capture run may receive less data since the emulator's connection state is not fully reset between captures.
- **No config reflection.** The emulator accepts AD9361 parameter writes but does not update readback attributes. `sdr_min_readback=true` downgrades the sample rate readback check to a warning.
- **Single connection.** The emulator only accepts one IIO connection at a time. A 5-second cleanup delay between captures allows the previous connection to close.
- **No SIGFPE.** The pretty-doomed binary runs natively on x86_64, avoiding the intermittent SIGFPE that affects ARM32 binaries under QEMU (see SEPP emulator section below).
- **No hardware FIR.** The AD9361 hardware FIR (`sdr_hw_fir_enable`) cannot be used with the emulator. The FIR configuration via `libad9361-iio` requires TX channels which the IIO emulator does not expose. Set `sdr_hw_fir_enable=false` in `config.emu.cfg`.

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

The `run` script executes three runs. Each run copies `config.cfg` to its run directory and appends per-run overrides (last value wins). All runs force-trigger DOOM regardless of detection.

1. **Run 1: Baseline** (2 x 20s captures, background, `sdr_hw_fir_enable=false`): software-only decimation
2. **Run 2: Hardware FIR** (2 x 20s captures, background, `sdr_hw_fir_enable=true`): AD9361 hardware FIR at 600 kSPS
3. **Run 3: Hardware FIR** (2 x 20s captures, sequential, `sdr_hw_fir_enable=true`): same as Run 2 but sequential to isolate FIR improvement from CPU contention

```bash
# On the SEPP (after deploying the package)
./run
```

The `run` script accepts a config file via `PRETTY_CONFIG` (defaults to `config.cfg`):

```bash
# Use a different base config
PRETTY_CONFIG=config.emu.cfg ./run
```

### Expected output structure

```
toGround/
├── doom_demo_index.txt          # Demo cycling state
├── results.txt                  # Append-only log of DOOM executions
├── run-00001/                   # Baseline (2x20s, background, sw decimation)
│   ├── resource.csv             # Per-second CPU + memory utilization
│   ├── config.cfg               # Per-run config copy (base + overrides)
│   ├── pretty-doomed.log        # All output with [cN/tM] thread tags
│   ├── capture-001/
│   │   ├── run.log             # Detailed capture + processing log
│   │   ├── capture.wav
│   │   ├── capture.sc16
│   │   ├── spectrogram.bmp
│   │   ├── constellation.bmp
│   │   ├── processed.wav
│   │   ├── transcription.txt
│   │   ├── scores.txt
│   │   ├── summary.txt
│   │   ├── postcard.png
│   │   └── gl-e1m2b/           # DOOM output (force-triggered)
│   └── capture-002/
│       └── ...
├── run-00002/                   # HW FIR (2x20s, background)
│   ├── resource.csv
│   ├── config.cfg
│   ├── pretty-doomed.log
│   ├── capture-001/
│   │   └── ...
│   └── capture-002/
│       └── ...
└── run-00003/                   # HW FIR (2x20s, sequential)
    ├── resource.csv
    ├── config.cfg
    ├── pretty-doomed.log
    ├── capture-001/
    │   └── ...
    └── capture-002/
        └── ...
```

### Capture duration vs wall time

SDR captures on the SEPP take significantly longer than the configured `sdr_duration`. A 20-second capture (4M I/Q samples at 200 kHz effective rate) typically completes in 50 to 70 seconds of wall time. The ARM cores cannot process IIO DMA buffers fast enough to match the configured 2.4 MSPS sample rate, so the GNU Radio flowgraph receives samples at roughly 40% of the expected throughput.

This has two implications:

1. **The capture window is longer than configured.** The SDR is actively receiving RF for the full wall-time duration (50 to 70s), not just the configured 20s. All transmitted audio during that window is captured. Someone transmitting a voice command must do so during this extended window.

2. **CPU contention slows later captures.** In background mode, capture #2 runs concurrently with STT inference for capture #1 on the other core. This memory bus and cache contention further reduces DMA throughput. In the v3 EM run, capture #1 completed in about 51s while capture #2 took about 72s for the same 20s of configured audio.

The `sdr_timeout_multiplier` config parameter (default 5) accounts for this: `timeout = multiplier * sdr_duration + 10`. With 20s duration and 5x multiplier, the timeout is 110s.

No I/Q data is lost. The flowgraph buffers all samples as they arrive. The resulting audio is the same 20 seconds at 16 kHz regardless of wall time. Check the `Progress` lines in the log to monitor actual throughput: `Progress [Ns]: I/Q current/total`.

### What to check in EM results

- **All runs**: `results.txt` should have one entry per capture with `trigger=force`. If missing, the per-run config copy failed to apply `doom_force_trigger=true`.
- **Run 2 vs Run 3**: compare `pretty-doomed.log` total times. Run 3 (`stt_concurrent_load=true`) should start its first capture ~16s sooner than Run 2.
- **Run 2 vs Run 3**: check `resource.csv` for CPU contention. If Run 3 shows SDR capture failures, `stt_concurrent_load` may need to be disabled.
- **Capture wall time**: check `Progress` lines for I/Q throughput. If captures take >100s for 20s configured duration, CPU contention may be too high.
- **capture-NNN/run.log** (sequential) or **pretty-doomed.log** (background): check I/Q diagnostics (zero fraction should be <1% for both I and Q).
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
