# Testing Guide

Three test environments, each with different capabilities and limitations.

## 1. Local (no SDR hardware)

Tests the pipeline with pre-recorded WAV files. No IIO, AD9361, or emulator needed.

```bash
# Build
docker-compose run --rm pretty-doomed make clean all

# Unit tests (pure C++17, no external deps)
docker-compose run --rm pretty-doomed make test

# Chain integration tests (GNU Radio chain params, narrowing, peak search)
docker-compose run --rm pretty-doomed make test-chain

# File input pipeline (single file)
docker-compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -i input/georges_01.wav -c config.cfg -f variants.cfg \
    -o toGround/test -d demos -e doom-build/local/opssat-doom

# File input with sc16 (tests postcard I/Q scatter rendering)
docker-compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -i input/georges_01.wav -q path/to/capture.sc16 -c config.cfg -f variants.cfg \
    -o toGround/test -d demos -e doom-build/local/opssat-doom

# Standalone postcard scatter test (no STT/DOOM, just postcard rendering)
docker-compose run --rm pretty-doomed ./build/local/test_postcard_scatter \
    <sc16_file> <frame_jpg> <output_png> [scale]

# Full run script (file input phase works, SDR phases fail gracefully)
docker-compose run --rm pretty-doomed ./run
```

**What it tests:** config parsing, STT transcription, fuzzy matching, DOOM execution, demo cycling, log output, multi-run orchestration, postcard I/Q scatter rendering (with `-q` flag or standalone test).

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

The `run` script accepts a config file via the `PRETTY_CONFIG` environment variable (defaults to `config.cfg`). To test the full run sequence against the emulator:

```bash
docker-compose -f docker-compose.emu-test.yml up -d sdr-emu
sleep 3

docker-compose -f docker-compose.emu-test.yml run --rm \
    -e PRETTY_CONFIG=/app/config.emu.cfg pretty-doomed ./run

docker-compose -f docker-compose.emu-test.yml down
```

This executes all runs defined in the `run` script with resource monitoring and per-run config copies. The emulator has finite sample data, so later captures may time out. Restart the emulator between runs if needed. On real hardware this is not an issue.

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

### Replaying flight captures

The emulator can replay real flight recordings instead of the default noise sample, turning the test into an end-to-end check against actual received RF (issue #114). Build a replay file from the raw Run 5 recordings with [`tools/src/make_emu_replay.py`](../../tools/README.md) (run from the repo's `tools/src/`):

```bash
python3 make_emu_replay.py <path-to-raw>/sdr_20260703_*.cs16     --output ../../sandbox/gnuradio/sdr-capture/emu-samples/run05_replay.cs16     --out-rate 4800000
```

Two things matter:

- **`--out-rate` is twice `sdr_rate`** (4.8 MSPS for the 2.4 MSPS emulator config): this iio-emu build consumes two complex samples per delivered sample, so a 1x-rate file plays back with all frequencies doubled.
- **Export `SDR_EMU_SAMPLE` for every compose invocation** (both `up` and `run`): `docker compose run` re-evaluates the `sdr-emu` service and recreates it with the default sample if the variable is not set.

```bash
export SDR_EMU_SAMPLE=run05_replay.cs16
docker-compose -f docker-compose.emu-test.yml up -d sdr-emu
sleep 3
docker-compose -f docker-compose.emu-test.yml run --rm pretty-doomed     ./build/local/pretty-doomed -s -c config.emu.cfg -f variants.cfg     -o toGround/emu-replay -d demos -e doom-build/local/opssat-doom
docker-compose -f docker-compose.emu-test.yml down
```

With `sdr_narrow_enable=true` in the config, expect per capture: a `Narrowing: peak at ...` log within a few hundred Hz of the injected offset (default -8 kHz; the residual is the snapshot's own Doppler drift), a regenerated `capture.wav` carrying the voice transmission, and voice-like fragments from the speech-to-text.

For a clean one-to-one mapping between recordings and outputs, build one replay per recording and restart the emulator for each (capture windows drift across snapshot boundaries in a concatenated replay):

```bash
for id in 192433 192511 192531 205727 205805 205825; do
    python3 make_emu_replay.py <path-to-raw>/sdr_20260703_${id}_*.cs16         --output ../../sandbox/gnuradio/sdr-capture/emu-samples/run05_${id}.cs16         --out-rate 4800000
done
# then per id: export SDR_EMU_SAMPLE=run05_<id>.cs16, force-recreate sdr-emu,
# and run one capture (sdr_captures=1) into its own output directory. Keep the
# capture duration below the replay length (e.g. 1.5 s for a 2 s recording) so
# the emulator does not run dry mid-capture.

**What it tests:** IIO connection, AD9361 config write/readback, GNU Radio IIO flowgraph (device_source, LPF, FM demod, resampler, bandpass), sc16/WAV output, spectrogram/constellation BMP generation, RMS normalization, multi-capture loop, STT on captured audio; with a flight-capture replay, reception and the narrowing stage against real signal.

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

The `run` script executes a single run using `config.cfg` directly. It copies `config.cfg` to the run directory for record-keeping. See the `run` script for the current layout.

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
└── run-NNNNN/
    ├── resource.csv             # Per-second CPU + memory utilization
    ├── config.cfg               # Per-run config copy (base + overrides)
    ├── pretty-doomed.log        # All output with [cN/tM] thread tags
    ├── psd-comparison.bmp       # Cross-capture PSD overlay (if multiple captures)
    └── capture-NNN/
        ├── capture.wav          # FM-demodulated audio
        ├── capture.sc16         # Raw I/Q (deleted unless sdr_keep_sc16=true)
        ├── capture-metrics.csv  # I/Q diagnostics: RMS, peak, PAPR, DC offset, imbalance
        ├── capture-psd.csv      # PSD frequency bins + power (dB/Hz)
        ├── capture-psd.bmp      # PSD line plot
        ├── spectrogram.bmp      # Time-frequency spectrogram
        ├── constellation.bmp    # I/Q constellation scatter
        ├── transcription.txt    # STT output
        ├── scores.txt           # Detection scores (exact vs fuzzy)
        ├── summary.txt          # Human-readable summary
        ├── postcard.png         # DOOM composite (if triggered)
        └── <demo-name>/         # DOOM output (if triggered)
```

### Capture duration vs wall time

With hardware FIR enabled (default since v4), SDR captures complete near the configured `sdr_duration` (20-23s for 20s configured). The AD9361 hardware FIR decimates from 2.4 MSPS to 600 kSPS before DMA, reducing ARM load to manageable levels.

Without hardware FIR (`sdr_hw_fir_enable=false`), captures take 50-70s for a 20s configuration. All 12x decimation happens in software at 2.4 MSPS, and the ARM cores can only sustain ~40% of real-time throughput. CPU contention from concurrent STT inference further degrades performance (72s in v3 EM tests).

In both cases, no I/Q data is lost. The flowgraph buffers all samples as they arrive. The resulting audio is the same 20 seconds at 16 kHz regardless of wall time.

The `sdr_timeout_multiplier` config parameter (default 2) accounts for this: `timeout = multiplier * sdr_duration + 10`. With 20s duration and 2x multiplier, the timeout is 50s. Increase to 5x if captures time out under heavy CPU contention.

### What to check in EM results

- **results.txt**: one entry per triggered capture. If `doom_force_trigger=true`, every capture should have an entry. If missing, the per-run config copy failed to apply the override.
- **Capture wall time**: check `Progress` lines for I/Q throughput. With hardware FIR, captures should complete near the configured duration (20-23s). Without FIR, expect 50-70s.
- **pretty-doomed.log**: check I/Q diagnostics (zero fraction should be <1% for both I and Q).
- **capture-metrics.csv**: per-capture I/Q diagnostics (RMS, peak, PAPR, DC offset, imbalance). RMS should be consistent across captures.
- **capture-psd.bmp / psd-comparison.bmp**: PSD plots should show the expected spectral shape. Cross-capture comparison should be consistent.
- **constellation.bmp**: visual I/Q health check. Should show a diffuse cloud, not a horizontal line (Q dropout).
- **postcard.png** (triggered captures only): I/Q scatter should show a smooth blood splatter pattern, not vertical stripes (see [V5_TO_V6.md](changelog/V5_TO_V6.md)).
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
