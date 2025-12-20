# PocketSphinx Speech-to-Text Evaluation

Evaluate PocketSphinx STT accuracy on OPS-SAT audio samples.

## Quick Start

```bash
# Build and run all tests
docker-compose build
docker-compose run pocketsphinx make test-all
```

## Model

Uses PocketSphinx 5.x default acoustic model (built-in, ~11 MB).

## Test Samples

- `georges_opssat_clean.wav` - Clean voice sample
- `georges_opssat_noisy.wav` - Noisy voice sample
- `georges_opssat_very_noisy.wav` - Very noisy voice sample
- `SDRSharp_*.wav` - Real OPS-SAT SDR capture

## Ground Truth

Georges samples: "PRETTY Play DOOM"

## Output

Results written to `output/` directory with WER calculations.
