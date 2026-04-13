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
├── sdr.h            [libiio, libad9361]
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
| Postcard | `postcard.cpp` | DOOM-themed composite image: frame, I/Q blood splatter, FFTW spectrogram, logos, metadata. Uses PLAYPAL palette. Scatter uses adaptive range (0.35 for strong signals, 1.1 for weak) with dithering and alpha boost for weak signal visibility. | stb, FFTW |
| SDR | `sdr.cpp` | AD9361 lifecycle: `ad9361_configure()` (hardware FIR or software-only path with readback verification), `ad9361_cleanup_fir()` (disable FIR after captures). | libiio, libad9361 |
| Capture | `capture.cpp` | SDR capture via GNU Radio IIO flowgraph (device_source, LPF, FM demod, resampler, bandpass). Writes WAV and sc16 files. Artifact generation (spectrogram, constellation, PSD BMPs with axis labels, metrics CSV) runs async in background mode. | GNU Radio, libiio, FFTW |
| Pipeline | `pipeline.cpp` | WAV processing pipeline in two stages: `process_wav_stt()` (DSP, STT, detection) runs serially across captures; `process_wav_exec()` (DOOM, postcard, sc16 cleanup) runs async, overlapping with the next capture's STT. Combined `process_wav()` for sequential mode. | All |
| Main | `main.cpp` | CLI arg parsing (`-i` WAV input, `-q` sc16 input for postcard scatter, `-s` SDR capture), input mode dispatch, multi-capture loop, process_mode (sequential/background), SDR init/cleanup orchestration, output file writing | All |

### Dependency Isolation

Four modules are pure C++17 with zero external dependencies:

- **config** -- string parsing (including model paths)
- **matcher** -- string matching algorithms
- **executor** -- DOOM process execution
- **output** -- summary and log formatting

These are fully unit-testable without installing GNU Radio, sherpa-onnx, or libsndfile. The test binary links only these pure modules. DSP integration tests require GNU Radio and run via `make test-dsp`.

### Multi-Capture Loop and Processing Modes

In SDR capture mode (`-s`), main.cpp drives a multi-capture loop controlled by `sdr_captures`. Two processing modes are available via `process_mode`:

- **sequential**: all N captures run first, then all N are processed through the pipeline. Simpler, no concurrency. Each capture and its processing output is redirected to a per-capture `run.log` file.
- **background**: after each capture completes, processing runs on a background thread (`std::async`) while the next capture starts. Reduces total wall time on multi-core systems (e.g., the OPS-SAT SEPP dual-core ARM). The STT model is loaded before captures begin (sequential mode defers loading until after all captures complete).

The `Transcriber` instance is created once and reused across all captures, avoiding repeated model loading (~27 MB).

#### Logging in background mode

Background mode writes all output (captures and processing) to the single `pretty-doomed.log` file. Per-capture `run.log` files are not created because `stdout` is a process-global resource that cannot be safely redirected per-thread. When capture N+1 and processing of capture N run concurrently, their log lines interleave in chronological order.

Every log line includes a `[cN/tM]` tag (CPU core and thread ID) so interleaved output is unambiguous:

```
[2026-03-24 17:25:46.623][c1/t1]  Background processing of capture 1 started
[2026-03-24 17:25:46.623][c1/t1]  Waiting 5s for IIO cleanup...
[2026-03-24 17:25:46.623][c0/t38] --- Processing: capture-001/capture.wav ---
[2026-03-24 17:25:46.623][c0/t38] Reading audio: capture-001/capture.wav
  ...
[2026-03-24 17:25:51.632][c1/t1]  === Capture 2/2 ===
[2026-03-24 17:25:51.632][c1/t1]  SDR Capture: 1s at 200000 Hz effective
[2026-03-24 17:25:52.651][c0/t38] Transcribing (modified_beam_search)...
```

The `[cN/tM]` tags appear on all platforms running Linux (using `sched_getcpu()` and `gettid()`). On non-Linux builds the tags are omitted.

**Note on external processes**: The `[cN/tM]` tags only apply to threads within the `pretty-doomed` process. The DOOM binary runs as a separate process via `fork()+exec()`, so the DOOM phase in log-derived timelines reflects wall time spent waiting in the parent thread, not DOOM's actual CPU or thread utilization. For DOOM core attribution, cross-reference with the `resource.csv` per-core CPU data collected by the run script's resource monitor.

## Configuration

All config keys use semantic prefixes (`dsp_`, `stt_`, `detect_`, `doom_`, `sdr_`). An `operation` key selects what the voice command triggers (only `doom` for now).

See [CONFIG.md](CONFIG.md) for the full configuration reference.

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
| postcard | `test_postcard.cpp` | Postcard generation, frame discovery, graceful failure handling |
| dsp | `test_dsp.cpp` | FIR convolution correctness, resampling ratios |

### Running Tests

```bash
make test
```

The test binary links only pure C++17 modules, no Docker or external libraries needed.

## Output Structure

### Cross-Run Files

```
toGround/
├── doom_demo_index.txt     # Demo cycling state (persists across runs)
└── results.txt             # Append-only log of DOOM executions (timestamp, run, demo, trigger type, transcript)
```

`results.txt` is appended to after each successful DOOM execution, across all runs and captures.

### File Input Mode

Each pipeline run produces:

```
toGround/run-00001/
├── resource.csv            # Per-second CPU + memory utilization (from run script monitor)
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

### SDR Capture Mode (sequential)

In sequential mode, each capture gets its own subdirectory. Stdout is redirected to a per-capture `run.log` containing both capture and processing output:

```
toGround/run-00002/
├── resource.csv            # Per-second CPU + memory utilization
├── pretty-doomed.log       # Dispatch log (capture/processing progress)
├── psd-comparison.bmp      # Cross-capture PSD overlay (if sdr_enable_psd + multiple captures)
├── capture-001/            # Per-capture directory
│   ├── run.log             # Detailed capture + processing log
│   ├── capture.wav         # FM-demodulated audio
│   ├── capture.sc16        # Raw I/Q (deleted after processing unless sdr_keep_sc16=true)
│   ├── capture-metrics.csv # I/Q diagnostics: RMS, peak, PAPR, DC offset, imbalance
│   ├── capture-psd.csv     # PSD frequency bins + power (dB/Hz)
│   ├── capture-psd.bmp     # PSD line plot
│   ├── spectrogram.bmp     # I/Q spectrogram (with axis labels)
│   ├── constellation.bmp   # I/Q constellation (with axis labels)
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

### SDR Capture Mode (background)

In background mode, there are no per-capture `run.log` files. All output goes to `pretty-doomed.log` with `[cN/tM]` tags to distinguish interleaved capture and processing threads:

```
toGround/run-00003/
├── resource.csv            # Per-second CPU + memory utilization
├── pretty-doomed.log       # All output (captures + processing, tagged by thread)
├── psd-comparison.bmp      # Cross-capture PSD overlay
├── capture-001/
│   ├── capture.wav
│   ├── capture.sc16        # Deleted after processing unless sdr_keep_sc16=true
│   ├── capture-metrics.csv
│   ├── capture-psd.csv
│   ├── capture-psd.bmp
│   ├── spectrogram.bmp
│   ├── constellation.bmp
│   ├── processed.wav
│   ├── transcription.txt
│   ├── scores.txt
│   ├── summary.txt
│   ├── postcard.png
│   └── e1m7-607/
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

## Versioning

The `VERSION` file at the project root contains the version number (integer). It is the single source of truth used by:

- **Makefile**: reads `VERSION` into `APP_VERSION`, sets `PACKAGE_VERSION=v$(APP_VERSION)` for package naming (`exp4023-pretty-DOOMed-v6`)
- **Compile-time banner**: passed as `-DAPP_VERSION` to `main.cpp`, displayed at startup (`=== PRETTY DOOMed v6 ===`)
- **Changelog**: version-specific docs in `docs/changelog/` (e.g., `V2_TO_V3.md`)

## SEPP Deployment

Target: Alpine Linux 3.21.3, ARM32 (armv7l), musl libc.

```
exp4023-pretty-DOOMed-v<VERSION>/
├── run                     # Entrypoint
├── pretty-doomed           # Pipeline binary (sherpa-onnx statically linked)
├── opssat-doom             # DOOM binary (static)
├── config.cfg
├── variants.cfg
├── assets/                 # Logo images for postcard generation
├── libs/                   # Bundled shared libraries (GNU Radio, Boost, etc.)
├── models/                 # Speech-to-text models
│   └── sherpa-onnx/
│       └── small/          # sherpa-onnx model (~27 MB)
├── demos/                  # doom.wad + demo files
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

8. **Operation feature flag** -- The `operation` config key selects what the voice command triggers. Currently only `doom` is implemented, but the architecture supports future operations (each with its own prefixed config keys).

9. **DOOM-themed postcard** -- After each DOOM run, a composite postcard image is generated using the DOOM PLAYPAL palette. Includes gameplay frame, I/Q blood splatter, FFTW spectrogram, logos, and run metadata. Configurable via `doom_enable_postcard` and `doom_postcard_scale`.

10. **Optional hardware FIR decimation** -- The AD9361 has a programmable FIR filter that can decimate in hardware before DMA, reducing the sample rate the ARM cores must process. Configurable via `sdr_hw_fir_enable`. When disabled (default), the pipeline operates at 2.4 MSPS with 12x software decimation. When enabled, the AD9361 decimates to a lower rate (e.g. 600 kSPS) and software decimation is reduced accordingly (e.g. 3x for 200 kHz effective). The AD9361 minimum baseband rate without the FIR is 2.083 MSPS; rates below this require the hardware FIR. AD9361 register configuration (LO, gain, rate, FIR) runs once before the capture loop via `ad9361_configure()` and persists in hardware across captures. The GNU Radio flowgraph (IIO buffer, signal processing blocks) is still created and destroyed per capture since `top_block` does not support stop-then-restart. The FIR is disabled after captures complete via `ad9361_cleanup_fir()`.

11. **DOOM fireball constellation** -- The I/Q constellation BMP uses a radial color gradient inspired by DOOM fireballs: white-hot center fading through orange and blood red to dark maroon at the edges.

12. **Two-stage background processing** -- In background mode, the pipeline is split into an STT stage (`process_wav_stt`) that chains sequentially (Transcriber is not thread-safe) and an exec stage (`process_wav_exec`) that fires async. This lets STT for capture N+1 start immediately after STT for capture N, while DOOM + Postcard for capture N runs concurrently. Exec stages can theoretically overlap if DOOM + Postcard outlasts the next STT, creating a potential race on `doom_demo_index.txt`. On the EM this is unlikely given ~40s STT vs ~13-41s exec.
