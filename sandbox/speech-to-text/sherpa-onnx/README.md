# Sherpa-ONNX Speech-to-Text Evaluation

Evaluate Sherpa-ONNX STT accuracy on OPS-SAT audio samples.

## Quick Start

```bash
# Build and test all models
docker-compose build
docker-compose run --rm sherpa make test-all-models
```

## Models

| Model | Size | Command |
|-------|------|---------|
| zipformer-small-en | ~27 MB | `make test-small` |
| zipformer-en (base) | ~68 MB | `make test-base` |
| zipformer-large-en | ~147 MB | `make test-large` |

All models use int8 quantization.

## Test Samples

- `georges_opssat_clean.wav` - Clean voice sample
- `georges_opssat_noisy.wav` - Noisy voice sample
- `georges_opssat_very_noisy.wav` - Very noisy voice sample

## Output

Results written to `output/{small,base,large}/` directories.
