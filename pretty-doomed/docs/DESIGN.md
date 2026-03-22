# PRETTY DOOMed - Design Document

## Overview

PRETTY DOOMed is a voice-command-to-DOOM pipeline for the OPS-SAT spacecraft. It processes audio captured from amateur radio transmissions, applies signal conditioning, performs speech-to-text transcription, detects voice commands through fuzzy matching, and launches DOOM when the command is recognized.

## Pipeline

```
WAV File ──> Lowpass Filter ──> Bandpass Filter ──> Resample ──> Sherpa-ONNX STT ──> Fuzzy Match ──> DOOM
 (48 kHz)     (FIR, cutoff      (FIR, 300-3400     (to 16 kHz)   (offline, int8      (wake word,
               3400 Hz)           Hz voice band)                    ~27 MB model)      call sign,
                                                                                        command)
```

## Architecture

The project is split into modules by single responsibility, with external library dependencies isolated to minimize coupling.

### Module Dependency Map

```
main.cpp (orchestrator)
├── config.h         [pure C++17]
├── audio_io.h       [libsndfile]
├── dsp.h            [GNU Radio]
├── transcriber.h    [sherpa-onnx]
├── matcher.h        [pure C++17]
├── executor.h       [pure C++17]
├── output.h         [pure C++17]
├── postcard.h       [stb, FFTW]
├── capture.h        [GNU Radio, libiio]
└── pipeline.h       [all of the above]
```

### Module Responsibilities

| Module | File | Responsibility | External Deps |
|--------|------|----------------|---------------|
| Config | `config.cpp` | Parse `config.cfg` (KEY=VALUE, including model paths) and `variants.cfg` (TARGET=V1,V2,...) | None |
| Audio I/O | `audio_io.cpp` | Read/write WAV files, stereo-to-mono downmix | libsndfile |
| DSP | `dsp.cpp` | GNU Radio FIR filter blocks (lowpass, bandpass) + linear interpolation resampling | GNU Radio |
| Transcriber | `transcriber.cpp` | Sherpa-ONNX offline recognition: init, feed audio, decode, get text. Persistent instance, model loaded once and reused across multiple captures. | sherpa-onnx |
| Matcher | `matcher.cpp` | Fuzzy matching with Levenshtein distance + variant lookup, command detection | None |
| Executor | `executor.cpp` | Fork+exec DOOM binary for each demo file | None |
| Output | `output.cpp` | Summary + log output formatting (ASCII art, scores) | None |
| Postcard | `postcard.cpp` | DOOM-themed composite image: frame, I/Q blood splatter, FFTW spectrogram, logos, metadata. Uses PLAYPAL palette. | stb, FFTW |
| Capture | `capture.cpp` | AD9361 SDR capture via GNU Radio IIO flowgraph (device_source, LPF, FM demod, resampler, bandpass). Writes WAV and sc16 files, generates spectrogram/constellation BMP. | GNU Radio, libiio |
| Pipeline | `pipeline.cpp` | WAV processing pipeline: DSP filtering, STT transcription, command detection, DOOM execution. Orchestrates the per-capture processing sequence. | All |
| Main | `main.cpp` | CLI arg parsing, input mode dispatch (file vs SDR), multi-capture loop, process_mode (sequential/background), output file writing | All |

### Dependency Isolation

Four modules are pure C++17 with zero external dependencies:

- **config** -- string parsing (including model paths)
- **matcher** -- string matching algorithms
- **executor** -- DOOM process execution
- **output** -- summary and log formatting

These are fully unit-testable without installing GNU Radio, sherpa-onnx, or libsndfile. The test binary links only these pure modules. DSP integration tests require GNU Radio and run via `make test-dsp`.

### Multi-Capture Loop and Processing Modes

In SDR capture mode (`-s`), main.cpp drives a multi-capture loop controlled by `sdr_captures`. Two processing modes are available via `process_mode`:

- **sequential**: all N captures run first, then all N are processed through the pipeline. Simpler, no concurrency.
- **background**: after the first capture completes, processing of the previous capture runs on a background thread while the next capture runs. Reduces total wall time on multi-core systems (e.g., the OPS-SAT SEPP dual-core ARM).

The `Transcriber` instance is created once at startup and reused across all captures, avoiding repeated model loading (~27 MB).

## Configuration

All parameters are externalized in two config files, not compiled into the binary.

### `config.cfg` -- Pipeline Parameters

```ini
# Signal Processing
lowpass_cutoff=3400
bandpass_low=300
bandpass_high=3400
lowpass_transition=500
bandpass_transition=100

# Speech-to-Text
model_encoder=models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx
model_decoder=models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx
model_joiner=models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx
model_tokens=models/sherpa-onnx/small/tokens.txt
decoding_method=modified_beam_search
num_threads=1

# Detection
wake_word=PRETTY
call_signs=NIGHT,LIGHT,HEART,...
command=DOOM,PLAY DOOM
fuzzy_max_distance=1

# DOOM Frame Capture
# Per-demo: integer=snapshot, range=GIF, -1=random, list=cycling
doom_frames_e1m7-607=8000,7992-8025
doom_frames_impfight=-1
doom_frames_m1-fast=400,300,500,100
doom_frames_m1-normal=-1
doom_frames_m1-simple=-1
doom_maxframes_impfight=2030
doom_maxframes_m1-normal=1785
doom_maxframes_m1-simple=700

# Demo cycling order (GIF-producing demos first for richer first-run output)
# If not set, cycles alphabetically through all .lmp files in demos/
doom_demo_order=e1m7-607,impfight,m1-fast,m1-normal,m1-simple
```

### SDR Capture Parameters

Used with the `-s` flag. All optional, with defaults matching sdr-capture.

| Config Key | Default | Description |
|------------|---------|-------------|
| `sdr_frequency` | 1296000000 | RX frequency in Hz |
| `sdr_rate` | 2400000 | AD9361 sample rate in Hz |
| `sdr_decimation` | 12 | LPF decimation factor |
| `sdr_rf_bandwidth` | 200000 | AD9361 analog filter bandwidth in Hz |
| `sdr_gain` | 50 | RX gain in dB |
| `sdr_fm_deviation` | 5000 | FM deviation in Hz |
| `sdr_uri` | local: | IIO URI (local: for hardware, ip:host:port for emulator) |
| `sdr_duration` | 20 | Capture duration in seconds |
| `sdr_max_iq_mb` | 20 | Max I/Q file size in MiB |
| `sdr_audio_rate` | 16000 | Output audio sample rate in Hz |
| `sdr_timeout_multiplier` | 5 | Timeout = duration * N + 10 seconds |
| `sdr_min_readback` | false | Downgrade sample rate readback mismatch to warning |
| `sdr_enable_spectrogram` | true | Generate spectrogram BMP |
| `sdr_enable_constellation` | true | Generate I/Q constellation BMP |
| `sdr_captures` | 3 | Number of sequential SDR captures |
| `process_mode` | sequential | Processing mode: `sequential` or `background` |
| `doom_force_trigger` | false | Force DOOM launch regardless of detection (for testing) |
| `doom_enable_postcard` | true | Generate DOOM-themed composite postcard after each run |
| `doom_postcard_scale` | 1 | Postcard output resolution multiplier (1 for 1x, 2 for 2x...) |

### `variants.cfg` -- Fuzzy Match Variants

Each line maps a target word to known misrecognition patterns derived from BPE token decomposition analysis:

```ini
PRETTY=PRETY,BRETTY,PREDDY
DOOM=DOM,DUM,DUME
NIGHT=KNIGHT,NITE,NIGH
```

The BPE model decomposes words into subword tokens. Words with fewer tokens are recognized more reliably. Misrecognition patterns follow predictable BPE decomposition boundaries.

## Voice Command Format

```
<wake_word>, THIS IS <call_sign>, PLAY <command>.
```

Example: "PRETTY, THIS IS NIGHT, PLAY DOOM."

### Word Selection Rationale

| Word | BPE Tokens | Rationale |
|------|-----------|-----------|
| PRETTY | 2 (PRE + TY) | Reliable wake word, distinct from common speech |
| PLAY | 2 (PLA + Y) | Strong verb token |
| DOOM | 2 (DO + OM) | Target application |

### Call Sign Tiers

**Tier 1 (single BPE token, best reliability):** NIGHT, LIGHT, HEART, POWER, VOICE, HORSE, HOUSE, DARK, HOME, WORLD, COUNT, WONDER, PRINCE, WHITE, FRIEND, OPEN, PLACE

**Tier 2 (NATO phonetic alphabet, 2-5 tokens):** ALPHA through ZULU

## Fuzzy Matching Strategy

The matcher scans the transcription word-by-word:

1. **Uppercase + split** the transcription into words
2. For each word, check against the wake word, all call signs, and the command
3. A word matches if it equals the target, equals any variant, or is within Levenshtein distance `fuzzy_max_distance` of the target or any variant
4. Count occurrences of each match type
5. Command is detected if `command_count >= 1`

## Testing

Unit tests use [doctest](https://github.com/doctest/doctest), a single-header C++17 test framework.

### Testable Modules

| Module | Test File | Coverage |
|--------|-----------|----------|
| matcher | `test_matcher.cpp` | Levenshtein distance, fuzzy matching, variant lookup, full detection |
| config | `test_config.cpp` | KEY=VALUE parsing, variants parsing, edge cases |
| executor | `test_executor.cpp` | Demo file discovery, frame resolution, cycling logic |
| output | `test_output.cpp` | Summary formatting, ASCII art rendering |
| dsp | `test_dsp.cpp` | FIR convolution correctness, resampling ratios |

### Running Tests

```bash
make test
```

The test binary links only pure C++17 modules, no Docker or external libraries needed.

## Output Structure

### File Input Mode

Each pipeline run produces:

```
toGround/run-00001/
├── pretty-doomed.log       # Full pipeline log
├── processed.wav           # Filtered audio
├── transcription.txt       # Transcription text
├── scores.txt              # Detection scores (exact/approximate breakdown)
├── summary.txt             # Human-readable summary
├── doom.log                # DOOM stdout/stderr (if triggered)
├── results.log             # Statdump validation (OK/ERROR per demo)
├── postcard.png            # DOOM-themed composite postcard (if enabled)
└── e1m7-607/               # DOOM demo output (one per run, cycling)
    ├── stats.txt
    ├── frame-NNNNNN.jpg    # Snapshot (random, cycling, or fixed)
    └── frames-007992-008025.gif  # Animated GIF (if dash range configured)
```

### SDR Capture Mode

In SDR mode, each capture gets its own subdirectory with capture artifacts (WAV, sc16, spectrogram, constellation) alongside the pipeline outputs:

```
toGround/run-00003/
├── pretty-doomed.log       # Dispatch log (capture/processing progress)
├── config.cfg              # Effective config (base + overrides)
├── capture-001/            # Per-capture directory
│   ├── run.log             # Detailed capture + processing log
│   ├── capture.wav         # FM-demodulated audio
│   ├── capture.sc16        # Raw I/Q data
│   ├── spectrogram.bmp     # I/Q spectrogram
│   ├── constellation.bmp   # I/Q constellation
│   ├── processed.wav       # Filtered audio (pipeline output)
│   ├── transcription.txt   # STT output
│   ├── scores.txt
│   ├── summary.txt
│   ├── postcard.png        # DOOM-themed composite postcard (if enabled)
│   └── e1m7-607/           # DOOM output (if command detected)
├── capture-002/
│   └── ...
└── capture-003/
    └── ...
```

### scores.txt

Machine-readable detection scores with exact and approximate match breakdown. Points are weighted: exact matches score 2 points each, fuzzy/approximate matches score 1 point each.

```
wake_word_exact=4
wake_word_exact_matches=PRETTY,PRETTY,PRETTY,PRETTY
wake_word_approx=0
wake_word_approx_matches=
command_DOOM_exact=2
command_DOOM_exact_matches=DOOM,DOOM
command_DOOM_approx=5
command_DOOM_approx_matches=DO,DO,DO,DO,DO
command_PLAY_DOOM_exact=2
command_PLAY_DOOM_exact_matches=PLAY DOOM,PLAY DOOM
command_PLAY_DOOM_approx=5
command_PLAY_DOOM_approx_matches=UPLI DO,PLAY DO,PLAY DO,PLAY DO,PLAY DO
total_points=26
total_points_exact=16
total_points_approx=10
```

In this example: 8 exact matches * 2 = 16 points_exact, 10 approx matches * 1 = 10 points_approx, total = 26.

## SEPP Deployment

Target: Alpine Linux 3.21.3, ARM32 (armv7l), musl libc.

```
exp4023-pretty-DOOMed-v1/
├── run                     # Entrypoint
├── pretty-doomed           # Pipeline binary (sherpa-onnx statically linked)
├── opssat-doom             # DOOM binary (static)
├── config.cfg
├── variants.cfg
├── libs/                   # Bundled shared libraries (GNU Radio, Boost, etc.)
├── models/                 # Speech-to-text models
│   └── sherpa-onnx/
│       └── small/          # sherpa-onnx model (~27 MB)
├── demos/                  # doom.wad + demo files
├── input/                  # Sample WAV
└── toGround/
```

## Key Design Decisions

1. **In-memory processing** -- No intermediate files between pipeline stages. WAV is read once into a float buffer, filtered in-place, resampled, and fed directly to sherpa-onnx. In SDR capture mode, the capture flowgraph writes WAV and sc16 files for diagnostics, but the subsequent pipeline processing still operates in memory from the WAV read onward.

2. **GNU Radio for signal processing** -- Uses GNU Radio `top_block` with `vector_source_f` -> `fir_filter_fff` -> `vector_sink_f` for in-memory FIR filtering. Filter taps designed with `firdes`.

3. **Sherpa-ONNX over Whisper** -- The int8-quantized zipformer-small model is ~27 MB vs Whisper tiny at ~75 MB. Sherpa-ONNX also supports `modified_beam_search` which improves WER by 17% relative over greedy search.

4. **Variant-based matching over hotwords** -- Hotword boosting degrades accuracy with BPE models (disrupts learned word boundaries). Post-transcription fuzzy matching with externalized variants is more effective.

5. **Single-token call signs** -- Words that map to a single BPE token (NIGHT, LIGHT, etc.) are transcribed far more reliably than multi-token words (NATO alphabet).

6. **Externalized config** -- All thresholds, call signs, and variants in text files, not compiled constants. Allows tuning without rebuilding.

7. **Static linking for sherpa-onnx/ONNX Runtime** -- The pre-built `libonnxruntime.so` targets glibc. Loading it at runtime on Alpine/musl causes a segfault due to deep ABI incompatibilities that cannot be resolved with stub libraries. Sherpa-ONNX and ONNX Runtime are built as static libraries (`BUILD_SHARED_LIBS=OFF`) and linked directly into the `pretty-doomed` binary, resolving all ONNX Runtime symbols at link time via glibc compatibility stubs (`glibc_compat.o`). GNU Radio and other dependencies remain as bundled shared libraries.
