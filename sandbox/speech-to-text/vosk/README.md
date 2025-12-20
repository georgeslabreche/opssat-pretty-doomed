# Vosk Speech-to-Text Evaluation

Evaluate Vosk STT accuracy on OPS-SAT audio samples.

## Quick Start

```bash
# Build and test all models
docker-compose build
docker-compose run --rm vosk make test-all-models
```

## Models

| Model | Size | WER (LibriSpeech) | Command |
|-------|------|-------------------|---------|
| vosk-model-small-en-us-0.15 | ~40 MB | 9.85% | `make test-small` |
| vosk-model-en-us-0.22-lgraph | ~128 MB | 7.82% | `make test-lgraph` |

## Test Samples

- `georges_opssat_clean.wav` - Clean voice sample
- `georges_opssat_noisy.wav` - Noisy voice sample
- `georges_opssat_very_noisy.wav` - Very noisy voice sample

## Output

Results written to `output/{small,lgraph}/` directories.
