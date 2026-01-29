# PRETTY DOOMed - Design Document

## Overview

PRETTY DOOMed is a voice-command-to-DOOM pipeline for the OPS-SAT spacecraft. It processes audio captured from amateur radio transmissions, applies signal conditioning, performs speech-to-text transcription, detects voice commands through fuzzy matching, and launches DOOM when the command is recognized.

All audio processing happens in memory — no intermediate files between pipeline stages.

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
└── executor.h       [pure C++17]
```

### Module Responsibilities

| Module | File | Responsibility | External Deps |
|--------|------|----------------|---------------|
| Config | `config.cpp` | Parse `config.cfg` (KEY=VALUE, including model paths) and `variants.cfg` (TARGET=V1,V2,...) | None |
| Audio I/O | `audio_io.cpp` | Read/write WAV files, stereo-to-mono downmix | libsndfile |
| DSP | `dsp.cpp` | GNU Radio FIR filter blocks (lowpass, bandpass) + linear interpolation resampling | GNU Radio |
| Transcriber | `transcriber.cpp` | Sherpa-ONNX offline recognition: init, feed audio, decode, get text | sherpa-onnx |
| Matcher | `matcher.cpp` | Fuzzy matching with Levenshtein distance + variant lookup, command detection | None |
| Executor | `executor.cpp` | Fork+exec DOOM binary for each demo file | None |
| Main | `main.cpp` | CLI arg parsing, pipeline orchestration, output file writing | All |

### Dependency Isolation

Three modules are pure C++17 with zero external dependencies:

- **config** — string parsing (including model paths)
- **matcher** — string matching algorithms
- **executor** — DOOM process execution

These are fully unit-testable without installing GNU Radio, sherpa-onnx, or libsndfile. The test binary links only these pure modules. DSP integration tests require GNU Radio and run via `make test-dsp`.

## Configuration

All parameters are externalized in two config files, not compiled into the binary.

### `config.cfg` — Pipeline Parameters

```ini
# Signal Processing
lowpass_cutoff=3400
bandpass_low=300
bandpass_high=3400
lowpass_transition=500
bandpass_transition=100

# Speech-to-Text
model_encoder=model/encoder-epoch-99-avg-1.int8.onnx
model_decoder=model/decoder-epoch-99-avg-1.onnx
model_joiner=model/joiner-epoch-99-avg-1.int8.onnx
model_tokens=model/tokens.txt
decoding_method=modified_beam_search
num_threads=1

# Detection
wake_word=PRETTY
call_signs=NIGHT,LIGHT,HEART,...
command=DOOM
fuzzy_max_distance=2
```

### `variants.cfg` — Fuzzy Match Variants

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

**Tier 1 — Single BPE token (best reliability):** NIGHT, LIGHT, HEART, POWER, VOICE, HORSE, HOUSE, DARK, HOME, WORLD, COUNT, WONDER, PRINCE, WHITE, FRIEND, OPEN, PLACE

**Tier 2 — NATO phonetic alphabet (2-5 tokens):** ALPHA through ZULU

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
| dsp | `test_dsp.cpp` | FIR convolution correctness, resampling ratios |

### Running Tests

```bash
make test
```

The test binary links only pure C++17 modules — no Docker, no external libraries needed.

## Output Structure

Each pipeline run produces:

```
toGround/run-000001/
├── pretty_doomed.log       # Full pipeline log
├── capture.cf32            # Raw I/Q samples from SDR capture
├── denoised.wav            # Filtered audio
├── transcription.txt       # Transcription text
├── scores.txt              # Detection counts
├── summary.txt             # Human-readable summary
├── doom.log                # DOOM stdout/stderr (if triggered)
└── runs/                   # DOOM demo runs (if triggered)
    └── e1m7-607/
        ├── stats.txt
        └── frame-001920.jpg
```

## SEPP Deployment

Target: Alpine Linux 3.21.3, ARM32 (armv7l), musl libc.

```
exp4023-pretty-DOOMed-v1/
├── run                     # Entrypoint
├── pretty_doomed           # Pipeline binary
├── opssat-doom             # DOOM binary
├── config.cfg
├── variants.cfg
├── libs/                   # GNU Radio shared libraries
├── model/                  # sherpa-onnx model (~27 MB)
├── demos/                  # doom.wad + demo files
├── input/                  # Sample WAV
└── toGround/
```

## Key Design Decisions

1. **In-memory processing** — No intermediate files between pipeline stages. WAV is read once into a float buffer, filtered in-place, resampled, and fed directly to sherpa-onnx.

2. **GNU Radio for signal processing** — Uses GNU Radio `top_block` with `vector_source_f` -> `fir_filter_fff` -> `vector_sink_f` for in-memory FIR filtering. Filter taps designed with `firdes`.

3. **Sherpa-ONNX over Whisper** — The int8-quantized zipformer-small model is ~27 MB vs Whisper tiny at ~75 MB. Sherpa-ONNX also supports `modified_beam_search` which improves WER by 17% relative over greedy search.

4. **Variant-based matching over hotwords** — Hotword boosting degrades accuracy with BPE models (disrupts learned word boundaries). Post-transcription fuzzy matching with externalized variants is more effective.

5. **Single-token call signs** — Words that map to a single BPE token (NIGHT, LIGHT, etc.) are transcribed far more reliably than multi-token words (NATO alphabet).

6. **Externalized config** — All thresholds, call signs, and variants in text files, not compiled constants. Allows tuning without rebuilding.
