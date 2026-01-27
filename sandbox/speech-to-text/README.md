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

| Engine | Model | Size | Decoding | Clean | Noisy | Very Noisy |
|--------|-------|------|----------|-------|-------|------------|
| Sherpa-ONNX | zipformer-small-en | 27 MB | beam | **22.73%** | **50.00%** | 90.91% |
| Sherpa-ONNX | zipformer-small-en | 27 MB | greedy | 27.27% | 54.55% | 95.45% |
| Sherpa-ONNX | zipformer-en (base) | 68 MB | greedy | 27.27% | 40.91% | 90.91% |
| Sherpa-ONNX | zipformer-large-en | 147 MB | greedy | 45.45% | 59.09% | 90.91% |
| Vosk | small-en-us-0.15 | 40 MB | - | 54.55% | 86.36% | 100.00% |
| Vosk | en-us-0.22-lgraph | 128 MB | - | 36.36% | 86.36% | 100.00% |
| PocketSphinx | built-in | 11 MB | - | 127.27% | 100.00% | 100.00% |

**Decoding methods:** `greedy` = greedy_search (fast), `beam` = modified_beam_search (better accuracy)

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

1. **Best Accuracy:** Sherpa-ONNX Small + beam search (27 MB) - **22.73% WER** on clean
2. **Beam Search Improves Accuracy:** 4.5 pp reduction in WER (27.27% → 22.73%), i.e., 17% fewer errors
3. **Best Speed:** Vosk Small (40 MB) - 11x faster than real-time
4. **Best Size/Accuracy:** Sherpa-ONNX Small (27 MB) - Best clean accuracy at smallest Sherpa size
5. **Larger ≠ Better:** Sherpa-ONNX Large performed worse than Small and Base models
6. **Noise Robustness:** Only Sherpa-ONNX maintains usable accuracy on noisy audio

## Recommendation

**For OPS-SAT deployment, consider:**

| Priority | Engine | Model | Decoding | Size | Clean WER | Speed |
|----------|--------|-------|----------|------|-----------|-------|
| Accuracy | Sherpa-ONNX | zipformer-small-en | beam | 27 MB | **22.73%** | ~2.7x RT |
| Balanced | Sherpa-ONNX | zipformer-small-en | greedy | 27 MB | 27.27% | ~2.3x RT |
| Speed | Vosk | small-en-us-0.15 | - | 40 MB | 54.55% | 0.09x RT |

**Sherpa-ONNX Small (27 MB) with `modified_beam_search`** offers the best accuracy:
- Lowest WER (22.73%) among all tested configurations
- Smallest Sherpa model size
- Modern Zipformer architecture
- Active development with ARM support

## OPS-SAT SEPP Deployment

The `sherpa-onnx-sepp/` directory contains the deployment package for OPS-SAT SEPP.

### Package Details

| Component | Size | Description |
|-----------|------|-------------|
| `sherpa-onnx-offline` | 7.3 MB | UPX-compressed ARM32 binary |
| Model (int8 quantized) | 27 MB | zipformer-small-en |
| **Total package** | **~34 MB** | `exp4023-sherpa-onnx-v1.tar.gz` |

### Build Process

The binary is built from source inside a QEMU-emulated Alpine ARM32 container for musl libc compatibility:

```bash
cd sherpa-onnx-sepp

# Build (requires QEMU and exp_env Docker image)
docker-compose build
docker-compose run --rm sherpa-onnx make

# Create deployment package
docker-compose run --rm sherpa-onnx make package-prepare
make package-model
./setup-samples.sh
make package-tar
```

### Package Structure

```
exp4023-sherpa-onnx-v1/
├── sherpa-onnx-offline      # ARM32 musl binary (UPX compressed)
├── run                      # Entry point script
├── model/
│   ├── encoder-epoch-99-avg-1.int8.onnx
│   ├── decoder-epoch-99-avg-1.onnx
│   ├── joiner-epoch-99-avg-1.int8.onnx
│   └── tokens.txt
└── input/                   # Place WAV files here
    └── *.wav
```

### Output Structure

Each run creates a timestamped folder:

```
toGround/
├── run-000001/
│   ├── sherpa_onnx.log      # Detailed execution log
│   ├── <filename>.txt       # Transcription text
│   ├── <filename>.json      # Full JSON with timestamps
│   └── summary.txt          # Human-readable summary
└── run-000002/
    └── ...
```

### Hotwords (Not Recommended)

Testing showed that hotwords with BPE models can **degrade** accuracy:

| Configuration | Clean WER |
|--------------|-----------|
| modified_beam_search (no hotwords) | **22.73%** |
| modified_beam_search + hotwords | 31.82% |

The BPE tokenization makes hotword matching difficult - single-letter tokens disrupt word formation, and multi-character BPE tokens (like `▁PRE`) require version-specific parser support.

**Recommendation:** Use intent matching on the transcription output instead:
- Treat "OP SAT", "UPSET", "OPPOSET", "OPSSAT" as equivalent triggers for "OPSSAT"
- Treat "PLAY DO", "PLAY DOOM", "PLAY DOM" as equivalent triggers for "PLAY DOOM"

### Build Notes

- **glibc compatibility stubs**: The pre-built onnxruntime library requires glibc symbols (`__libc_single_threaded`, `backtrace`, `stat64`) that are shimmed for musl
- **Source patching**: 90+ files patched to add missing `#include <cstdint>` for GCC 14 on musl
- **UPX compression**: Binary compressed from 16 MB to 7.3 MB (self-extracting, no UPX needed on target)

## Running the Evaluations

```bash
# Sherpa-ONNX (all models)
cd sherpa-onnx && docker-compose run --rm sherpa make test-all-models

# Vosk (all models)
cd vosk && docker-compose run --rm vosk make test-all-models

# PocketSphinx
cd pocket-sphinx && docker-compose run --rm pocketsphinx make test-all
```
