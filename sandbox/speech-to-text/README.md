# Speech-to-Text Engine Evaluation

Evaluation of STT engines for OPS-SAT spacecraft voice command recognition.

## Test Environment

**Hardware:**
- MacBook Air (Apple M2)
- 8 cores (4 performance + 4 efficiency)
- 24 GB RAM

**Execution:** Docker containers (linux/amd64 via Rosetta emulation)

## Test Setup

**Ground Truth** ([georges_opssat_transcript.txt](../../samples/georges/georges_opssat_transcript.txt) - 22 words):
```
opssat play doom opssat play doom opssat play doom pretty play doom pretty play doom pretty play doom opssat pretty play doom
```

**Test Samples:**
- [georges_opssat_clean.wav](../../samples/georges/georges_opssat_clean.wav) - Clean voice recording
- [georges_opssat_noisy.wav](../../samples/georges/georges_opssat_noisy.wav) - With background noise
- [georges_opssat_very_noisy.wav](../../samples/georges/georges_opssat_very_noisy.wav) - Heavy noise

**Audio Duration:** ~23 seconds per sample (46875 Hz, stereo, resampled to 16 kHz mono)

## Results

### Word Error Rate (WER)

*Lower is better. 0% = perfect transcription.*

| Engine | Model | Size | Clean | Noisy | Very Noisy |
|--------|-------|------|-------|-------|------------|
| Sherpa-ONNX | zipformer-small-en | 27 MB | **27.27%** | 54.55% | 95.45% |
| Sherpa-ONNX | zipformer-en (base) | 68 MB | **27.27%** | **40.91%** | 90.91% |
| Sherpa-ONNX | zipformer-large-en | 147 MB | 45.45% | 59.09% | 90.91% |
| Vosk | small-en-us-0.15 | 40 MB | 54.55% | 86.36% | 100.00% |
| Vosk | en-us-0.22-lgraph | 128 MB | 36.36% | 86.36% | 100.00% |
| PocketSphinx | built-in | 11 MB | 127.27% | 100.00% | 100.00% |

### Execution Time (Clean Audio)

*Time to transcribe ~23 seconds of audio.*

| Engine | Model | Size | Time | Real-time Factor |
|--------|-------|------|------|------------------|
| Vosk | small-en-us-0.15 | 40 MB | **2.0s** | 0.09x |
| Vosk | en-us-0.22-lgraph | 128 MB | **10.0s** | 0.43x |
| Sherpa-ONNX | zipformer-small-en | 27 MB | 41.9s | 1.82x |
| PocketSphinx | built-in | 11 MB | 41.6s | 1.81x |
| Sherpa-ONNX | zipformer-en (base) | 68 MB | 52.5s | 2.28x |
| Sherpa-ONNX | zipformer-large-en | 147 MB | 69.7s | 3.03x |

*Real-time factor: <1x means faster than real-time*

### Word Accuracy (Clean Audio)

*Higher is better. Considers "upset/upsat/opposet" as phonetic match for "opssat".*

| Engine | Model | Accuracy | Key Observations |
|--------|-------|----------|------------------|
| Sherpa-ONNX | zipformer-small-en | ~91% | "OPPOSSET PLAY DOOM...PRETTY PLAY DOOM" |
| Sherpa-ONNX | zipformer-en (base) | ~91% | "UPSET PLAY DOOM...PRETTY PLAY DOOM" |
| Vosk | en-us-0.22-lgraph | ~82% | "upset play doom...pretty play doom" |
| Vosk | small-en-us-0.15 | ~68% | "doom" often heard as "do" |
| Sherpa-ONNX | zipformer-large-en | ~77% | "OPSATT...PLAYDOUM" (merged words) |
| PocketSphinx | built-in | ~18% | Heavy hallucination |

## Key Findings

1. **Best Accuracy:** Sherpa-ONNX Base (68 MB) - 27% WER on clean, 41% on noisy
2. **Best Speed:** Vosk Small (40 MB) - 11x faster than real-time
3. **Best Size/Accuracy:** Sherpa-ONNX Small (27 MB) - Same clean accuracy as Base at half the size
4. **Larger ≠ Better:** Sherpa-ONNX Large performed worse than Base model
5. **Noise Robustness:** Only Sherpa-ONNX maintains usable accuracy on noisy audio

## Recommendation

**For OPS-SAT deployment, consider:**

| Priority | Engine | Model | Size | Clean WER | Speed |
|----------|--------|-------|------|-----------|-------|
| Accuracy | Sherpa-ONNX | zipformer-en (base) | 68 MB | 27% | 2.3x RT |
| Balanced | Sherpa-ONNX | zipformer-small-en | 27 MB | 27% | 1.8x RT |
| Speed | Vosk | small-en-us-0.15 | 40 MB | 55% | 0.09x RT |

**Sherpa-ONNX Small (27 MB)** offers the best balance:
- Same clean accuracy as the Base model
- Half the model size
- Modern Zipformer architecture
- Active development with ARM support

## Running the Evaluations

```bash
# Sherpa-ONNX (all models)
cd sherpa-onnx && docker-compose run --rm sherpa make test-all-models

# Vosk (all models)
cd vosk && docker-compose run --rm vosk make test-all-models

# PocketSphinx
cd pocket-sphinx && docker-compose run --rm pocketsphinx make test-all
```
