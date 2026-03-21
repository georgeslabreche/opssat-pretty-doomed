# Sherpa-ONNX Speech-to-Text for OPS-SAT SEPP

> **Status:** Validated on EM (v2). Model loads in ~26s, transcription takes ~288s for 22s of audio on ARM32. Integrated into [`pretty-doomed`](../../../pretty-doomed/) (via `transcriber.cpp` with persistent Transcriber class).

Speech-to-text transcription using Sherpa-ONNX with the zipformer-small model, built from source for Alpine Linux ARM32 (musl).

## Model

- **Name**: sherpa-onnx-zipformer-small-en-2023-06-26
- **Type**: int8 quantized (for smaller size)
- **Size**: ~27 MB
- **Language**: English

## Prerequisites

1. Setup QEMU emulation:
   ```bash
   docker run --rm --privileged tonistiigi/binfmt --install arm
   ```

2. Import exp_env (from repository root):
   ```bash
   docker import --platform linux/arm/v7 resources/exp_env.tar.gz exp_env:latest
   ```

3. Download the small model (if not already present):
   ```bash
   cd ../sherpa-onnx && make download-model-small
   ```

## Building

```bash
# Build the Docker image
docker-compose build

# Build sherpa-onnx from source (takes time with QEMU emulation)
docker-compose run --rm sherpa-onnx make

# Create the deployment package
docker-compose run --rm sherpa-onnx make package-prepare

# Copy model and samples (run outside container)
make package-model
./setup-samples.sh
make package-tar
```

## Package Contents

```
exp4023-sherpa-onnx-v1.tar.gz
├── run                    # Entry point script
├── sherpa-onnx-offline    # Statically linked binary
├── model/                 # Sherpa-ONNX model (int8)
│   ├── encoder-epoch-99-avg-1.int8.onnx
│   ├── decoder-epoch-99-avg-1.onnx
│   ├── joiner-epoch-99-avg-1.int8.onnx
│   └── tokens.txt
└── input/                 # Sample WAV files
```

## Output

Each run creates a directory in `toGround/`:

```
toGround/run-000001/
├── georges_opssat_clean.txt    # Transcription
├── georges_opssat_noisy.txt
├── sherpa_onnx.log             # Processing log
└── summary.txt                 # Results summary
```

## Usage

On SEPP:
```bash
./run
```

The script will:
1. Process all WAV files in `input/`
2. Create transcriptions in `toGround/run-NNNNNN/`
3. Generate summary with all transcriptions
